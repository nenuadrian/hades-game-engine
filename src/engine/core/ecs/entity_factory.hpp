#ifndef HADES_ENGINE_CORE_ECS_ENTITY_FACTORY_HPP
#define HADES_ENGINE_CORE_ECS_ENTITY_FACTORY_HPP

#include <filesystem>
#include <optional>
#include <string>

#include "entity.hpp"

namespace hades
{
  class ComponentManager;
  class EntityManager;

  class EntityFactory
  {
  public:
    static Entity::EntityId createCamera(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        std::optional<Entity::EntityId> parent = std::nullopt);

    static Entity::EntityId createCube(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        std::optional<Entity::EntityId> parent = std::nullopt);

    static Entity::EntityId createText(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        std::optional<Entity::EntityId> parent = std::nullopt);

    static Entity::EntityId createAudioEmitter(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        std::optional<Entity::EntityId> parent = std::nullopt);

    static Entity::EntityId createWorld(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        const std::string &name,
        bool isDefault = false);

    static std::optional<Entity::EntityId> createImportedModel(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        const std::filesystem::path &sourcePath,
        std::optional<Entity::EntityId> parent = std::nullopt,
        std::string *errorMessage = nullptr);

  private:
    static Entity::EntityId createBaseEntity(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        const std::string &name,
        std::optional<Entity::EntityId> parent,
        bool addPositionComponent = true);

    static void attachToParent(
        Entity::EntityId entity,
        ComponentManager &componentManager,
        std::optional<Entity::EntityId> parent);
  };
}

#endif
