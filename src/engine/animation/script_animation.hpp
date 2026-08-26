#ifndef HADES_ENGINE_ANIMATION_SCRIPT_ANIMATION_HPP
#define HADES_ENGINE_ANIMATION_SCRIPT_ANIMATION_HPP

// Scripting-friendly animation facade.
//
// Use this from HadesScript subclasses to drive skeletal animation without
// touching AnimatorComponent or the animator internals:
//
//   void onStart(ScriptContext &ctx) override
//   {
//     hades::Animation::play(ctx.entityId, "idle");
//   }
//
//   void onUpdate(ScriptContext &ctx, float dt) override
//   {
//     hades::Animation::setFloat(ctx.entityId, "speed", currentSpeed);
//     if (hades::Animation::eventFired(ctx.entityId, "footstep"))
//     {
//       hades::Audio::playSfxr(hades::Audio::SfxrBlip);
//     }
//   }
//
// Every call is a no-op (or returns a default) for an entity that has no
// model, so scripts do not have to guard.

#include <string>
#include <vector>

#include "../core/ecs/entity.hpp"
#include "animation_types.hpp"

namespace hades
{
  class Animation
  {
  public:
    /// Passed as `blendSeconds` to say "no opinion": the crossfade length is
    /// then the AnimatorComponent's authored "Default Blend" (0.15 s until an
    /// entity's AnimatorSystem pass has pushed it, which is what a call from
    /// `onStart` sees). It has to be negative rather than 0, because 0 is a
    /// legal authored value meaning "snap".
    static constexpr float kComponentBlend = -1.0f;

    // ---- Clip playback ---------------------------------------------------

    /// Crossfade to `clip` (a name under `.hades/animations/`, or a path).
    /// `blendSeconds` of 0 snaps; the default defers to the Animator
    /// component's "Default Blend".
    static void play(Entity::EntityId entity, const std::string &clip,
                     float blendSeconds = kComponentBlend, bool looping = true, int layer = 0);

    /// Play once and hold the last frame.
    static void playOnce(Entity::EntityId entity, const std::string &clip,
                         float blendSeconds = kComponentBlend, int layer = 0);

    static void stop(Entity::EntityId entity, int layer = -1);
    static void restart(Entity::EntityId entity, int layer = 0);
    static void seek(Entity::EntityId entity, float seconds, int layer = 0);

    static void setPlaying(Entity::EntityId entity, bool playing);
    static bool isPlaying(Entity::EntityId entity);
    static void setSpeed(Entity::EntityId entity, float speed);
    static float speed(Entity::EntityId entity);

    // ---- Animator graph --------------------------------------------------

    /// Crossfade to a named state of the entity's animator graph.
    static bool gotoState(Entity::EntityId entity, const std::string &state, float blendSeconds = 0.2f, int layer = 0);
    static std::string currentState(Entity::EntityId entity, int layer = 0);
    static std::string currentClip(Entity::EntityId entity, int layer = 0);
    /// Playback position of the active clip in [0,1].
    static float normalizedTime(Entity::EntityId entity, int layer = 0);
    static bool isTransitioning(Entity::EntityId entity, int layer = 0);

    // ---- Parameters ------------------------------------------------------

    static void setFloat(Entity::EntityId entity, const std::string &name, float value);
    static void setInt(Entity::EntityId entity, const std::string &name, int value);
    static void setBool(Entity::EntityId entity, const std::string &name, bool value);
    static void setTrigger(Entity::EntityId entity, const std::string &name);
    static void resetTrigger(Entity::EntityId entity, const std::string &name);

    static float getFloat(Entity::EntityId entity, const std::string &name);
    static int getInt(Entity::EntityId entity, const std::string &name);
    static bool getBool(Entity::EntityId entity, const std::string &name);

    // ---- Events ----------------------------------------------------------

    /// True once per firing of `name`. Poll this in onUpdate.
    static bool eventFired(Entity::EntityId entity, const std::string &name);
    /// Every event that fired for `entity` this frame, consumed by the call.
    static std::vector<AnimationEventFired> drainEvents(Entity::EntityId entity);
  };
}

#endif
