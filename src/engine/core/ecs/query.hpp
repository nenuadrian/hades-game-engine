#ifndef HADES_ENGINE_CORE_ECS_QUERY_HPP
#define HADES_ENGINE_CORE_ECS_QUERY_HPP

#include <bitset>
#include <optional>
#include <vector>

#include "constants.h"
#include "entity.hpp"
#include "entity_manager.hpp"
#include "type_id.hpp"

namespace hades
{
  class ComponentManager;

  /// Build a bitset mask representing a set of component types.
  template <typename... Ts>
  std::bitset<MAX_COMPONENTS> component_mask()
  {
    std::bitset<MAX_COMPONENTS> mask;
    ((mask.set(ComponentTypeId::get<Ts>())), ...);
    return mask;
  }

  /// Return all entity IDs whose component signature includes all of Ts...
  template <typename... Ts>
  std::vector<Entity::EntityId> query(EntityManager &entityManager)
  {
    const auto required = component_mask<Ts...>();
    std::vector<Entity::EntityId> result;

    for (Entity::EntityId entity : entityManager.getActiveEntities())
    {
      const auto &signature = entityManager.getComponentSignature(entity);
      if ((signature & required) == required)
      {
        result.push_back(entity);
      }
    }

    return result;
  }

  // Forward declaration for world filtering - implemented in query_world.hpp
  // to avoid circular dependency with world_utils.hpp.
  bool entity_belongs_to_world(Entity::EntityId entity, Entity::EntityId world, ComponentManager &componentManager);

  /// Return all entity IDs whose component signature includes all of Ts...,
  /// optionally filtered to a specific world.
  template <typename... Ts>
  std::vector<Entity::EntityId> query(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::optional<Entity::EntityId> worldFilter)
  {
    if (!worldFilter.has_value())
    {
      return query<Ts...>(entityManager);
    }

    const auto required = component_mask<Ts...>();
    std::vector<Entity::EntityId> result;

    for (Entity::EntityId entity : entityManager.getActiveEntities())
    {
      const auto &signature = entityManager.getComponentSignature(entity);
      if ((signature & required) != required)
      {
        continue;
      }

      if (!entity_belongs_to_world(entity, *worldFilter, componentManager))
      {
        continue;
      }

      result.push_back(entity);
    }

    return result;
  }
}

#endif
