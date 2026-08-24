#ifndef HADES_ENGINE_SYSTEMS_ANIMATION_SYSTEM_HPP
#define HADES_ENGINE_SYSTEMS_ANIMATION_SYSTEM_HPP

#include "../core/ecs/system.hpp"

namespace hades
{
  /// Advances AnimationComponent playback time each frame for entities that
  /// also carry a ModelComponent. Looping wraps into the clip; one-shot
  /// clips clamp at the end and stop.
  class AnimationSystem : public System
  {
  public:
    void update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager) override;
  };
}

#endif
