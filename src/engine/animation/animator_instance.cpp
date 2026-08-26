#include "animator_instance.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "../assets/model_asset.hpp"
#include "animation_clip.hpp"
#include "animation_clip_cache.hpp"
#include "pose_ops.hpp"

namespace hades
{
  namespace
  {
    /// Upper bound on the events one update may report. A clip with a
    /// pathological number of markers (or a tiny duration crossed many times
    /// in one frame) must not be able to grow the queue without bound.
    constexpr std::size_t kMaxPendingEvents = 64;

    /// Tolerance for float parameter equality and for "this threshold is the
    /// value we are looking at".
    constexpr float kValueEpsilon = 1e-4f;

    float clamp_range(float value, float low, float high)
    {
      return value < low ? low : (value > high ? high : value);
    }

    float clamp01(float value)
    {
      return clamp_range(value, 0.0f, 1.0f);
    }

    /// How far a layer's crossfade has progressed, in [0,1]. A layer that is
    /// not transitioning is fully on its current source.
    float transition_blend(const AnimatorLayerRuntime &runtime)
    {
      if (!runtime.transitioning || runtime.transitionDuration <= 0.0f)
      {
        return 1.0f;
      }
      return clamp01(runtime.transitionElapsed / runtime.transitionDuration);
    }

    /// One clip of a blend tree and the weight it contributes this frame.
    struct BlendContribution
    {
      int entry = -1;
      float weight = 0.0f;
    };

    /// How many entries one evaluation may weight. A 1D tree only ever
    /// brackets two, but gradient-band weighting (see blend_2d) can put a
    /// non-zero weight on several entries at once, and capping the set is
    /// what keeps blend-tree evaluation free of per-frame allocation. Any
    /// authored tree up to this many entries is weighted exactly; a larger
    /// one keeps the heaviest contributors, and the ones it drops are by
    /// construction the lightest. blend_2d rebases the weights it keeps
    /// against the heaviest one it dropped, so even an oversized tree stays
    /// continuous where the membership of the set changes.
    constexpr int kMaxBlendContributions = 16;

    /// The clips a blend tree actually samples this frame, heaviest first.
    struct BlendSet
    {
      BlendContribution items[kMaxBlendContributions];
      int count = 0;

      /// Slot carrying the most weight, or -1 when the set is empty. Events
      /// and `current_clip` follow this entry.
      int dominant() const
      {
        int best = -1;
        for (int i = 0; i < count; ++i)
        {
          if (best < 0 || items[i].weight > items[best].weight)
          {
            best = i;
          }
        }
        return best;
      }
    };

    /// Bracket `value` between the two nearest thresholds. Scanning for the
    /// neighbours gives the same answer as sorting the entries by thresholdX
    /// and walking them, without copying or reordering the authored list.
    BlendSet blend_1d(const AnimState &state, float value)
    {
      BlendSet set;
      const std::size_t count = state.entries.size();
      if (count == 0)
      {
        return set;
      }

      int lower = -1;
      int upper = -1;
      for (std::size_t i = 0; i < count; ++i)
      {
        const float threshold = state.entries[i].thresholdX;
        if (threshold <= value && (lower < 0 || threshold > state.entries[static_cast<std::size_t>(lower)].thresholdX))
        {
          lower = static_cast<int>(i);
        }
        if (threshold >= value && (upper < 0 || threshold < state.entries[static_cast<std::size_t>(upper)].thresholdX))
        {
          upper = static_cast<int>(i);
        }
      }

      // Outside the authored range the nearest end entry plays on its own.
      if (lower < 0 || upper < 0 || lower == upper)
      {
        set.items[0].entry = lower < 0 ? upper : lower;
        set.items[0].weight = 1.0f;
        set.count = 1;
        return set;
      }

      const float low = state.entries[static_cast<std::size_t>(lower)].thresholdX;
      const float high = state.entries[static_cast<std::size_t>(upper)].thresholdX;
      const float span = high - low;
      const float t = span > kValueEpsilon ? clamp01((value - low) / span) : 0.0f;

      set.items[0].entry = lower;
      set.items[0].weight = 1.0f - t;
      set.items[1].entry = upper;
      set.items[1].weight = t;
      set.count = 2;
      return set;
    }

