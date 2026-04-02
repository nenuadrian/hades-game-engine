#ifndef TRANSFORM_HIERARCHY_COMPONENT_H
#define TRANSFORM_HIERARCHY_COMPONENT_H

#include <optional>
#include <vector>
#include "../core/ecs/entity.hpp"

namespace hades
{
  class TransformHierarchyComponent
  {
  public:
    std::optional<Entity::EntityId> parent; // The parent entity, if it exists
    std::vector<Entity::EntityId> children; // List of child entities

    void addChild(Entity::EntityId child);
    void removeChild(Entity::EntityId child);
    void setParent(Entity::EntityId newParent);
    void clearParent();
    bool hasParent() const;
  };
}

#endif
