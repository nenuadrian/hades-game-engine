#include "entity_factory.hpp"

#include "../../components/animation_component.hpp"
#include "../../components/animator_component.hpp"
#include "../../components/audio_listener_component.hpp"
#include "../../components/light_component.hpp"
#include "../../components/audio_source_component.hpp"
#include "../../components/camera_component.hpp"
#include "../../components/collider_component.hpp"
#include "../../components/mesh_renderer_component.hpp"
#include "../../components/model_component.hpp"
#include "../../components/name_component.hpp"
#include "../../components/position_component_3d.hpp"
#include "../../components/primitive_component.hpp"
#include "../../components/rigid_body_component.hpp"
#include "../../components/rotation_component_3d.hpp"
#include "../../components/scale_component_3d.hpp"
#include "../../components/text_component.hpp"
#include "../../components/transform_hierarchy_component.hpp"
#include "../../components/world_component.hpp"
#include "component_manager.hpp"
#include "entity_manager.hpp"
#include "query.hpp"
#include "world_utils.hpp"

namespace hades
{
  namespace
  {
    bool has_existing_camera(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        std::optional<Entity::EntityId> world)
    {
      return !query<CameraComponent>(entityManager, componentManager, world).empty();
    }
  }

  Entity::EntityId EntityFactory::createCamera(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::optional<Entity::EntityId> parent)
  {
    const auto entity = createBaseEntity(entityManager, componentManager, "Camera", parent);
    const auto world = world_for_entity(entity, componentManager);
    CameraComponent camera;
    camera.isMainCamera = !has_existing_camera(entityManager, componentManager, world);
    componentManager.addComponent(entity, camera);
    componentManager.addComponent(entity, RotationComponent3D());
    componentManager.addComponent(entity, AudioListenerComponent());
    return entity;
  }

  Entity::EntityId EntityFactory::createCube(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::optional<Entity::EntityId> parent)
  {
    const auto entity = createBaseEntity(entityManager, componentManager, "Cube", parent);
    componentManager.addComponent(entity, PrimitiveComponent());
    componentManager.addComponent(entity, MeshRendererComponent{});
    return entity;
  }

  Entity::EntityId EntityFactory::createText(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::optional<Entity::EntityId> parent)
  {
    const auto entity = createBaseEntity(entityManager, componentManager, "Text", parent);
    componentManager.addComponent(entity, TextComponent());
    return entity;
  }

  Entity::EntityId EntityFactory::createAudioEmitter(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::optional<Entity::EntityId> parent)
  {
    const auto entity = createBaseEntity(entityManager, componentManager, "Audio Emitter", parent);
    componentManager.addComponent(entity, AudioSourceComponent());
    return entity;
  }

  Entity::EntityId EntityFactory::createPlane(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::optional<Entity::EntityId> parent)
  {
    const auto entity = createBaseEntity(entityManager, componentManager, "Plane", parent);
    componentManager.addComponent(entity, PrimitiveComponent{PrimitiveType::Plane});
    componentManager.addComponent(entity, MeshRendererComponent{});
    return entity;
  }

  Entity::EntityId EntityFactory::createSphere(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::optional<Entity::EntityId> parent)
  {
    const auto entity = createBaseEntity(entityManager, componentManager, "Sphere", parent);
    componentManager.addComponent(entity, PrimitiveComponent{PrimitiveType::Sphere});
    componentManager.addComponent(entity, MeshRendererComponent{});
    return entity;
  }

  Entity::EntityId EntityFactory::createModel(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::optional<Entity::EntityId> parent)
  {
    const auto entity = createBaseEntity(entityManager, componentManager, "Model", parent);
    componentManager.addComponent(entity, ModelComponent{});
    // The animator, not the superseded clip player: with neither a graph nor a
    // default clip it starts the model's own first animation, which is what
    // AnimationComponent used to do here, and it can be grown into a state
    // machine without swapping components.
    componentManager.addComponent(entity, AnimatorComponent{});
    componentManager.addComponent(entity, RotationComponent3D{});
    componentManager.addComponent(entity, ScaleComponent3D{});
    return entity;
  }

  Entity::EntityId EntityFactory::createPhysicsCube(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::optional<Entity::EntityId> parent)
  {
    const auto entity = createBaseEntity(entityManager, componentManager, "Physics Cube", parent);
    componentManager.addComponent(entity, PrimitiveComponent());
    componentManager.addComponent(entity, MeshRendererComponent{});
    componentManager.addComponent(entity, RotationComponent3D());
    componentManager.addComponent(entity, RigidBodyComponent());
    componentManager.addComponent(entity, ColliderComponent());
    return entity;
  }

  Entity::EntityId EntityFactory::createWorld(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      const std::string &name,
      bool isDefault)
  {
    const auto entity = createBaseEntity(entityManager, componentManager, name, std::nullopt, false);
    componentManager.addComponent(entity, WorldComponent{isDefault});
    return entity;
  }

  Entity::EntityId EntityFactory::createDirectionalLight(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::optional<Entity::EntityId> parent)
  {
    const auto entity = createBaseEntity(entityManager, componentManager, "Directional Light", parent);
    LightComponent light;
    light.type = LightType::Directional;
    componentManager.addComponent(entity, light);
    return entity;
  }

  Entity::EntityId EntityFactory::createPointLight(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::optional<Entity::EntityId> parent)
  {
    const auto entity = createBaseEntity(entityManager, componentManager, "Point Light", parent);
    LightComponent light;
    light.type = LightType::Point;
    light.range = 10.0f;
    componentManager.addComponent(entity, light);
    return entity;
  }

  Entity::EntityId EntityFactory::createSpotLight(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::optional<Entity::EntityId> parent)
  {
    const auto entity = createBaseEntity(entityManager, componentManager, "Spot Light", parent);
    LightComponent light;
    light.type = LightType::Spot;
    light.range = 10.0f;
    light.innerConeAngle = 25.0f;
    light.outerConeAngle = 35.0f;
    componentManager.addComponent(entity, light);
    return entity;
  }

  Entity::EntityId EntityFactory::createBaseEntity(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      const std::string &name,
      std::optional<Entity::EntityId> parent,
      bool addPositionComponent)
  {
    const auto entity = entityManager.createEntity();
    componentManager.addComponent(entity, NameComponent{name});
    componentManager.addComponent(entity, TransformHierarchyComponent());
    if (addPositionComponent)
    {
      componentManager.addComponent(entity, PositionComponent3D());
    }
    attachToParent(entity, componentManager, parent);
    return entity;
  }

  void EntityFactory::attachToParent(
      Entity::EntityId entity,
      ComponentManager &componentManager,
      std::optional<Entity::EntityId> parent)
  {
    if (!parent.has_value() || !componentManager.hasComponent<TransformHierarchyComponent>(*parent))
    {
      return;
    }

    auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
    hierarchy.setParent(*parent);

    auto &parentHierarchy = componentManager.getComponent<TransformHierarchyComponent>(*parent);
    parentHierarchy.addChild(entity);

    invalidate_world_caches(entity, componentManager);
  }
}