    /// Gradient-band weighting over EVERY entry: for entry i,
    ///
    ///   h_i = min over j != i of clamp01(1 - dot(p - p_i, p_j - p_i) / |p_j - p_i|^2)
    ///
    /// normalised across the entries that survive. Each term falls linearly
    /// to zero exactly where entry j takes over from entry i, which buys two
    /// properties an "N nearest, inverse distance" scheme cannot have:
    ///
    /// - It is continuous everywhere. Ranking the nearest few entries flips
    ///   discontinuously on every bisector where two of them are equidistant
    ///   — on an 8-way strafe set, at all eight 22.5 degree boundaries — and
    ///   the entry that drops out is still carrying weight when it does, so
    ///   the pose pops. Nothing here is ranked: every entry is weighted, and
    ///   the only place a rank still exists — the kMaxBlendContributions cap,
    ///   which needs a tree bigger than any authored set — is made continuous
    ///   by rebasing the kept weights against the heaviest dropped one.
    /// - It clamps outside the authored set. Past the ring of entries every
    ///   one of them except the one facing the query is shadowed, so its band
    ///   is zero and the boundary clip plays alone — the same edge behaviour
    ///   blend_1d has. Inverse distance instead drifts toward the plain mean
    ///   of whatever it kept, which is not a pose anybody authored.
    ///
    /// An exact hit still reproduces its clip exactly: at p == p_i every
    /// other band evaluates to clamp01(1 - 1) == 0.
    BlendSet blend_2d(const AnimState &state, float x, float y)
    {
      BlendSet set;
      const std::size_t count = state.entries.size();
      if (count == 0)
      {
        return set;
      }

      int nearest = -1;
      float nearestDistance = 0.0f;
      /// Heaviest band that did not fit in `set`. Zero for any tree with at
      /// most kMaxBlendContributions entries, which is what keeps those
      /// exact; see the subtraction below for what it is for.
      float droppedBand = 0.0f;

      for (std::size_t i = 0; i < count; ++i)
      {
        const float originX = state.entries[i].thresholdX;
        const float originY = state.entries[i].thresholdY;
        const float toPointX = x - originX;
        const float toPointY = y - originY;

        const float distanceSquared = toPointX * toPointX + toPointY * toPointY;
        if (nearest < 0 || distanceSquared < nearestDistance)
        {
          nearest = static_cast<int>(i);
          nearestDistance = distanceSquared;
        }

        float band = 1.0f;
        for (std::size_t j = 0; j < count && band > 0.0f; ++j)
        {
          if (j == i)
          {
            continue;
          }
          const float axisX = state.entries[j].thresholdX - originX;
          const float axisY = state.entries[j].thresholdY - originY;
          const float axisLengthSquared = axisX * axisX + axisY * axisY;
          if (axisLengthSquared <= kValueEpsilon * kValueEpsilon)
          {
            // Two entries authored at the same point cannot shadow each
            // other; they just share whatever weight that point carries.
            continue;
          }
          band = std::min(band, clamp01(1.0f - (toPointX * axisX + toPointY * axisY) / axisLengthSquared));
        }

        if (band <= 0.0f)
        {
          continue;
        }

        // Insertion sort, heaviest first, so an oversized tree keeps the
        // entries that matter and drops the ones nearest to zero.
        int position = set.count;
        while (position > 0 && set.items[position - 1].weight < band)
        {
          --position;
        }
        if (position >= kMaxBlendContributions)
        {
          droppedBand = std::fmax(droppedBand, band);
          continue;
        }
        if (set.count == kMaxBlendContributions)
        {
          // This insertion evicts the lightest entry currently held.
          droppedBand = std::fmax(droppedBand, set.items[kMaxBlendContributions - 1].weight);
        }
        for (int shift = std::min(set.count, kMaxBlendContributions - 1); shift > position; --shift)
        {
          set.items[shift] = set.items[shift - 1];
        }
        set.items[position].entry = static_cast<int>(i);
        set.items[position].weight = band;
        if (set.count < kMaxBlendContributions)
        {
          ++set.count;
        }
      }

      if (droppedBand > 0.0f)
      {
        // Something was thrown away while still carrying weight, which is the
        // exact shape of the pop this whole scheme exists to remove: which
        // entries make the cut flips discontinuously as the parameter moves.
        // Subtracting the heaviest dropped band puts every kept weight on the
        // same footing -- an entry is worth zero at the instant it crosses the
        // cut, in or out -- so the blend stays continuous even for a tree with
        // more entries than the set can hold. `droppedBand` is 0 for every
        // tree that fits, so nothing below this size is affected at all.
        // Entries are already heaviest-first, so the compaction keeps order.
        int kept = 0;
        for (int slot = 0; slot < set.count; ++slot)
        {
          const float weight = set.items[slot].weight - droppedBand;
          if (weight <= 0.0f)
          {
            break;
          }
          set.items[kept].entry = set.items[slot].entry;
          set.items[kept].weight = weight;
          ++kept;
        }
        if (kept > 0)
        {
          set.count = kept;
        }
        // kept == 0 means every entry that fit is tied with the heaviest one
        // that did not -- the exact centre of an oversized symmetric set,
        // where they really are all equidistant. Keep the un-rebased weights
        // there: an equal share of the entries held is the right answer, and
        // it is also what the neighbourhood around that point converges to.
      }

      if (set.count == 0)
      {
        // No entry carries any weight, which needs a degenerate authored set
        // (every entry at one point, or none at all). Fall back to the entry
        // the parameter is closest to rather than posing nothing at all.
        if (nearest >= 0)
        {
          set.items[0].entry = nearest;
          set.items[0].weight = 1.0f;
          set.count = 1;
        }
        return set;
      }

      float total = 0.0f;
      for (int slot = 0; slot < set.count; ++slot)
      {
        total += set.items[slot].weight;
      }
      if (total > 0.0f)
      {
        for (int slot = 0; slot < set.count; ++slot)
        {
          set.items[slot].weight /= total;
        }
      }
      return set;
    }

    /// Contributions of whichever tree kind `state` is. A Clip state never
    /// reaches this.
    BlendSet blend_contributions(const AnimState &state, float x, float y)
    {
      return state.kind == AnimStateKind::BlendTree2D ? blend_2d(state, x, y) : blend_1d(state, x);
    }
  }

  // ---- Configuration -----------------------------------------------------

  void AnimatorInstance::set_graph_reference(const std::string &reference)
  {
    // AnimatorSystem re-binds every frame, so the unchanged case has to be
    // free: rebuilding here would restart the state machine each tick.
    if (reference == graphReference_)
    {
      return;
    }
    graphReference_ = reference;
    layersDirty_ = true;
  }

  // ---- Playback ----------------------------------------------------------

  void AnimatorInstance::play_clip(const std::string &clipReference, float blendSeconds, bool looping, int layer)
  {
    if (clipReference.empty())
    {
      return;
    }

    // Clip mode has no graph to size the layer stack, so layer 0 is implicit.
    if (layers_.empty())
    {
      layers_.resize(1);
    }
    const std::size_t index = clamp_layer(layer);
    AnimatorLayerRuntime &runtime = layers_[index];

    // Scripts call play() from onUpdate every frame ("keep running"), so
    // re-playing the looping clip that is already current must be a no-op —
    // restarting it would pin the clip to its first frame. A one-shot is
    // different: asking for it again is an explicit replay request.
    if (runtime.current.state < 0 && runtime.current.clip == clipReference &&
        looping && runtime.current.looping && !runtime.current.finished)
    {
      runtime.playing = true;
      return;
    }

    // A negative request means the caller did not pick a blend, so the
    // authored AnimatorComponent value applies. It has to be resolved here,
    // before the `> 0.0f` test below reads it as "snap".
    const float blend = blendSeconds < 0.0f ? defaultBlend_ : blendSeconds;

    AnimatorSource next;
    next.state = -1;
    next.clip = clipReference;
    next.time = 0.0f;
    next.previousTime = 0.0f;
    next.speed = 1.0f;
    next.looping = looping;
    next.finished = false;

    if (blend > 0.0f)
    {
      runtime.previous = runtime.current;
      runtime.transitioning = true;
      runtime.transitionElapsed = 0.0f;
      runtime.transitionDuration = blend;
      runtime.transitionIndex = -1;
    }
    else
    {
      runtime.transitioning = false;
      runtime.transitionElapsed = 0.0f;
      runtime.transitionDuration = 0.0f;
      runtime.transitionIndex = -1;
      runtime.previous = AnimatorSource{};
    }

    runtime.current = std::move(next);
    runtime.playing = true;
  }

