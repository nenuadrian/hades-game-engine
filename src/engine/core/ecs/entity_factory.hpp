#ifndef HADES_ENGINE_CORE_ECS_ENTITY_FACTORY_HPP
#define HADES_ENGINE_CORE_ECS_ENTITY_FACTORY_HPP

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

    static Entity::EntityId createPlane(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        std::optional<Entity::EntityId> parent = std::nullopt);

    static Entity::EntityId createSphere(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        std::optional<Entity::EntityId> parent = std::nullopt);

    static Entity::EntityId createModel(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        std::optional<Entity::EntityId> parent = std::nullopt);

    static Entity::EntityId createPhysicsCube(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        std::optional<Entity::EntityId> parent = std::nullopt);

    static Entity::EntityId createWorld(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        const std::string &name,
        bool isDefault = false);

    static Entity::EntityId createDirectionalLight(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        std::optional<Entity::EntityId> parent = std::nullopt);

    static Entity::EntityId createPointLight(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        std::optional<Entity::EntityId> parent = std::nullopt);

    static Entity::EntityId createSpotLight(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        std::optional<Entity::EntityId> parent = std::nullopt);

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
