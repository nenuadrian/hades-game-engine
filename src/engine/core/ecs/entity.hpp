#ifndef ENTITY_H
#define ENTITY_H

#include <cstdint>
#include <limits>

class Entity
{
public:
  using Id = uint32_t;
  static const Id INVALID = std::numeric_limits<Id>::max();

private:
  Id id;
  // Other properties (if necessary)

public:
  Entity(Id id) : id(id) {}

  Id getId() const { return id; }
};

#endif