  bool AnimatorInstance::goto_state(const std::string &stateName, float blendSeconds, int layer)
  {
    if (graphReference_.empty())
    {
      return false;
    }

    const AnimatorGraph *graph = AnimationClipCache::instance().graph(graphReference_);
    if (graph == nullptr || graph->layers.empty())
    {
      return false;
    }

    // A script may call this before the first update has sized the stack.
    ensure_layers(graph);

    const std::size_t index = clamp_layer(layer);
    if (index >= graph->layers.size())
    {
      return false;
    }

    const AnimLayer &graphLayer = graph->layers[index];
    const int state = graphLayer.find_state(stateName);
    if (state < 0)
    {
      return false;
    }

    begin_transition(layers_[index], graphLayer, state, blendSeconds, -1);
    layers_[index].playing = true;
    return true;
  }

  void AnimatorInstance::stop(int layer)
  {
    if (layer < 0)
    {
      // Whole-animator hold: the pose stays, nothing advances.
      playing_ = false;
      return;
    }
    if (layers_.empty())
    {
      return;
    }
    layers_[clamp_layer(layer)].playing = false;
  }

  void AnimatorInstance::restart(int layer)
  {
    if (layers_.empty())
    {
      return;
    }
    AnimatorLayerRuntime &runtime = layers_[clamp_layer(layer)];
    runtime.current.time = 0.0f;
    runtime.current.previousTime = 0.0f;
    runtime.current.finished = false;
    runtime.playing = true;
  }

  void AnimatorInstance::seek(float seconds, int layer)
  {
    if (layers_.empty())
    {
      return;
    }
    AnimatorLayerRuntime &runtime = layers_[clamp_layer(layer)];

    AnimationClipCache &clips = AnimationClipCache::instance();
    const AnimatorGraph *graph = graphReference_.empty() ? nullptr : clips.graph(graphReference_);
    const float duration = source_duration(runtime.current, graph, clamp_layer(layer), clips);

    float time = seconds < 0.0f ? 0.0f : seconds;
    if (duration > 0.0f)
    {
      time = clamp_range(time, 0.0f, duration);
    }
    runtime.current.time = time;
    // Close the event window as well: a scrub must not replay every marker
    // between where the head was and where it landed.
    runtime.current.previousTime = time;
    runtime.current.finished = false;
  }

  // ---- Parameters --------------------------------------------------------

  void AnimatorInstance::set_float(const std::string &name, float value)
  {
    if (name.empty())
    {
      return;
    }
    ParamValue &param = parameters_[name];
    // Every representation is mirrored so a condition still reads correctly
    // when a script sets a parameter the graph declares with another type
    // (or sets it before the graph has even loaded).
    param.floatValue = value;
    param.intValue = static_cast<int>(std::lround(value));
    param.boolValue = std::abs(value) > kValueEpsilon;
  }

  void AnimatorInstance::set_int(const std::string &name, int value)
  {
    if (name.empty())
    {
      return;
    }
    ParamValue &param = parameters_[name];
    param.intValue = value;
    param.floatValue = static_cast<float>(value);
    param.boolValue = value != 0;
  }

  void AnimatorInstance::set_bool(const std::string &name, bool value)
  {
    if (name.empty())
    {
      return;
    }
    ParamValue &param = parameters_[name];
    param.boolValue = value;
    param.intValue = value ? 1 : 0;
    param.floatValue = value ? 1.0f : 0.0f;
  }

  void AnimatorInstance::set_trigger(const std::string &name)
  {
    if (name.empty())
    {
      return;
    }
    auto inserted = parameters_.emplace(name, ParamValue{});
    ParamValue &param = inserted.first->second;
    if (inserted.second)
    {
      // A trigger a script latches before the graph loads has no declared
      // type yet; sync_parameters adopts the graph's once it arrives.
      param.type = AnimParamType::Trigger;
    }
    param.boolValue = true;
    param.intValue = 1;
    param.floatValue = 1.0f;
  }

  void AnimatorInstance::reset_trigger(const std::string &name)
  {
    auto it = parameters_.find(name);
    if (it == parameters_.end())
    {
      return;
    }
    it->second.boolValue = false;
    it->second.intValue = 0;
    it->second.floatValue = 0.0f;
  }

  float AnimatorInstance::get_float(const std::string &name) const
  {
    auto it = parameters_.find(name);
    return it == parameters_.end() ? 0.0f : it->second.floatValue;
  }

  int AnimatorInstance::get_int(const std::string &name) const
  {
    auto it = parameters_.find(name);
    return it == parameters_.end() ? 0 : it->second.intValue;
  }

  bool AnimatorInstance::get_bool(const std::string &name) const
  {
    auto it = parameters_.find(name);
    return it == parameters_.end() ? false : it->second.boolValue;
  }

  // ---- Queries -----------------------------------------------------------

  std::string AnimatorInstance::current_state(int layer) const
  {
    if (layers_.empty() || graphReference_.empty())
    {
      return std::string();
    }
    const std::size_t index = clamp_layer(layer);
    const AnimatorGraph *graph = AnimationClipCache::instance().graph(graphReference_);
    const AnimState *state = state_for(graph, index, layers_[index].current.state);
    return state == nullptr ? std::string() : state->name;
  }

  std::string AnimatorInstance::current_clip(int layer) const
  {
    if (layers_.empty())
    {
      return std::string();
    }
    const std::size_t index = clamp_layer(layer);
    const AnimatorGraph *graph =
        graphReference_.empty() ? nullptr : AnimationClipCache::instance().graph(graphReference_);
    const std::string *reference = clip_reference_for(layers_[index].current, graph, index);
    return reference == nullptr ? std::string() : *reference;
  }

  float AnimatorInstance::normalized_time(int layer) const
  {
    if (layers_.empty())
    {
      return 0.0f;
    }
    const std::size_t index = clamp_layer(layer);
    AnimationClipCache &clips = AnimationClipCache::instance();
    const AnimatorGraph *graph = graphReference_.empty() ? nullptr : clips.graph(graphReference_);
    return source_normalized(layers_[index].current, graph, index, clips);
  }

