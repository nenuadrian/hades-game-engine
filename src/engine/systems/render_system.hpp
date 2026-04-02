#ifndef RENDER_SYSTEM_H
#define RENDER_SYSTEM_H

#include "../core/ecs/system.hpp"

namespace hades
{
  class RenderSystem : public System
  {
  public:
    void update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager) override;
  };
}
#endif
