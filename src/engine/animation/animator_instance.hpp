#ifndef HADES_ENGINE_ANIMATION_ANIMATOR_INSTANCE_HPP
#define HADES_ENGINE_ANIMATION_ANIMATOR_INSTANCE_HPP

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "../rendering/math3d.hpp"
#include "animation_types.hpp"
#include "animator_graph.hpp"
#include "skeleton.hpp"

namespace hades
{
  class AnimationClipAsset;
  class AnimationClipCache;
  class ModelAsset;

  /// What one layer is currently playing. A layer either runs a graph state
  /// or an ad-hoc clip pushed by `play_clip` — both are modelled as a
  /// "source", so a crossfade between them needs no special case.
  struct AnimatorSource
  {
    /// Graph state index, or -1 for an ad-hoc clip.
    int state = -1;
    /// Clip reference for an ad-hoc source (state == -1).
    std::string clip;
    float time = 0.0f;
    float speed = 1.0f;
    bool looping = true;
    /// Normalised time of the previous evaluation, for event windows.
    float previousTime = 0.0f;
    bool finished = false;
  };

  /// Runtime state of one layer: the active source, plus the source being
  /// crossfaded out.
  struct AnimatorLayerRuntime
  {
    AnimatorSource current;
    AnimatorSource previous;
    bool transitioning = false;
    float transitionElapsed = 0.0f;
    float transitionDuration = 0.0f;
    /// Index of the transition currently running, or -1 for a scripted
    /// crossfade. Only used to honour `canInterrupt`.
    int transitionIndex = -1;
    BoneMask mask;
    bool maskBuilt = false;
    float weight = 1.0f;
    /// Cleared by `stop(layer)`: the layer holds its pose while the rest of
    /// the animator keeps running. The global `playing()` flag freezes every
    /// layer regardless.
    bool playing = true;
  };

  /// Per-entity animation player.
  ///
  /// Owns no assets: the model and the clip cache are handed to `update`
  /// every frame, because ModelAssetCache can drop and reload an asset at any
  /// workspace switch. The instance keeps only playback state.
  class AnimatorInstance
  {
  public:
    // ---- Configuration ---------------------------------------------------

    /// Bind an animator graph by reference (file stem or path). Passing an
    /// empty string drops the graph and leaves the instance in clip mode.
    /// Re-binding the same reference is a no-op, so this is safe per frame.
    void set_graph_reference(const std::string &reference);
    const std::string &graph_reference() const { return graphReference_; }

    void set_playing(bool playing) { playing_ = playing; }
    bool playing() const { return playing_; }

    /// Global speed multiplier applied on top of per-state speed.
    void set_speed(float speed) { speed_ = speed; }
    float speed() const { return speed_; }

    /// Crossfade length `play_clip` falls back to when the caller passes a
    /// negative `blendSeconds`. AnimatorSystem pushes AnimatorComponent's
    /// authored "Default Blend" here every frame; until it has (a script's
    /// `onStart` runs before the system has ever seen the entity) this is
    /// the engine's own 0.15 s. Negatives are clamped away so the fallback
    /// can never itself be a sentinel.
    void set_default_blend(float seconds) { defaultBlend_ = seconds < 0.0f ? 0.0f : seconds; }
    float default_blend() const { return defaultBlend_; }

    // ---- Playback --------------------------------------------------------

    /// Crossfade `layer` to an ad-hoc clip over `blendSeconds`. This is what
    /// `hades::Animation::play` and the Blueprint "Play Animation" node call.
    /// A negative `blendSeconds` means "use `default_blend()`" — 0 still
    /// snaps, which is a legal authored value and therefore cannot be the
    /// sentinel.
    void play_clip(const std::string &clipReference, float blendSeconds, bool looping, int layer = 0);

    /// Crossfade `layer` to a named graph state. Returns false when no graph
    /// is bound or the state does not exist.
    bool goto_state(const std::string &stateName, float blendSeconds, int layer = 0);

    /// Stop playback on one layer (or all layers when `layer` is negative)
    /// and hold the current pose.
    void stop(int layer = -1);

    /// Restart the active source from its first frame.
    void restart(int layer = 0);