  float AnimatorInstance::time_seconds(int layer) const
  {
    if (layers_.empty())
    {
      return 0.0f;
    }
    return layers_[clamp_layer(layer)].current.time;
  }

  bool AnimatorInstance::is_transitioning(int layer) const
  {
    if (layers_.empty())
    {
      return false;
    }
    return layers_[clamp_layer(layer)].transitioning;
  }

  // ---- Evaluation --------------------------------------------------------

  void AnimatorInstance::update(float deltaTime, const ModelAsset &asset, AnimationClipCache &clips)
  {
    refresh_skeleton(asset);

    const AnimatorGraph *graph = graphReference_.empty() ? nullptr : clips.graph(graphReference_);
    ensure_layers(graph);
    sync_parameters(graph);

    // pending_events() means "what fired this frame", so last frame's list
    // goes first. A paused animator therefore reports nothing.
    events_.clear();

    for (std::size_t i = 0; i < layers_.size(); ++i)
    {
      if (playing_ && layers_[i].playing)
      {
        advance_layer(i, graph, clips, deltaTime);
      }
      else
      {
        // A held layer still closes its event window, otherwise the frame it
        // resumes on would replay everything it crossed before the pause.
        layers_[i].current.previousTime = layers_[i].current.time;
        layers_[i].previous.previousTime = layers_[i].previous.time;
      }
    }

    // A paused animator still poses itself, so it holds the frame it stopped
    // on instead of snapping back to the bind pose.
    evaluate_pose(asset, clips, graph, playing_);
  }

  void AnimatorInstance::evaluate(const ModelAsset &asset, AnimationClipCache &clips)
  {
    refresh_skeleton(asset);

    const AnimatorGraph *graph = graphReference_.empty() ? nullptr : clips.graph(graphReference_);
    ensure_layers(graph);
    sync_parameters(graph);

    evaluate_pose(asset, clips, graph, false);
  }

  // ---- Events ------------------------------------------------------------

  std::vector<AnimationEventFired> AnimatorInstance::drain_events()
  {
    std::vector<AnimationEventFired> drained;
    drained.swap(events_);
    return drained;
  }

  bool AnimatorInstance::event_fired(const std::string &name) const
  {
    for (const AnimationEventFired &event : events_)
    {
      if (event.name == name)
      {
        return true;
      }
    }
    return false;
  }

  void AnimatorInstance::clear_events()
  {
    events_.clear();
  }

  void AnimatorInstance::reset()
  {
    layers_.clear();
    parameters_.clear();
    events_.clear();
    pose_.clear();
    palette_.clear();
    globals_.clear();
    // The graph reference, speed and playing flag are configuration and
    // survive; everything derived from a run does not.
    layersDirty_ = true;
    graphSource_ = nullptr;
  }

  // ---- Internals ---------------------------------------------------------

  std::size_t AnimatorInstance::clamp_layer(int layer) const
  {
    if (layers_.empty() || layer <= 0)
    {
      return 0;
    }
    const std::size_t index = static_cast<std::size_t>(layer);
    return index < layers_.size() ? index : layers_.size() - 1;
  }

  void AnimatorInstance::refresh_skeleton(const ModelAsset &asset)
  {
    // Pointer identity alone is not enough: ModelAssetCache can reload an
    // asset in place, which keeps the address but changes the hierarchy.
    if (skeletonSource_ == &asset && skeletonNodeCount_ == asset.nodes.size())
    {
      return;
    }

    skeleton_ = Skeleton::from_model(asset);
    skeletonSource_ = &asset;
    skeletonNodeCount_ = asset.nodes.size();
    restPose_ = skeleton_.rest_pose();
    pose_ = restPose_;
    scratchA_ = restPose_;
    scratchB_ = restPose_;
    scratchReference_ = restPose_;
    scratchReferenceB_ = restPose_;
    blendScratch_ = restPose_;

    // Masks are joint-index based, so a new skeleton invalidates them.
    for (AnimatorLayerRuntime &runtime : layers_)
    {
      runtime.maskBuilt = false;
    }
  }

  void AnimatorInstance::ensure_layers(const AnimatorGraph *graph)
  {
    if (layersDirty_)
    {
      layersDirty_ = false;
      for (AnimatorLayerRuntime &runtime : layers_)
      {
        // State indices belong to the graph that was just dropped. An ad-hoc
        // clip pushed by a script is graph-independent, so it survives —
        // AnimatorSystem binds the graph after the script has already played.
        if (runtime.current.state >= 0)
        {
          runtime.current = AnimatorSource{};
        }
        runtime.previous = AnimatorSource{};
        runtime.transitioning = false;
        runtime.transitionElapsed = 0.0f;
        runtime.transitionDuration = 0.0f;
        runtime.transitionIndex = -1;
        runtime.maskBuilt = false;
      }
    }

    if (graph != graphSource_)
    {
      // A graph reloaded behind our back (an editor save) can carry different
      // mask bones, and a mask is joint indices, so it has to be rebuilt.
      graphSource_ = graph;
      for (AnimatorLayerRuntime &runtime : layers_)
      {
        runtime.maskBuilt = false;
      }
    }

    const std::size_t wanted = (graph != nullptr && !graph->layers.empty()) ? graph->layers.size() : 1u;
    if (layers_.size() != wanted)
    {
      layers_.resize(wanted);
    }

    for (std::size_t i = 0; i < layers_.size(); ++i)
    {
      AnimatorLayerRuntime &runtime = layers_[i];
      const AnimLayer *layer = (graph != nullptr && i < graph->layers.size()) ? &graph->layers[i] : nullptr;
      if (layer != nullptr)
      {
        // Re-read every frame so tweaking a weight in the graph editor is
        // visible immediately.
        runtime.weight = layer->weight;

        // Saving in the graph editor replaces the cached AnimatorGraph with a
        // freshly parsed one while playback continues, and the layer is still
        // holding an index into the states of the object that was dropped. If
        // the save deleted a state that index no longer resolves, and nothing
        // else would ever reset it: state_for() returns nullptr forever, the
        // layer resolves no clip, and the entity holds the bind pose for the
        // rest of the session. Treat a stale index as "never started" so the
        // block below re-seeds it from the layer's default state.
        const int stateCount = static_cast<int>(layer->states.size());
        const bool currentStale = runtime.current.state >= stateCount;
        if (currentStale || runtime.previous.state >= stateCount)
        {
          if (currentStale)
          {
            runtime.current = AnimatorSource{};
          }
          // Either way the crossfade is over: at least one of its two halves
          // refers to a state that is gone.
          runtime.previous = AnimatorSource{};
          runtime.transitioning = false;
          runtime.transitionElapsed = 0.0f;
          runtime.transitionDuration = 0.0f;
          runtime.transitionIndex = -1;
        }
      }

      // A source with neither a state nor a clip has never been started.
      const bool fresh = runtime.current.state < 0 && runtime.current.clip.empty();
      if (!fresh || layer == nullptr || layer->states.empty())
      {
        continue;
      }

      int defaultState = layer->defaultState;
      if (defaultState < 0 || defaultState >= static_cast<int>(layer->states.size()))
      {
        defaultState = 0;
      }
      const AnimState &state = layer->states[static_cast<std::size_t>(defaultState)];
      runtime.current.state = defaultState;
      runtime.current.clip.clear();
      runtime.current.time = 0.0f;
      runtime.current.previousTime = 0.0f;
      runtime.current.speed = state.speed;
      runtime.current.looping = state.looping;
      runtime.current.finished = false;
    }
  }

