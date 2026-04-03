#include "entity_factory.hpp"

#include <filesystem>

#include "../../assets/model_importer.hpp"
#include "../../components/audio_listener_component.hpp"
#include "../../components/audio_source_component.hpp"
#include "../../components/camera_component.hpp"
#include "../../components/collider_component.hpp"
#include "../../components/model_component.hpp"
#include "../../components/name_component.hpp"
#include "../../components/position_component_3d.hpp"
#include "../../components/primitive_component.hpp"
#include "../../components/rigid_body_component.hpp"
#include "../../components/rotation_component_3d.hpp"
#include "../../components/text_component.hpp"
#include "../../components/transform_hierarchy_component.hpp"
#include "../../components/world_component.hpp"
#include "component_manager.hpp"
#include "entity_manager.hpp"
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
      for (Entity::EntityId entity : entityManager.getAllEntities())
      {
        if (world.has_value() && !entity_belongs_to_world(entity, *world, componentManager))
        {
          continue;
        }

        if (componentManager.hasComponent<CameraComponent>(entity))
        {
          return true;
        }
      }

      return false;
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

  Entity::EntityId EntityFactory::createPhysicsCube(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::optional<Entity::EntityId> parent)
  {
    const auto entity = createBaseEntity(entityManager, componentManager, "Physics Cube", parent);
    componentManager.addComponent(entity, PrimitiveComponent());
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

  std::optional<Entity::EntityId> EntityFactory::createImportedModel(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      const std::filesystem::path &sourcePath,
      std::optional<Entity::EntityId> parent,
      std::string *errorMessage)
  {
    const auto importedModel = ModelImporter::importFromFile(sourcePath, errorMessage);
    if (!importedModel.has_value())
    {
      return std::nullopt;
    }

    const std::string entityName = sourcePath.stem().string().empty() ? "Imported Model" : sourcePath.stem().string();
    const auto entity = createBaseEntity(entityManager, componentManager, entityName, parent);
    componentManager.addComponent(entity, ModelComponent{*importedModel});
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
  }
}
