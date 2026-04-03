#include "physics_forces.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include "../components/rigid_body_component.hpp"
#include "../core/ecs/component_manager.hpp"
#include "physics_world.hpp"

namespace hades::physics
{
  void apply_force(PhysicsWorld &world, ComponentManager &cm, Entity::EntityId entity, float fx, float fy, float fz)
  {
    if (!cm.hasComponent<RigidBodyComponent>(entity))
    {
      return;
    }

    const auto &rb = cm.getComponent<RigidBodyComponent>(entity);
    if (!rb.bodyCreated)
    {
      return;
    }

    JPH::BodyID bodyId(rb.bodyIdValue);
    world.body_interface().AddForce(bodyId, JPH::Vec3(fx, fy, fz));
  }

  void apply_impulse(PhysicsWorld &world, ComponentManager &cm, Entity::EntityId entity, float ix, float iy, float iz)
  {
    if (!cm.hasComponent<RigidBodyComponent>(entity))
    {
      return;
    }

    const auto &rb = cm.getComponent<RigidBodyComponent>(entity);
    if (!rb.bodyCreated)
    {
      return;
    }

    JPH::BodyID bodyId(rb.bodyIdValue);
    world.body_interface().AddImpulse(bodyId, JPH::Vec3(ix, iy, iz));
  }

  void set_linear_velocity(PhysicsWorld &world, ComponentManager &cm, Entity::EntityId entity, float vx, float vy, float vz)
  {
    if (!cm.hasComponent<RigidBodyComponent>(entity))
    {
      return;
    }

    const auto &rb = cm.getComponent<RigidBodyComponent>(entity);
    if (!rb.bodyCreated)
    {
      return;
    }

    JPH::BodyID bodyId(rb.bodyIdValue);
    world.body_interface().SetLinearVelocity(bodyId, JPH::Vec3(vx, vy, vz));
  }
}