  void AnimatorInstance::sync_parameters(const AnimatorGraph *graph)
  {
    if (graph == nullptr)
    {
      return;
    }

    for (const AnimParameter &declared : graph->parameters)
    {
      if (declared.name.empty())
      {
        continue;
      }
      auto inserted = parameters_.emplace(declared.name, ParamValue{});
      ParamValue &param = inserted.first->second;
      // The graph owns the type even for a parameter a script created first,
      // but never the value: a script may legitimately set a parameter before
      // the graph has finished loading.
      param.type = declared.type;
      if (!inserted.second)
      {
        continue;
      }
      param.floatValue = declared.floatValue;
      param.intValue = declared.intValue;
      param.boolValue = declared.boolValue;
      switch (declared.type)
      {
      case AnimParamType::Float:
        param.intValue = static_cast<int>(std::lround(declared.floatValue));
        param.boolValue = std::abs(declared.floatValue) > kValueEpsilon;
        break;
      case AnimParamType::Int:
        param.floatValue = static_cast<float>(declared.intValue);
        param.boolValue = declared.intValue != 0;
        break;
      case AnimParamType::Bool:
      case AnimParamType::Trigger:
        param.floatValue = declared.boolValue ? 1.0f : 0.0f;
        param.intValue = declared.boolValue ? 1 : 0;
        break;
      }
    }
  }

  bool AnimatorInstance::condition_holds(const AnimCondition &condition) const
  {
    auto it = parameters_.find(condition.parameter);
    if (it == parameters_.end())
    {
      // An unknown parameter never satisfies a guard: a graph referring to a
      // parameter it does not declare should stall, not fire at random.
      return false;
    }
    const ParamValue &param = it->second;
    const bool integral = param.type == AnimParamType::Int;

    switch (condition.op)
    {
    case AnimConditionOp::Greater:
      return param.floatValue > condition.threshold;
    case AnimConditionOp::Less:
      return param.floatValue < condition.threshold;
    case AnimConditionOp::GreaterOrEqual:
      return param.floatValue >= condition.threshold;
    case AnimConditionOp::LessOrEqual:
      return param.floatValue <= condition.threshold;
    case AnimConditionOp::Equals:
      if (integral)
      {
        return param.intValue == static_cast<int>(std::lround(condition.threshold));
      }
      return std::abs(param.floatValue - condition.threshold) <= kValueEpsilon;
    case AnimConditionOp::NotEquals:
      if (integral)
      {
        return param.intValue != static_cast<int>(std::lround(condition.threshold));
      }
      return std::abs(param.floatValue - condition.threshold) > kValueEpsilon;
    case AnimConditionOp::IsTrue:
      return param.boolValue;
    case AnimConditionOp::IsFalse:
      return !param.boolValue;
    }
    return false;
  }

  bool AnimatorInstance::transition_ready(const AnimTransition &transition, const AnimatorLayerRuntime &runtime,
                                          float sourceNormalizedTime, bool sourceWrapped) const
  {
    if (runtime.transitioning && !transition.canInterrupt)
    {
      return false;
    }

    if (transition.hasExitTime)
    {
      // A looping source that wrapped this frame has passed every exit time
      // in the clip, even though its normalised time is back near zero.
      if (!sourceWrapped && sourceNormalizedTime < transition.exitTime)
      {
        return false;
      }
    }

    for (const AnimCondition &condition : transition.conditions)
    {
      if (!condition_holds(condition))
      {
        return false;
      }
    }
    return true;
  }

  void AnimatorInstance::consume_triggers(const AnimTransition &transition)
  {
    for (const AnimCondition &condition : transition.conditions)
    {
      auto it = parameters_.find(condition.parameter);
      if (it == parameters_.end() || it->second.type != AnimParamType::Trigger)
      {
        continue;
      }
      it->second.boolValue = false;
      it->second.intValue = 0;
      it->second.floatValue = 0.0f;
    }
  }

  void AnimatorInstance::advance_layer(std::size_t layerIndex, const AnimatorGraph *graph,
                                       AnimationClipCache &clips, float deltaTime)
  {
    AnimatorLayerRuntime &runtime = layers_[layerIndex];

    advance_source(runtime.current, graph, layerIndex, clips, deltaTime);
    if (runtime.transitioning)
    {
      // The outgoing source keeps playing while it fades, so a crossfade
      // blends two moving poses rather than a moving one into a frozen one.
      advance_source(runtime.previous, graph, layerIndex, clips, deltaTime);
      runtime.transitionElapsed += deltaTime;
      if (runtime.transitionElapsed >= runtime.transitionDuration)
      {
        runtime.transitioning = false;
        runtime.transitionElapsed = 0.0f;
        runtime.transitionDuration = 0.0f;
        runtime.transitionIndex = -1;
        runtime.previous = AnimatorSource{};
      }
    }

    // Transitions are picked after advancing so an exit time reached this
    // frame is honoured this frame.
    select_transition(layerIndex, graph, clips);
  }

