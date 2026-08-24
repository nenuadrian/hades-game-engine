#ifndef HADES_ENGINE_CORE_ECS_HIERARCHY_UTILS_HPP
#define HADES_ENGINE_CORE_ECS_HIERARCHY_UTILS_HPP

#include <algorithm>
#include <iterator>
#include <optional>

#include "../../components/transform_hierarchy_component.hpp"
#include "component_manager.hpp"
#include "entity.hpp"
#include "world_utils.hpp"

namespace hades
{
  /// Insertion index meaning "append to the end of the parent's child list".
  inline constexpr int HIERARCHY_APPEND_INDEX = -1;

  /// True when `entity` is `ancestor` itself or sits anywhere beneath it.
  inline bool is_same_or_descendant_of(
      Entity::EntityId entity,
      Entity::EntityId ancestor,
      ComponentManager &componentManager)
  {
    std::optional<Entity::EntityId> current = entity;
    while (current.has_value())
    {
      if (*current == ancestor)
      {
        return true;
      }

      if (!componentManager.hasComponent<TransformHierarchyComponent>(*current))
      {
        return false;
      }

      current = componentManager.getComponent<TransformHierarchyComponent>(*current).parent;
    }

    return false;
  }

  /// Index of `entity` inside its parent's child list, if it has one.
  inline std::optional<int> child_index_in_parent(
      Entity::EntityId entity,
      ComponentManager &componentManager)
  {
    if (!componentManager.hasComponent<TransformHierarchyComponent>(entity))
    {
      return std::nullopt;
    }

    const auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
    if (!hierarchy.parent.has_value() ||
        !componentManager.hasComponent<TransformHierarchyComponent>(*hierarchy.parent))
    {
      return std::nullopt;
    }

    const auto &parentHierarchy = componentManager.getComponent<TransformHierarchyComponent>(*hierarchy.parent);
    const auto it = std::find(parentHierarchy.children.begin(), parentHierarchy.children.end(), entity);
    if (it == parentHierarchy.children.end())
    {
      return std::nullopt;
    }

    return static_cast<int>(std::distance(parentHierarchy.children.begin(), it));
  }

  /// True when `entity` can legally become a child of `newParent`. Rejects
  /// self-parenting and drops onto the entity's own descendants, which would
  /// otherwise orphan the subtree into a cycle.
  inline bool can_reparent_entity(
      Entity::EntityId entity,
      Entity::EntityId newParent,
      ComponentManager &componentManager)
  {
    if (entity == newParent)
    {
      return false;
    }

    if (!componentManager.hasComponent<TransformHierarchyComponent>(entity) ||
        !componentManager.hasComponent<TransformHierarchyComponent>(newParent))
    {
      return false;
    }

    return !is_same_or_descendant_of(newParent, entity, componentManager);
  }

  /// Move `entity` under `newParent`, optionally at a specific slot in the
  /// child list. Returns false when the move would be illegal, leaving the
  /// hierarchy untouched.
  inline bool reparent_entity(
      Entity::EntityId entity,
      Entity::EntityId newParent,
      ComponentManager &componentManager,
      int insertIndex = HIERARCHY_APPEND_INDEX)
  {
    if (!can_reparent_entity(entity, newParent, componentManager))
    {
      return false;
    }

    auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
    const std::optional<Entity::EntityId> oldParent = hierarchy.parent;

    if (oldParent.has_value() &&
        componentManager.hasComponent<TransformHierarchyComponent>(*oldParent))
    {
      if (*oldParent == newParent && insertIndex >= 0)
      {
        // Detaching the entity first shifts every later sibling down one slot,
        // so an insertion point past the old slot has to shift with them.
        const auto oldIndex = child_index_in_parent(entity, componentManager);
        if (oldIndex.has_value() && insertIndex > *oldIndex)
        {
          --insertIndex;
        }
      }

      componentManager.getComponent<TransformHierarchyComponent>(*oldParent).removeChild(entity);
    }

    hierarchy.setParent(newParent);

    auto &parentHierarchy = componentManager.getComponent<TransformHierarchyComponent>(newParent);
    if (insertIndex < 0 || insertIndex >= static_cast<int>(parentHierarchy.children.size()))
    {
      parentHierarchy.addChild(entity);
    }
    else
    {
      parentHierarchy.children.insert(parentHierarchy.children.begin() + insertIndex, entity);
    }

    invalidate_world_caches(entity, componentManager);
    return true;
  }
}

#endif
