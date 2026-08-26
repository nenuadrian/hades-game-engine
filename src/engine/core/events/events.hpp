#ifndef HADES_ENGINE_CORE_EVENTS_EVENTS_HPP
#define HADES_ENGINE_CORE_EVENTS_EVENTS_HPP

#include <cstdint>
#include <string>

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

  /// An animation event marker crossed by the play head this frame, raised by
  /// AnimatorSystem. Gameplay code can either subscribe to these or poll
  /// hades::Animation::eventFired().
  struct AnimationEvent
  {
    Entity::EntityId entity;
    std::string name;
    std::string stringValue;
    float floatValue;
    std::string clip;
    float time;
  };
}

#endif