  void AnimatorInstance::advance_source(AnimatorSource &source, const AnimatorGraph *graph,
                                        std::size_t layerIndex, AnimationClipCache &clips, float deltaTime)
  {
    source.previousTime = source.time;

    const float duration = source_duration(source, graph, layerIndex, clips);
    if (duration <= 0.0f)
    {
      return;
    }

    source.time += deltaTime * speed_ * source.speed;

    if (source.looping)
    {
      source.time = std::fmod(source.time, duration);
      if (source.time < 0.0f)
      {
        source.time += duration;
      }
      source.finished = false;
      return;
    }

    if (source.time >= duration)
    {
      source.time = duration;
      source.finished = true;
    }
    else if (source.time <= 0.0f)
    {
      // Reached the start playing backwards: just as finished as the end.
      source.time = 0.0f;
      source.finished = deltaTime * speed_ * source.speed < 0.0f;
    }
  }

  void AnimatorInstance::select_transition(std::size_t layerIndex, const AnimatorGraph *graph,
                                           AnimationClipCache &clips)
  {
    if (graph == nullptr || layerIndex >= graph->layers.size())
    {
      return;
    }

    const AnimLayer &layer = graph->layers[layerIndex];
    AnimatorLayerRuntime &runtime = layers_[layerIndex];
    const float normalized = source_normalized(runtime.current, graph, layerIndex, clips);
    // A wrap is "the head crossed the end of the clip", which is time moving
    // backwards only while the source plays forwards. A source with a negative
    // rate moves backwards every frame, so the undirected test would report a
    // wrap on all of them and make every hasExitTime transition eligible from
    // the first frame of the state.
    const bool reversed = speed_ * runtime.current.speed < 0.0f;
    const bool wrapped = runtime.current.looping &&
                         (reversed ? runtime.current.time > runtime.current.previousTime
                                   : runtime.current.time < runtime.current.previousTime);

    int chosen = -1;
    int chosenPriority = 0;
    for (std::size_t i = 0; i < layer.transitions.size(); ++i)
    {
      const AnimTransition &transition = layer.transitions[i];
      if (transition.fromState != AnimTransition::kAnyState && transition.fromState != runtime.current.state)
      {
        continue;
      }
      if (transition.toState < 0 || transition.toState >= static_cast<int>(layer.states.size()))
      {
        continue;
      }
      // An "any state" edge exists to reach a state from anywhere, not to
      // re-enter the one already running. Without this guard a level-triggered
      // guard (IsDead == true, not a one-shot trigger) re-selects its own
      // target the moment the crossfade ends and pins the state to its first
      // `duration` seconds forever. An explicitly authored self transition
      // (fromState == toState) is still honoured — that one was asked for.
      if (transition.fromState == AnimTransition::kAnyState && transition.toState == runtime.current.state)
      {
        continue;
      }
      if (!transition_ready(transition, runtime, normalized, wrapped))
      {
        continue;
      }
      // Highest priority wins; a strict comparison keeps the earliest
      // declaration on a tie, which is the documented tie-break.
      if (chosen < 0 || transition.priority > chosenPriority)
      {
        chosen = static_cast<int>(i);
        chosenPriority = transition.priority;
      }
    }

    if (chosen < 0)
    {
      return;
    }

    const AnimTransition &transition = layer.transitions[static_cast<std::size_t>(chosen)];
    begin_transition(runtime, layer, transition.toState, transition.duration, chosen);
    consume_triggers(transition);
  }

  void AnimatorInstance::begin_transition(AnimatorLayerRuntime &runtime, const AnimLayer &layer, int stateIndex,
                                          float blendSeconds, int transitionIndex)
  {
    if (stateIndex < 0 || stateIndex >= static_cast<int>(layer.states.size()))
    {
      return;
    }
    const AnimState &state = layer.states[static_cast<std::size_t>(stateIndex)];

    AnimatorSource next;
    next.state = stateIndex;
    next.clip.clear();
    next.time = 0.0f;
    next.previousTime = 0.0f;
    next.speed = state.speed;
    next.looping = state.looping;
    next.finished = false;

    if (blendSeconds > 0.0f)
    {
      runtime.previous = runtime.current;
      runtime.transitioning = true;
      runtime.transitionElapsed = 0.0f;
      runtime.transitionDuration = blendSeconds;
      runtime.transitionIndex = transitionIndex;
    }
    else
    {
      // Zero duration snaps: no blend, and nothing left to fade out.
      runtime.transitioning = false;
      runtime.transitionElapsed = 0.0f;
      runtime.transitionDuration = 0.0f;
      runtime.transitionIndex = -1;
      runtime.previous = AnimatorSource{};
    }

    runtime.current = std::move(next);
  }

  const AnimState *AnimatorInstance::state_for(const AnimatorGraph *graph, std::size_t layerIndex,
                                               int stateIndex) const
  {
    if (graph == nullptr || stateIndex < 0 || layerIndex >= graph->layers.size())
    {
      return nullptr;
    }
    const AnimLayer &layer = graph->layers[layerIndex];
    if (stateIndex >= static_cast<int>(layer.states.size()))
    {
      return nullptr;
    }
    return &layer.states[static_cast<std::size_t>(stateIndex)];
  }

  const std::string *AnimatorInstance::clip_reference_for(const AnimatorSource &source, const AnimatorGraph *graph,
                                                          std::size_t layerIndex) const
  {
    const AnimState *state = state_for(graph, layerIndex, source.state);
    if (state == nullptr)
    {
      return source.clip.empty() ? nullptr : &source.clip;
    }
    if (state->kind == AnimStateKind::Clip)
    {
      return state->clip.empty() ? nullptr : &state->clip;
    }

    const BlendSet set =
        blend_contributions(*state, get_float(state->blendParameterX), get_float(state->blendParameterY));
    const int dominant = set.dominant();
    if (dominant < 0 || set.items[dominant].entry < 0)
    {
      return nullptr;
    }
    const AnimBlendEntry &entry = state->entries[static_cast<std::size_t>(set.items[dominant].entry)];
    return entry.clip.empty() ? nullptr : &entry.clip;
  }

