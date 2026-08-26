#ifndef HADES_ENGINE_ANIMATION_ANIMATION_CLIP_HPP
#define HADES_ENGINE_ANIMATION_ANIMATION_CLIP_HPP

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "animation_types.hpp"

namespace hades
{
  class ModelAsset;
  class Skeleton;

  /// Keys for one joint, addressed by joint NAME rather than index so a clip
  /// survives a re-import that renumbers nodes, and retargets onto any
  /// skeleton that shares the naming.
  ///
  /// A sub-track with no keys is not animated: the joint keeps whatever the
  /// pose already held for that channel (its rest value, or a lower layer's
  /// output). This is what makes rotation-only clips safe.
  struct AnimationBoneTrack
  {
    std::string bone;
    std::vector<AnimVec3Key> translations;
    std::vector<AnimQuatKey> rotations;
    std::vector<AnimVec3Key> scales;

    bool empty() const
    {
      return translations.empty() && rotations.empty() && scales.empty();
    }

    std::size_t keyCount() const
    {
      return translations.size() + rotations.size() + scales.size();
    }
  };

  /// An authored animation clip: the asset the animation editor writes and
  /// the animator plays. Stored as JSON under `<assets>/.hades/animations/`.
  ///
  /// Distinct from `AnimationClip` in model_asset.hpp, which is the read-only
  /// result of an assimp import: that one is indexed by node and carries no
  /// easing or events. `bake_from_model` converts one into this form so an
  /// imported animation can be edited.
  class AnimationClipAsset
  {
  public:
    static constexpr int kFormatVersion = 1;

    std::string name;
    /// Workspace-relative path of the model this was authored against. Only
    /// a hint for the editor — playback binds by joint name.
    std::string sourceModel;
    float duration = 1.0f;
    /// Authoring frame rate. Timeline snapping and the frame counter use it;
    /// evaluation is always in seconds.
    float frameRate = 30.0f;
    bool looping = true;
    /// Additive clips are applied as a delta over the layer beneath them,
    /// measured against the pose at `additiveReferenceTime`.
    bool additive = false;
    float additiveReferenceTime = 0.0f;

    std::vector<AnimationBoneTrack> tracks;
    std::vector<AnimationEventKey> events;

    // ---- Track access ----------------------------------------------------

    const AnimationBoneTrack *find_track(const std::string &bone) const;
    AnimationBoneTrack *find_track(const std::string &bone);
    /// Existing track for `bone`, creating an empty one if needed.
    AnimationBoneTrack &track_for(const std::string &bone);
    void remove_track(const std::string &bone);

    // ---- Key editing -----------------------------------------------------
    // All of these keep keys sorted by time and collapse keys that land on
    // the same time (within kKeyEpsilon), so the editor never has to.

    static constexpr float kKeyEpsilon = 1e-4f;

    void set_translation_key(const std::string &bone, float time, const math::Vec3 &value,
                             Interpolation interpolation = Interpolation::Linear,
                             const EaseCurve &ease = EaseCurve{});
    void set_rotation_key(const std::string &bone, float time, const math::Quat &value,
                          Interpolation interpolation = Interpolation::Linear,
                          const EaseCurve &ease = EaseCurve{});
    void set_scale_key(const std::string &bone, float time, const math::Vec3 &value,
                       Interpolation interpolation = Interpolation::Linear,
                       const EaseCurve &ease = EaseCurve{});

    /// Key every channel of one joint from a local TRS — what the editor
    /// calls after a gizmo drag with auto-key on.
    void set_pose_key(const std::string &bone, float time,
                      const math::Vec3 &translation, const math::Quat &rotation, const math::Vec3 &scale,
                      Interpolation interpolation = Interpolation::Linear,
                      const EaseCurve &ease = EaseCurve{});

    /// Remove the key at `time` on one channel. Returns true if one went.
    bool remove_key(const std::string &bone, TrackChannel channel, float time);

    /// Move a key in time, resolving collisions by replacing the key already
    /// at the destination. Returns the resulting time, or -1 when no key sat
    /// at `fromTime`.
    float move_key(const std::string &bone, TrackChannel channel, float fromTime, float toTime);

    /// Index of the key at `time` on one channel, or -1.
    int key_index_at(const std::string &bone, TrackChannel channel, float time) const;

    /// Every distinct key time in the clip, sorted — the dope sheet's
    /// "summary" row.
    void key_times(std::vector<float> &out) const;

    // ---- Evaluation ------------------------------------------------------

    /// Sample into `inOutPose`, which must already hold the pose this clip
    /// layers over (usually the skeleton's rest pose). Only channels that
    /// carry keys are written. `time` is clamped to [0, duration].
    void sample(const Skeleton &skeleton, float time, Pose &inOutPose) const;

    /// Sample as a delta over the reference pose and add it onto `inOutPose`
    /// with `weight`. Used for additive layers.
    void sample_additive(const Skeleton &skeleton, float time, float weight, Pose &inOutPose) const;

    /// Events whose time lies in the half-open window advanced this frame.
    /// Handles wrap-around when `looped` is set (fromTime > toTime).
    void events_in_range(float fromTime, float toTime, bool looped,
                         std::vector<const AnimationEventKey *> &out) const;

    // ---- Maintenance -----------------------------------------------------

    /// Sort every sub-track by time. Cheap and idempotent.
    void sort_keys();
    /// Longest key time across the clip (events included), or 0.
    float last_key_time() const;
    /// Grow `duration` to cover the last key. Never shrinks it: an author may
    /// deliberately want trailing hold time.
    void recompute_duration();
    /// Drop tracks that carry no keys at all.
    void prune_empty_tracks();
    std::size_t total_key_count() const;

    // ---- Persistence -----------------------------------------------------

    nlohmann::json to_json() const;
    static bool from_json(const nlohmann::json &document, AnimationClipAsset &out,
                          std::string *errorMessage = nullptr);

    // ---- Import ----------------------------------------------------------

    /// Convert an imported (assimp) clip of `asset` into an editable clip.
    /// Keys are copied verbatim — no resampling — and bound to joint names.
    /// Returns false when `clipIndex` is out of range.
    static bool bake_from_model(const ModelAsset &asset, int clipIndex, AnimationClipAsset &out);

    /// A one-key clip holding the skeleton's rest pose. The starting point
    /// for authoring from scratch.
    static AnimationClipAsset from_rest_pose(const Skeleton &skeleton, const std::string &clipName);
  };
}

#endif
