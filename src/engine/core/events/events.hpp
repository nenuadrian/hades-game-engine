#ifndef HADES_ENGINE_CORE_EVENTS_EVENTS_HPP
#define HADES_ENGINE_CORE_EVENTS_EVENTS_HPP

#include <cstdint>

#include "../ecs/entity.hpp"

namespace hades
{
  struct EntityCreatedEvent
  {
    Entity::EntityId entity;
  };

  struct EntityDestroyedEvent
  {
    Entity::EntityId entity;
  };

  struct CollisionBeginEvent
  {
    Entity::EntityId entityA;
    Entity::EntityId entityB;
    std::uint32_t bodyIdA;
    std::uint32_t bodyIdB;
  };

  struct CollisionEndEvent
  {
    Entity::EntityId entityA;
    Entity::EntityId entityB;
  };
}

#endif
