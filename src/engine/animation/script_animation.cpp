#include "script_animation.hpp"

#include "animation_runtime.hpp"
#include "animator_instance.hpp"

namespace hades
{
  namespace
  {
    // Mutating entry points create the instance on demand. A script's onStart
    // runs before AnimatorSystem has ever seen the entity, so requiring an
    // existing instance would silently drop the first play/setFloat of every
    // scripted actor. The instance the system later picks up is this one.
    AnimatorInstance &writable(Entity::EntityId entity)
    {
      return AnimationRuntime::instance().instanceFor(entity);
    }

    // Read-only entry points never create: a getter must not leave state
    // behind for an entity that has no animator at all.
    AnimatorInstance *existing(Entity::EntityId entity)
    {
      return AnimationRuntime::instance().find(entity);
    }
  }

  void Animation::play(Entity::EntityId entity, const std::string &clip,
                       float blendSeconds, bool looping, int layer)
  {
    if (clip.empty())
    {
      return;
    }
    writable(entity).play_clip(clip, blendSeconds, looping, layer);
  }

  void Animation::playOnce(Entity::EntityId entity, const std::string &clip,
                           float blendSeconds, int layer)
  {
    play(entity, clip, blendSeconds, false, layer);
  }

  void Animation::stop(Entity::EntityId entity, int layer)
  {
    writable(entity).stop(layer);
  }

  void Animation::restart(Entity::EntityId entity, int layer)
  {
    writable(entity).restart(layer);
  }

  void Animation::seek(Entity::EntityId entity, float seconds, int layer)
  {
    writable(entity).seek(seconds, layer);
  }

  void Animation::setPlaying(Entity::EntityId entity, bool playing)
  {
    writable(entity).set_playing(playing);
  }

  bool Animation::isPlaying(Entity::EntityId entity)
  {
    const AnimatorInstance *instance = existing(entity);
    return instance != nullptr && instance->playing();
  }

  void Animation::setSpeed(Entity::EntityId entity, float speed)
  {
    writable(entity).set_speed(speed);
  }

  float Animation::speed(Entity::EntityId entity)
  {
    const AnimatorInstance *instance = existing(entity);
    return instance == nullptr ? 0.0f : instance->speed();
  }

  bool Animation::gotoState(Entity::EntityId entity, const std::string &state, float blendSeconds, int layer)
  {
    if (state.empty())
    {
      return false;
    }
    return writable(entity).goto_state(state, blendSeconds, layer);
  }

  std::string Animation::currentState(Entity::EntityId entity, int layer)
  {
    const AnimatorInstance *instance = existing(entity);
    return instance == nullptr ? std::string() : instance->current_state(layer);
  }

  std::string Animation::currentClip(Entity::EntityId entity, int layer)
  {
    const AnimatorInstance *instance = existing(entity);
    return instance == nullptr ? std::string() : instance->current_clip(layer);
  }

  float Animation::normalizedTime(Entity::EntityId entity, int layer)
  {
    const AnimatorInstance *instance = existing(entity);
    return instance == nullptr ? 0.0f : instance->normalized_time(layer);
  }

  bool Animation::isTransitioning(Entity::EntityId entity, int layer)
  {
    const AnimatorInstance *instance = existing(entity);
    return instance != nullptr && instance->is_transitioning(layer);
  }

  void Animation::setFloat(Entity::EntityId entity, const std::string &name, float value)
  {
    if (name.empty())
    {
      return;
    }
    writable(entity).set_float(name, value);
  }

  void Animation::setInt(Entity::EntityId entity, const std::string &name, int value)
  {
    if (name.empty())
    {
      return;
    }
    writable(entity).set_int(name, value);
  }

  void Animation::setBool(Entity::EntityId entity, const std::string &name, bool value)
  {
    if (name.empty())
    {
      return;
    }
    writable(entity).set_bool(name, value);
  }

  void Animation::setTrigger(Entity::EntityId entity, const std::string &name)
  {
    if (name.empty())
    {
      return;
    }
    writable(entity).set_trigger(name);
  }

  void Animation::resetTrigger(Entity::EntityId entity, const std::string &name)
  {
    if (name.empty())
    {
      return;
    }
    writable(entity).reset_trigger(name);
  }

  float Animation::getFloat(Entity::EntityId entity, const std::string &name)
  {
    const AnimatorInstance *instance = existing(entity);
    return instance == nullptr ? 0.0f : instance->get_float(name);
  }

  int Animation::getInt(Entity::EntityId entity, const std::string &name)
  {
    const AnimatorInstance *instance = existing(entity);
    return instance == nullptr ? 0 : instance->get_int(name);
  }

  bool Animation::getBool(Entity::EntityId entity, const std::string &name)
  {
    const AnimatorInstance *instance = existing(entity);
    return instance != nullptr && instance->get_bool(name);
  }

  bool Animation::eventFired(Entity::EntityId entity, const std::string &name)
  {
    if (name.empty())
    {
      return false;
    }
    return AnimationRuntime::instance().event_fired(entity, name);
  }

  std::vector<AnimationEventFired> Animation::drainEvents(Entity::EntityId entity)
  {
    return AnimationRuntime::instance().drain_events(entity);
  }
}
