#ifndef ENTITY_H
#define ENTITY_H

#include <cstdint>
#include <limits>

namespace hades
{
  class Entity
  {
  public:
    using EntityId = uint32_t;

    static constexpr EntityId INVALID = (std::numeric_limits<EntityId>::max)();

  private:
    EntityId id;

  public:
    explicit Entity(EntityId id);

    EntityId getId() const;
  };
}
#endif
