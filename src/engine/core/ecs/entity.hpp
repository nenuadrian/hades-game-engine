#ifndef ENTITY_H
#define ENTITY_H

#include <cstdint>
#include <limits>

class Entity
{
public:
  using EntityId = uint32_t;

  static const EntityId INVALID = std::numeric_limits<EntityId>::max();

private:
  EntityId id;

public:
  Entity(EntityId id) : id(id) {}

  EntityId getId() const { return id; }
};

#endif