  float AnimatorInstance::source_duration(const AnimatorSource &source, const AnimatorGraph *graph,
                                          std::size_t layerIndex, AnimationClipCache &clips) const
  {
    const AnimState *state = state_for(graph, layerIndex, source.state);

    if (state == nullptr || state->kind == AnimStateKind::Clip)
    {
      const std::string &reference = state == nullptr ? source.clip : state->clip;
      if (reference.empty())
      {
        return 0.0f;
      }
      const AnimationClipAsset *clip = clips.clip(reference);
      return clip == nullptr ? 0.0f : clip->duration;
    }

    // A blend tree runs on one timeline whose length is the weighted average
    // of its children's, so the stride rate morphs smoothly as the parameter
    // moves from a walk to a run.
    const BlendSet set =
        blend_contributions(*state, get_float(state->blendParameterX), get_float(state->blendParameterY));

    float total = 0.0f;
    float weight = 0.0f;
    for (int i = 0; i < set.count; ++i)
    {
      if (set.items[i].entry < 0)
      {
        continue;
      }
      const AnimBlendEntry &entry = state->entries[static_cast<std::size_t>(set.items[i].entry)];
      if (entry.clip.empty())
      {
        continue;
      }
      const AnimationClipAsset *clip = clips.clip(entry.clip);
      if (clip == nullptr || clip->duration <= 0.0f)
      {
        continue;
      }
      const float entrySpeed = std::abs(entry.speed) > kValueEpsilon ? std::abs(entry.speed) : 1.0f;
      total += set.items[i].weight * (clip->duration / entrySpeed);
      weight += set.items[i].weight;
    }

    return weight > 0.0f ? total / weight : 0.0f;
  }

  float AnimatorInstance::source_normalized(const AnimatorSource &source, const AnimatorGraph *graph,
                                            std::size_t layerIndex, AnimationClipCache &clips) const
  {
    const float duration = source_duration(source, graph, layerIndex, clips);
    if (duration <= 0.0f)
    {
      return 0.0f;
    }
    return clamp01(source.time / duration);
  }

  bool AnimatorInstance::evaluate_source(const AnimatorSource &source, const AnimatorGraph *graph,
                                         std::size_t layerIndex, const Skeleton &skeleton,
                                         AnimationClipCache &clips, Pose &out, bool collectEvents)
  {
    // Clips only write the channels they key, so every evaluation starts from
    // the rest pose. Copying the cached one reuses `out`'s buffers.
    out = restPose_;

    // Sign of the rate the play head actually moved at this frame. Only the
    // event window cares: sampling itself is direction-agnostic.
    const bool reversed = speed_ * source.speed < 0.0f;

    const AnimState *state = state_for(graph, layerIndex, source.state);
    if (state == nullptr)
    {
      return sample_clip_into(source.clip, source.time, source.previousTime, source.looping,
                              skeleton, clips, out, collectEvents, reversed);
    }
    if (state->kind == AnimStateKind::Clip)
    {
      return sample_clip_into(state->clip, source.time, source.previousTime, source.looping,
                              skeleton, clips, out, collectEvents, reversed);
    }

    const BlendSet set =
        blend_contributions(*state, get_float(state->blendParameterX), get_float(state->blendParameterY));
    if (set.count == 0)
    {
      return false;
    }

    const float duration = source_duration(source, graph, layerIndex, clips);
    const float normalized = duration > 0.0f ? clamp01(source.time / duration) : 0.0f;
    const float normalizedPrevious = duration > 0.0f ? clamp01(source.previousTime / duration) : 0.0f;
    const int dominant = set.dominant();

    const BoneMask noMask;
    bool resolved = false;
    float accumulated = 0.0f;

    for (int i = 0; i < set.count; ++i)
    {
      if (set.items[i].entry < 0 || set.items[i].weight <= 0.0f)
      {
        continue;
      }
      const AnimBlendEntry &entry = state->entries[static_cast<std::size_t>(set.items[i].entry)];
      if (entry.clip.empty())
      {
        continue;
      }
      const AnimationClipAsset *clip = clips.clip(entry.clip);
      if (clip == nullptr)
      {
        continue;
      }

      // Every child is sampled at the SAME normalised phase rather than at
      // the same seconds: that is what keeps a walk and a run foot-synced
      // through the blend instead of sliding against each other.
      const float childTime = clamp_range(normalized * clip->duration, 0.0f, clip->duration);

      Pose &target = resolved ? blendScratch_ : out;
      if (resolved)
      {
        blendScratch_ = restPose_;
      }
      clip->sample(skeleton, childTime, target);

      if (resolved)
      {
        // Incremental weighted average: each child folds in at its share of
        // the weight gathered so far, so N children need one extra pose.
        const float share = set.items[i].weight / (accumulated + set.items[i].weight);
        pose_ops::blend_into(out, blendScratch_, share, noMask);
      }

      accumulated += set.items[i].weight;
      resolved = true;

      if (collectEvents && dominant >= 0 && set.items[dominant].entry == set.items[i].entry)
      {
        const float childPrevious = clamp_range(normalizedPrevious * clip->duration, 0.0f, clip->duration);
        // Same ordering rule as sample_clip_into(): the window is always
        // handed over low-to-high so a backwards pass is not mistaken for a
        // wrap on every frame.
        const float from = reversed ? childTime : childPrevious;
        const float to = reversed ? childPrevious : childTime;
        collect_events(*clip, entry.clip, from, to, source.looping && to < from);
      }
    }

    return resolved;
  }

  bool AnimatorInstance::sample_clip_into(const std::string &reference, float time, float previousTime, bool looping,
                                          const Skeleton &skeleton, AnimationClipCache &clips,
                                          Pose &out, bool collectEvents, bool reversed)
  {
    if (reference.empty())
    {
      return false;
    }
    const AnimationClipAsset *clip = clips.clip(reference);
    if (clip == nullptr)
    {
      return false;
    }

    const float duration = clip->duration > 0.0f ? clip->duration : 0.0f;
    const float sampleTime = duration > 0.0f ? clamp_range(time, 0.0f, duration) : 0.0f;
    clip->sample(skeleton, sampleTime, out);

    if (collectEvents)
    {
      const float other = duration > 0.0f ? clamp_range(previousTime, 0.0f, duration) : 0.0f;
      // events_in_range() reads its window forwards, so a source running
      // backwards has to hand it the two times the other way round. Deriving
      // "wrapped" from the ordered pair instead of from `time < previousTime`
      // is what keeps a rewinding loop from reporting a wrap — and therefore
      // the whole clip's worth of events — on every single frame.
      const float from = reversed ? sampleTime : other;
      const float to = reversed ? other : sampleTime;
      collect_events(*clip, reference, from, to, looping && to < from);
    }
    return true;
  }

