#include "physics_system.hpp"

#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

#include "../components/collider_component.hpp"
#include "../components/position_component_3d.hpp"
#include "../components/rigid_body_component.hpp"
#include "../components/rotation_component_3d.hpp"
#include "../components/transform_hierarchy_component.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"
#include "../core/ecs/query.hpp"
#include "../core/events/event_bus.hpp"
#include "../core/events/events.hpp"
#include "../physics/physics_layers.hpp"
#include "../physics/physics_math.hpp"
#include "../physics/physics_world.hpp"

namespace hades
{
  void PhysicsSystem::setPhysicsWorld(PhysicsWorld *physicsWorld)
  {
    physicsWorld_ = physicsWorld;
  }

  void PhysicsSystem::set_active_world(std::optional<Entity::EntityId> activeWorld)
  {
    activeWorld_ = activeWorld;
  }

  void PhysicsSystem::update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager)
  {
    if (physicsWorld_ == nullptr || !physicsWorld_->is_initialized())
    {
      return;
    }

    ensure_bodies(componentManager, entityManager);
    remove_stale_bodies(componentManager, entityManager);

    timeAccumulator_ += deltaTime;
    while (timeAccumulator_ >= FIXED_TIMESTEP)
    {
      physicsWorld_->step(FIXED_TIMESTEP);
      timeAccumulator_ -= FIXED_TIMESTEP;
    }

    sync_transforms(componentManager);
  }

  void PhysicsSystem::update(float deltaTime, SystemContext &context)
  {
    update(deltaTime, context.componentManager, context.entityManager);

    // Publish collision events from the physics step.
    if (physicsWorld_ != nullptr && physicsWorld_->is_initialized())
    {
      auto contacts = physicsWorld_->drain_contacts();
      for (const auto &contact : contacts)
      {
        // Look up entities from body IDs.
        auto itA = bodyToEntity_.find(contact.bodyIdA);
        auto itB = bodyToEntity_.find(contact.bodyIdB);
        if (itA == bodyToEntity_.end() || itB == bodyToEntity_.end())
        {
          continue;
        }

        if (contact.began)
        {
          context.eventBus.publish(CollisionBeginEvent{
              itA->second, itB->second,
              contact.bodyIdA, contact.bodyIdB});
        }
        else
        {
          context.eventBus.publish(CollisionEndEvent{
              itA->second, itB->second});
        }
      }
    }
  }

  void PhysicsSystem::clear_bodies()
  {
    entityToBody_.clear();
    bodyToEntity_.clear();
    timeAccumulator_ = 0.0f;
  }

  void PhysicsSystem::ensure_bodies(ComponentManager &componentManager, EntityManager &entityManager)
  {
    for (Entity::EntityId entity : query<RigidBodyComponent, ColliderComponent>(entityManager, componentManager, activeWorld_))
    {
      auto &rb = componentManager.getComponent<RigidBodyComponent>(entity);
      if (rb.bodyCreated)
      {
        continue;
      }

      create_body_for_entity(entity, componentManager);
    }
  }

  void PhysicsSystem::remove_stale_bodies(ComponentManager &componentManager, EntityManager &entityManager)
  {
    std::vector<Entity::EntityId> toRemove;
    const auto &allEntities = entityManager.getActiveEntities();

    for (const auto &[entity, bodyId] : entityToBody_)
    {
      bool found = false;
      for (Entity::EntityId e : allEntities)
      {
        if (e == entity)
        {
          found = true;
          break;
        }
      }

      if (!found ||
          !componentManager.hasComponent<RigidBodyComponent>(entity) ||
          !componentManager.hasComponent<ColliderComponent>(entity))
      {
        toRemove.push_back(entity);
      }
    }

    for (Entity::EntityId entity : toRemove)
    {
      destroy_body_for_entity(entity, componentManager);
    }
  }

  void PhysicsSystem::sync_transforms(ComponentManager &componentManager)
  {
    JPH::BodyInterface &bodyInterface = physicsWorld_->body_interface();

    for (const auto &[entity, bodyIdValue] : entityToBody_)
    {
      if (!componentManager.hasComponent<RigidBodyComponent>(entity))
      {
        continue;
      }

      const auto &rb = componentManager.getComponent<RigidBodyComponent>(entity);
      if (rb.type == RigidBodyType::Static)
      {
        continue;
      }

      JPH::BodyID bodyId(bodyIdValue);
      JPH::Vec3 position = bodyInterface.GetPosition(bodyId);
      JPH::Quat rotation = bodyInterface.GetRotation(bodyId);

      if (componentManager.hasComponent<PositionComponent3D>(entity))
      {
        auto &pos = componentManager.getComponent<PositionComponent3D>(entity);
        physics::from_jph(position, pos);
      }

      if (componentManager.hasComponent<RotationComponent3D>(entity))
      {
        auto &rot = componentManager.getComponent<RotationComponent3D>(entity);
        physics::from_jph(rotation, rot);
      }

      if (componentManager.hasComponent<TransformHierarchyComponent>(entity))
      {
        auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
        hierarchy.transformDirty = true;
      }
    }
  }

  void PhysicsSystem::create_body_for_entity(Entity::EntityId entity, ComponentManager &componentManager)
  {
    auto &rb = componentManager.getComponent<RigidBodyComponent>(entity);
    const auto &collider = componentManager.getComponent<ColliderComponent>(entity);

    // Build shape.
    JPH::ShapeRefC shape;
    switch (collider.shape)
    {
    case ColliderShape::Box:
      shape = new JPH::BoxShape(JPH::Vec3(collider.halfExtentX, collider.halfExtentY, collider.halfExtentZ));
      break;
    case ColliderShape::Sphere:
      shape = new JPH::SphereShape(collider.radius);
      break;
    case ColliderShape::Capsule:
      shape = new JPH::CapsuleShape(collider.capsuleHalfHeight, collider.capsuleRadius);
      break;
    }

    // Read initial transform.
    JPH::Vec3 position(0.0f, 0.0f, 0.0f);
    if (componentManager.hasComponent<PositionComponent3D>(entity))
    {
      position = physics::to_jph(componentManager.getComponent<PositionComponent3D>(entity));
    }

    JPH::Quat rotation = JPH::Quat::sIdentity();
    if (componentManager.hasComponent<RotationComponent3D>(entity))
    {
      rotation = physics::to_jph(componentManager.getComponent<RotationComponent3D>(entity));
    }

    // Map RigidBodyType to Jolt motion type and object layer.
    JPH::EMotionType motionType;
    JPH::ObjectLayer objectLayer;
    switch (rb.type)
    {
    case RigidBodyType::Static:
      motionType = JPH::EMotionType::Static;
      objectLayer = physics::Layers::NON_MOVING;
      break;
    case RigidBodyType::Kinematic:
      motionType = JPH::EMotionType::Kinematic;
      objectLayer = physics::Layers::MOVING;
      break;
    case RigidBodyType::Dynamic:
    default:
      motionType = JPH::EMotionType::Dynamic;
      objectLayer = physics::Layers::MOVING;
      break;
    }

    JPH::BodyCreationSettings settings(shape, position, rotation, motionType, objectLayer);

    if (rb.type == RigidBodyType::Dynamic)
    {
      settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
      settings.mMassPropertiesOverride.mMass = rb.mass;
    }

    settings.mFriction = rb.friction;
    settings.mRestitution = rb.restitution;
    settings.mLinearDamping = rb.linearDamping;
    settings.mAngularDamping = rb.angularDamping;
    settings.mGravityFactor = rb.gravityScale;

    JPH::BodyInterface &bodyInterface = physicsWorld_->body_interface();
    JPH::Body *body = bodyInterface.CreateBody(settings);
    if (body == nullptr)
    {
      return;
    }

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);

    rb.bodyIdValue = body->GetID().GetIndexAndSequenceNumber();
    rb.bodyCreated = true;

    entityToBody_[entity] = rb.bodyIdValue;
    bodyToEntity_[rb.bodyIdValue] = entity;
  }

  void PhysicsSystem::destroy_body_for_entity(Entity::EntityId entity, ComponentManager &componentManager)
  {
    auto it = entityToBody_.find(entity);
    if (it == entityToBody_.end())
    {
      return;
    }

    std::uint32_t bodyIdValue = it->second;
    JPH::BodyID bodyId(bodyIdValue);
    JPH::BodyInterface &bodyInterface = physicsWorld_->body_interface();

    bodyInterface.RemoveBody(bodyId);
    bodyInterface.DestroyBody(bodyId);

    bodyToEntity_.erase(bodyIdValue);
    entityToBody_.erase(it);

    if (componentManager.hasComponent<RigidBodyComponent>(entity))
    {
      auto &rb = componentManager.getComponent<RigidBodyComponent>(entity);
      rb.bodyCreated = false;
      rb.bodyIdValue = 0xFFFFFFFF;
    }
  }
}
