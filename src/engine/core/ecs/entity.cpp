#include "entity.hpp"

namespace hades
{
  Entity::Entity(EntityId id) : id(id) {}

  Entity::EntityId Entity::getId() const
  {
    return id;
  }
}
