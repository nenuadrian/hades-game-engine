#ifndef HADES_ENGINE_CORE_ECS_WORLD_UTILS_HPP
#define HADES_ENGINE_CORE_ECS_WORLD_UTILS_HPP

#include <optional>
#include <vector>

#include "../../components/transform_hierarchy_component.hpp"
#include "../../components/world_component.hpp"
#include "component_manager.hpp"
#include "entity_manager.hpp"

namespace hades
{
  inline bool is_world_entity(Entity::EntityId entity, ComponentManager &componentManager)
  {
    return componentManager.hasComponent<WorldComponent>(entity);
  }

  inline std::optional<Entity::EntityId> world_for_entity(
      Entity::EntityId entity,
      ComponentManager &componentManager)
  {
    // Check the cache on the hierarchy component if it exists.
    if (componentManager.hasComponent<TransformHierarchyComponent>(entity))
    {
      auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
      if (!hierarchy.worldCacheDirty)
      {
        return hierarchy.cachedWorld;
      }
    }

    // Walk the parent chain to find the world root.
    Entity::EntityId current = entity;
    while (true)
    {
      if (componentManager.hasComponent<WorldComponent>(current))
      {
        // Cache the result on the original entity.
        if (componentManager.hasComponent<TransformHierarchyComponent>(entity))
        {
          auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
          hierarchy.cachedWorld = current;
          hierarchy.worldCacheDirty = false;
        }
        return current;
      }

      if (!componentManager.hasComponent<TransformHierarchyComponent>(current))
      {
        // Cache the miss too.
        if (componentManager.hasComponent<TransformHierarchyComponent>(entity))
        {
          auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
          hierarchy.cachedWorld = std::nullopt;
          hierarchy.worldCacheDirty = false;
        }
        return std::nullopt;
      }

      const auto &h = componentManager.getComponent<TransformHierarchyComponent>(current);
      if (!h.parent.has_value())
      {
        if (componentManager.hasComponent<TransformHierarchyComponent>(entity))
        {
          auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
          hierarchy.cachedWorld = std::nullopt;
          hierarchy.worldCacheDirty = false;
        }
        return std::nullopt;
      }

      current = *h.parent;
    }
  }

  inline bool entity_belongs_to_world(
      Entity::EntityId entity,
      Entity::EntityId world,
      ComponentManager &componentManager)
  {
    const auto entityWorld = world_for_entity(entity, componentManager);
    return entityWorld.has_value() && *entityWorld == world;
  }

  /// Recursively invalidate cached world lookups for an entity and all its
  /// descendants. Call this after modifying the transform hierarchy.
  inline void invalidate_world_caches(
      Entity::EntityId entity,
      ComponentManager &componentManager)
  {
    if (!componentManager.hasComponent<TransformHierarchyComponent>(entity))
    {
      return;
    }

    auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
    hierarchy.worldCacheDirty = true;

    for (Entity::EntityId child : hierarchy.children)
    {
      invalidate_world_caches(child, componentManager);
    }
  }

  inline std::vector<Entity::EntityId> find_world_entities(
      EntityManager &entityManager,
      ComponentManager &componentManager)
  {
    std::vector<Entity::EntityId> worlds;
    for (Entity::EntityId entity : entityManager.getActiveEntities())
    {
      if (!componentManager.hasComponent<WorldComponent>(entity))
      {
        continue;
      }

      // Only include root-level world entities (no parent).
      if (componentManager.hasComponent<TransformHierarchyComponent>(entity))
      {
        const auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
        if (hierarchy.parent.has_value())
        {
          continue;
        }
      }

      worlds.push_back(entity);
    }

    return worlds;
  }

  inline std::optional<Entity::EntityId> find_default_world(
      EntityManager &entityManager,
      ComponentManager &componentManager)
  {
    for (Entity::EntityId entity : entityManager.getActiveEntities())
    {
      if (!componentManager.hasComponent<WorldComponent>(entity))
      {
        continue;
      }

      const auto &world = componentManager.getComponent<WorldComponent>(entity);
      if (world.isDefault)
      {
        return entity;
      }
    }

    return std::nullopt;
  }

  inline std::optional<Entity::EntityId> normalize_default_world(
      EntityManager &entityManager,
      ComponentManager &componentManager)
  {
    std::optional<Entity::EntityId> firstWorld;
    std::optional<Entity::EntityId> defaultWorld;

    for (Entity::EntityId entity : entityManager.getActiveEntities())
    {
      if (!componentManager.hasComponent<WorldComponent>(entity))
      {
        continue;
      }

      if (!firstWorld.has_value())
      {
        firstWorld = entity;
      }

      auto &world = componentManager.getComponent<WorldComponent>(entity);
      if (!world.isDefault)
      {
        continue;
      }

      if (!defaultWorld.has_value())
      {
        defaultWorld = entity;
      }
      else
      {
        world.isDefault = false;
      }
    }

    if (!defaultWorld.has_value() && firstWorld.has_value())
    {
      componentManager.getComponent<WorldComponent>(*firstWorld).isDefault = true;
      defaultWorld = firstWorld;
    }

    return defaultWorld;
  }

  inline void set_default_world(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      Entity::EntityId worldEntity)
  {
    for (Entity::EntityId entity : entityManager.getActiveEntities())
    {
      if (!componentManager.hasComponent<WorldComponent>(entity))
      {
        continue;
      }

      auto &world = componentManager.getComponent<WorldComponent>(entity);
      world.isDefault = (entity == worldEntity);
    }
  }
}

#endif