  void AnimatorInstance::collect_events(const AnimationClipAsset &clip, const std::string &reference,
                                        float fromTime, float toTime, bool looped)
  {
    if (clip.events.empty() || events_.size() >= kMaxPendingEvents)
    {
      return;
    }
    if (!looped && fromTime == toTime)
    {
      return;
    }

    eventScratch_.clear();
    clip.events_in_range(fromTime, toTime, looped, eventScratch_);

    for (const AnimationEventKey *key : eventScratch_)
    {
      if (key == nullptr)
      {
        break;
      }
      if (events_.size() >= kMaxPendingEvents)
      {
        break;
      }
      AnimationEventFired fired;
      fired.name = key->name;
      fired.stringValue = key->stringValue;
      fired.floatValue = key->floatValue;
      fired.clip = clip.name.empty() ? reference : clip.name;
      fired.time = key->time;
      events_.push_back(std::move(fired));
    }
  }

  void AnimatorInstance::build_additive_reference(const AnimatorSource &source, const AnimatorGraph *graph,
                                                  std::size_t layerIndex, AnimationClipCache &clips, Pose &out)
  {
    out = restPose_;

    const std::string *reference = clip_reference_for(source, graph, layerIndex);
    if (reference == nullptr)
    {
      return;
    }
    const AnimationClipAsset *clip = clips.clip(*reference);
    if (clip == nullptr || !clip->additive)
    {
      // A normal clip on an additive layer is a delta over the rest pose,
      // which is what an aim offset authored from the bind pose expects.
      return;
    }
    const float time = clip->duration > 0.0f
                           ? clamp_range(clip->additiveReferenceTime, 0.0f, clip->duration)
                           : 0.0f;
    clip->sample(skeleton_, time, out);
  }

  void AnimatorInstance::evaluate_pose(const ModelAsset &asset, AnimationClipCache &clips,
                                       const AnimatorGraph *graph, bool collectEvents)
  {
    if (skeleton_.empty() || layers_.empty())
    {
      pose_.clear();
      palette_ = asset.bindPose();
      return;
    }

    const BoneMask noMask;
    bool resolved = false;

    for (std::size_t i = 0; i < layers_.size(); ++i)
    {
      AnimatorLayerRuntime &runtime = layers_[i];
      const AnimLayer *layer = (graph != nullptr && i < graph->layers.size()) ? &graph->layers[i] : nullptr;

      if (!runtime.maskBuilt)
      {
        runtime.mask = layer != nullptr
                           ? pose_ops::build_mask(skeleton_, layer->maskBones, layer->maskIncludesDescendants)
                           : BoneMask{};
        runtime.maskBuilt = true;
      }

      const float blend = transition_blend(runtime);
      // Only the source carrying at least half the blend fires events, so a
      // crossfade never plays both clips' footsteps.
      const bool currentDominant = blend >= 0.5f;

      Pose *result = &scratchA_;
      // Hoisted out of the transition block: the additive branch below has to
      // know which halves are actually showing before it can pick the pose to
      // measure them against.
      bool previousResolved = false;
      const bool currentResolved =
          evaluate_source(runtime.current, graph, i, skeleton_, clips, scratchA_, collectEvents && currentDominant);
      if (currentResolved)
      {
        resolved = true;
      }

      if (runtime.transitioning)
      {
        previousResolved = evaluate_source(runtime.previous, graph, i, skeleton_, clips, scratchB_,
                                           collectEvents && !currentDominant);
        if (previousResolved)
        {
          resolved = true;
        }

        // A source that resolved nothing left its scratch pose at rest, and
        // rest is the bind pose. Crossfading against it would play the T-pose
        // for the length of the blend — which is exactly what a script's
        // first `play_clip(clip, 0.3f)` on a fresh instance would do, because
        // the source it fades out of has never been started. Blend only when
        // both halves have something to show; otherwise take whichever one
        // does, and fall through to the bind-pose path when neither does.
        if (previousResolved && currentResolved)
        {
          // The outgoing pose is the base: at t = 0 the layer still looks
          // exactly like the state it is leaving.
          pose_ops::blend_into(scratchB_, scratchA_, blend, noMask);
          result = &scratchB_;
        }
        else if (previousResolved)
        {
          result = &scratchB_;
        }
      }

      if (i == 0)
      {
        // The base layer defines the pose; masks and weights only make sense
        // for what stacks on top of it.
        pose_ = *result;
        continue;
      }

      const float weight = clamp01(runtime.weight);
      if (weight <= 0.0f)
      {
        continue;
      }

      if (layer != nullptr && layer->additive)
      {
        // add_additive() measures `*result` against the reference, so the two
        // have to describe the same thing. `*result` is whichever halves of
        // the crossfade resolved, so the reference has to be built the same
        // way: a layer whose delta is zero on both sides otherwise pops to
        // the full difference between the two clips' reference frames and
        // eases out of it over the whole blend.
        if (runtime.transitioning && previousResolved && currentResolved)
        {
          build_additive_reference(runtime.previous, graph, i, clips, scratchReferenceB_);
          build_additive_reference(runtime.current, graph, i, clips, scratchReference_);
          // noMask, not runtime.mask: add_additive applies the mask itself,
          // and masking the reference here would put the mismatch back on
          // exactly the joints the mask excludes.
          pose_ops::blend(scratchReferenceB_, scratchReference_, blend, noMask, scratchReference_);
        }
        else if (runtime.transitioning && previousResolved)
        {
          // Only the outgoing half is showing, so only its reference applies.
          build_additive_reference(runtime.previous, graph, i, clips, scratchReference_);
        }
        else
        {
          build_additive_reference(runtime.current, graph, i, clips, scratchReference_);
        }
        pose_ops::add_additive(pose_, *result, scratchReference_, weight, runtime.mask);
      }
      else
      {
        pose_ops::blend_into(pose_, *result, weight, runtime.mask);
      }
    }

    pose_ops::normalize_rotations(pose_);

    if (!resolved)
    {
      // Nothing playable resolved (no graph, unloaded clip, empty state): the
      // mesh still has to draw, so hand back the model's own bind pose.
      palette_ = asset.bindPose();
      return;
    }

    // pose_to_palette() split in two so the model-space matrices land in the
    // reused globals_ buffer instead of a per-frame temporary.
    skeleton_.local_to_global(pose_, globals_);
    Skeleton::globals_to_palette(asset, globals_, palette_);
  }
}
