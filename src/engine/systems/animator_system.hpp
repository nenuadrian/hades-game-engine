#ifndef HADES_ENGINE_SYSTEMS_ANIMATOR_SYSTEM_HPP
#define HADES_ENGINE_SYSTEMS_ANIMATOR_SYSTEM_HPP

#include <unordered_map>
#include <unordered_set>

#include "../core/ecs/entity.hpp"
#include "../core/ecs/system.hpp"

namespace hades
{
  /// Advances every entity that carries a ModelComponent and an
  /// AnimatorComponent: binds the animator graph, applies authored parameter
  /// overrides once at start, evaluates the pose, and republishes any
  /// animation events onto the event bus as AnimationEvent.
  ///
  /// Poses land in AnimationRuntime rather than in the component, and the
  /// scene renderer picks them up from there.
  class AnimatorSystem : public System
  {
  public:
    void update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager) override;
    void update(float deltaTime, SystemContext &context) override;

    /// Forget which entities have been started, so the next play run applies
    /// authored parameter overrides again.
    void reset();

  private:
    /// Component values already pushed into the animator instance. Speed and
    /// the playing flag are shared with scripts (hades::Animation::setSpeed,
    /// setPlaying), so they are pushed when the *authored* value changes
    /// rather than every frame: that keeps an inspector edit live without
    /// stamping over what a script decided on the frame before.
    struct AuthoredPlayback
    {
      float speed = 1.0f;
      bool playing = true;
    };

    void run(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager,
             EventBus *eventBus);

    std::unordered_set<Entity::EntityId> started_;
    std::unordered_map<Entity::EntityId, AuthoredPlayback> authored_;
  };
}

#endif