    void seek(float seconds, int layer = 0);

    // ---- Parameters ------------------------------------------------------

    void set_float(const std::string &name, float value);
    void set_int(const std::string &name, int value);
    void set_bool(const std::string &name, bool value);
    /// Latch a trigger. It stays set until a transition consumes it or
    /// `reset_trigger` clears it.
    void set_trigger(const std::string &name);
    void reset_trigger(const std::string &name);

    float get_float(const std::string &name) const;
    int get_int(const std::string &name) const;
    bool get_bool(const std::string &name) const;

    // ---- Queries ---------------------------------------------------------

    std::string current_state(int layer = 0) const;
    std::string current_clip(int layer = 0) const;
    /// Time within the active source, normalised by its clip duration.
    float normalized_time(int layer = 0) const;
    float time_seconds(int layer = 0) const;
    bool is_transitioning(int layer = 0) const;
    std::size_t layer_count() const { return layers_.size(); }

    // ---- Evaluation ------------------------------------------------------

    /// Advance by `deltaTime` and rebuild the pose and bone palette.
    /// Safe to call with a null-clip or empty skeleton: the palette then
    /// falls back to the model's bind pose.
    void update(float deltaTime, const ModelAsset &asset, AnimationClipCache &clips);

    /// Evaluate at the current time without advancing. Used by the editor so
    /// scrubbing shows the graph's pose.
    void evaluate(const ModelAsset &asset, AnimationClipCache &clips);

    const Pose &pose() const { return pose_; }
    /// Skinning palette produced by the last update/evaluate. Empty until the
    /// instance has evaluated at least once.
    const std::vector<math::Mat4> &palette() const { return palette_; }

    // ---- Events ----------------------------------------------------------

    /// Events that fired during the last update, oldest first.
    const std::vector<AnimationEventFired> &pending_events() const { return events_; }
    std::vector<AnimationEventFired> drain_events();
    /// True when an event of this name fired during the last update.
    ///
    /// Deliberately non-destructive: several consumers legitimately watch the
    /// same event in one frame (a script playing a footstep sound and a
    /// Blueprint node spawning dust), and a destructive poll would let
    /// whichever ran first silently starve the rest. AnimatorSystem clears
    /// the buffer once per frame, which is what keeps this "once per firing".
    bool event_fired(const std::string &name) const;
    void clear_events();

    /// Forget playback state but keep configuration. Called when play mode
    /// stops so the next run starts clean.
    void reset();

  private:
    struct ParamValue
    {
      AnimParamType type = AnimParamType::Float;
      float floatValue = 0.0f;
      int intValue = 0;
      bool boolValue = false;
    };

    void ensure_layers(const AnimatorGraph *graph);
    void sync_parameters(const AnimatorGraph *graph);
    bool condition_holds(const AnimCondition &condition) const;
    /// `sourceNormalizedTime` and `sourceWrapped` describe the layer's current
    /// source this frame. They are passed in because deriving them needs the
    /// clip cache, which the caller already holds.
    bool transition_ready(const AnimTransition &transition, const AnimatorLayerRuntime &runtime,
                          float sourceNormalizedTime, bool sourceWrapped) const;
    void consume_triggers(const AnimTransition &transition);

