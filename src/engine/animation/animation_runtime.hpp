#ifndef HADES_ENGINE_ANIMATION_ANIMATION_RUNTIME_HPP
#define HADES_ENGINE_ANIMATION_ANIMATION_RUNTIME_HPP

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../core/ecs/entity.hpp"
#include "../rendering/math3d.hpp"
#include "animation_types.hpp"
#include "animator_instance.hpp"

namespace hades
{
  /// Process-wide owner of per-entity animation state.
  ///
  /// Animator state deliberately does NOT live in the component: it holds
  /// vectors that would be copied on every play-mode snapshot, and scripts
  /// need to reach it without a ComponentManager. AnimatorComponent stays a
  /// small serialisable description; this holds the running players.
  ///
  /// It also carries the editor's pose preview: while the animation editor
  /// scrubs a clip, it publishes the resulting palette here and the scene
  /// renderer picks it up, so the viewport shows the authored pose without
  /// the entity needing any animator at all.
  class AnimationRuntime
  {
  public:
    static AnimationRuntime &instance();

    // ---- Instances -------------------------------------------------------

    /// The instance for `entity`, created on first use.
    AnimatorInstance &instanceFor(Entity::EntityId entity);
    /// The instance for `entity`, or nullptr when it has none.
    AnimatorInstance *find(Entity::EntityId entity);
    const AnimatorInstance *find(Entity::EntityId entity) const;
    bool has(Entity::EntityId entity) const;

    void remove(Entity::EntityId entity);
    /// Drop every instance and preview. Called when play mode stops and when
    /// the workspace changes.
    void clear();
    /// Drop playback state but keep the instances, so entities resume from
    /// their default state on the next play.
    void reset_all();

    // ---- Editor pose preview --------------------------------------------

    /// Publish a palette for `entity` that overrides whatever the animation
    /// systems produce, until cleared. Used by the animation editor.
    void set_preview_palette(Entity::EntityId entity, std::vector<math::Mat4> palette);
    void clear_preview(Entity::EntityId entity);
    void clear_all_previews();
    bool has_preview(Entity::EntityId entity) const;

    /// Palette the renderer should draw `entity` with: the editor preview if
    /// one is published, otherwise the animator's own output. Returns false
    /// when neither exists and the caller should fall back to the legacy
    /// AnimationComponent path or the bind pose.
    bool palette_for(Entity::EntityId entity, std::vector<math::Mat4> &out) const;

    // ---- Events ----------------------------------------------------------

    std::vector<AnimationEventFired> drain_events(Entity::EntityId entity);
    bool event_fired(Entity::EntityId entity, const std::string &name) const;

  private:
    AnimationRuntime() = default;

    mutable std::mutex mutex_;
    std::unordered_map<Entity::EntityId, AnimatorInstance> instances_;
    std::unordered_map<Entity::EntityId, std::vector<math::Mat4>> previews_;
  };
}

#endif
