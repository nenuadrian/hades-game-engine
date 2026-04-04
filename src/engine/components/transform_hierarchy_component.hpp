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

    /// Cached world entity for this entity. Avoids repeated parent-chain walks.
    mutable std::optional<Entity::EntityId> cachedWorld;
    mutable bool worldCacheDirty = true;

    /// Set to true when position/rotation/scale changes (e.g. by physics sync).
    /// Consumers can check and clear this flag as needed.
    bool transformDirty = true;

    void addChild(Entity::EntityId child);
    void removeChild(Entity::EntityId child);
    void setParent(Entity::EntityId newParent);
    void clearParent();
    bool hasParent() const;
  };
}

#endif