    /// Rebuild `skeleton_` (and the cached rest pose) from `asset` when the
    /// asset it was derived from has changed underneath us.
    void refresh_skeleton(const ModelAsset &asset);
    /// Pose every layer, combine them and rebuild the palette. `collectEvents`
    /// is false while paused and on the editor's scrub path.
    void evaluate_pose(const ModelAsset &asset, AnimationClipCache &clips,
                       const AnimatorGraph *graph, bool collectEvents);
    void advance_layer(std::size_t layerIndex, const AnimatorGraph *graph,
                       AnimationClipCache &clips, float deltaTime);
    void advance_source(AnimatorSource &source, const AnimatorGraph *graph, std::size_t layerIndex,
                        AnimationClipCache &clips, float deltaTime);
    void select_transition(std::size_t layerIndex, const AnimatorGraph *graph, AnimationClipCache &clips);
    void begin_transition(AnimatorLayerRuntime &runtime, const AnimLayer &layer, int stateIndex,
                          float blendSeconds, int transitionIndex);
    /// State a source is playing, or nullptr for an ad-hoc clip. State indices
    /// are per layer, which is why `layerIndex` is part of the lookup.
    const AnimState *state_for(const AnimatorGraph *graph, std::size_t layerIndex, int stateIndex) const;
    /// Clip reference a source resolves to, or nullptr when it resolves to
    /// none. For a blend tree this is the dominant entry. The pointer aliases
    /// the source or the graph, so it dies with them.
    const std::string *clip_reference_for(const AnimatorSource &source, const AnimatorGraph *graph,
                                          std::size_t layerIndex) const;
    /// Evaluate one source into `out`, starting from the skeleton rest pose,
    /// and collect the events it crossed. Returns false when nothing resolved
    /// (no clip bound, or the clip failed to load), leaving `out` at rest.
    bool evaluate_source(const AnimatorSource &source, const AnimatorGraph *graph, std::size_t layerIndex,
                         const Skeleton &skeleton, AnimationClipCache &clips,
                         Pose &out, bool collectEvents);
    /// `reversed` is set when the source's effective rate is negative. It only
    /// affects the event window: a head running backwards traverses
    /// [time, previousTime), which is the mirror image of the forward window.
    bool sample_clip_into(const std::string &reference, float time, float previousTime, bool looping,
                          const Skeleton &skeleton, AnimationClipCache &clips,
                          Pose &out, bool collectEvents, bool reversed);
    void collect_events(const AnimationClipAsset &clip, const std::string &reference,
                        float fromTime, float toTime, bool looped);
    /// Fill `out` with the pose an additive layer measures `source`'s delta
    /// against: the clip's own reference frame, or the rest pose. The
    /// destination is explicit because a crossfading additive layer needs
    /// both halves' references at once.
    void build_additive_reference(const AnimatorSource &source, const AnimatorGraph *graph,
                                  std::size_t layerIndex, AnimationClipCache &clips, Pose &out);
    float source_duration(const AnimatorSource &source, const AnimatorGraph *graph, std::size_t layerIndex,
                          AnimationClipCache &clips) const;
    float source_normalized(const AnimatorSource &source, const AnimatorGraph *graph, std::size_t layerIndex,
                            AnimationClipCache &clips) const;
    /// Layer index clamped into range. Callers must check `layers_` first.
    std::size_t clamp_layer(int layer) const;

    std::string graphReference_;
    bool playing_ = true;
    float speed_ = 1.0f;
    float defaultBlend_ = 0.15f;
    /// Set when the bound graph changed: layers still holding a state index
    /// from the old graph must go back to its default state.
    bool layersDirty_ = true;
    /// Graph object the layer masks were built from. The editor saves a graph
    /// while it plays, and the cache hands back a new object for it, so the
    /// pointer is the cheap "my cached derivations are stale" signal.
    const AnimatorGraph *graphSource_ = nullptr;

    std::vector<AnimatorLayerRuntime> layers_;
    std::unordered_map<std::string, ParamValue> parameters_;

    Pose pose_;
    Pose scratchA_;
    Pose scratchB_;
    Pose scratchReference_;
    /// Second reference buffer: an additive layer mid-crossfade measures the
    /// blended pose against the blend of BOTH halves' reference frames.
    Pose scratchReferenceB_;
    /// Rest pose of `skeleton_`, cached so seeding a scratch pose is a copy
    /// into an already-sized buffer rather than a fresh allocation.
    Pose restPose_;
    /// Extra accumulator for blend-tree children.
    Pose blendScratch_;
    std::vector<math::Mat4> globals_;
    std::vector<math::Mat4> palette_;
    Skeleton skeleton_;
    /// Cheap identity for "the skeleton I built `skeleton_` from": the node
    /// count and the address of the asset it came from.
    const ModelAsset *skeletonSource_ = nullptr;
    std::size_t skeletonNodeCount_ = 0;

    std::vector<AnimationEventFired> events_;
    /// Reused by collect_events so event gathering allocates nothing.
    std::vector<const AnimationEventKey *> eventScratch_;
  };
}

#endif
