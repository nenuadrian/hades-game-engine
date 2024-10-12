#ifndef TRANSFORM_HIERARCHY_COMPONENT_H
#define TRANSFORM_HIERARCHY_COMPONENT_H

#include <vector>
#include <optional>
#include "entity.hpp"

class TransformHierarchyComponent
{
public:
  std::optional<Entity::Id> parent; // The parent entity, if it exists
  std::vector<Entity::Id> children; // List of child entities

  void addChild(Entity::Id child)
  {
    children.push_back(child);
  }

  void removeChild(Entity::Id child)
  {
    children.erase(std::remove(children.begin(), children.end(), child), children.end());
  }

  void setParent(Entity::Id newParent)
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

#endif
