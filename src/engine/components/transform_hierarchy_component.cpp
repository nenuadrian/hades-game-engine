#include "transform_hierarchy_component.hpp"

#include <algorithm>

namespace hades
{
  void TransformHierarchyComponent::addChild(Entity::EntityId child)
  {
    children.push_back(child);
  }

  void TransformHierarchyComponent::removeChild(Entity::EntityId child)
  {
    children.erase(std::remove(children.begin(), children.end(), child), children.end());
  }

  void TransformHierarchyComponent::setParent(Entity::EntityId newParent)
  {
    parent = newParent;
  }

  void TransformHierarchyComponent::clearParent()
  {
    parent.reset();
  }

  bool TransformHierarchyComponent::hasParent() const
  {
    return parent.has_value();
  }
}
