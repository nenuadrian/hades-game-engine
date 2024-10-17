#ifndef TRANSFORM_HIERARCHY_COMPONENT_H
#define TRANSFORM_HIERARCHY_COMPONENT_H

#include <vector>
#include <optional>
#include "../core/ecs/entity.hpp"

namespace hades
{
  class TransformHierarchyComponent
  {
  public:
    std::optional<Entity::EntityId> parent; // The parent entity, if it exists
    std::vector<Entity::EntityId> children; // List of child entities

    void addChild(Entity::EntityId child)
    {
      children.push_back(child);
    }

    void removeChild(Entity::EntityId child)
    {
      children.erase(std::remove(children.begin(), children.end(), child), children.end());
    }

    void setParent(Entity::EntityId newParent)
    {
      parent = newParent;
    }

    void clearParent()
    {
      parent.reset();
    }

    bool hasParent() const
    {
      return parent.has_value();
    }
  };
}

#endif
