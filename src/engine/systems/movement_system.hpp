#ifndef MOVEMENT_SYSTEM_h
#define MOVEMENT_SYSTEM_h

#include "../core/ecs/system.hpp"

namespace hades
{
  class MovementSystem : public System
  {
  public:
    void update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager) override;
  };
}

#endif
