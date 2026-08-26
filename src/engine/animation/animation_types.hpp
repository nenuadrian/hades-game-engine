#ifndef HADES_ENGINE_ANIMATION_ANIMATION_TYPES_HPP
#define HADES_ENGINE_ANIMATION_ANIMATION_TYPES_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "../rendering/math3d.hpp"

namespace hades
{
  /// How a keyframe blends into the key that follows it.
  ///
  /// Every mode is expressed as a reshaping of the normalised segment
  /// parameter `t` in [0,1]; the value itself is then produced by a plain
  /// lerp (vectors) or slerp (rotations). That keeps rotation interpolation
  /// correct for free: easing never leaves the unit quaternion sphere.
  enum class Interpolation : std::uint8_t
  {
    /// Hold the value until the next key. `t` collapses to 0.
    Step = 0,
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    /// CSS-style cubic-bezier easing using the key's four control values.
    Bezier,
  };

  const char *interpolation_name(Interpolation interpolation);
  bool interpolation_from_name(const std::string &name, Interpolation &out);

  /// Bezier control points, in the same layout as CSS `cubic-bezier`:
  /// (x1, y1) and (x2, y2) with the implicit endpoints (0,0) and (1,1).
  struct EaseCurve
  {
    float x1 = 0.25f;
    float y1 = 0.1f;
    float x2 = 0.25f;
    float y2 = 1.0f;

    bool operator==(const EaseCurve &other) const
    {
      return x1 == other.x1 && y1 == other.y1 && x2 == other.x2 && y2 == other.y2;
    }
  };

  /// Reshape a normalised segment parameter. `t` is clamped into [0,1].
  /// `curve` is only consulted for Interpolation::Bezier.
  float apply_easing(Interpolation interpolation, float t, const EaseCurve &curve = EaseCurve{});

  /// A keyed Vec3 (translation or scale). `interpolation` and `ease` describe
  /// the segment that *starts* at this key.
  struct AnimVec3Key
  {
    float time = 0.0f;
    math::Vec3 value;
    Interpolation interpolation = Interpolation::Linear;
    EaseCurve ease;
  };

  /// A keyed rotation. Same segment-ownership rule as AnimVec3Key.
  struct AnimQuatKey
  {
    float time = 0.0f;
    math::Quat value;
    Interpolation interpolation = Interpolation::Linear;
    EaseCurve ease;
  };

  /// Which sub-track of a bone a key belongs to. Used by the editor for
  /// selection, and by the clip API for generic key operations.
  enum class TrackChannel : std::uint8_t
  {
    Translation = 0,
    Rotation,
    Scale,
  };

  const char *track_channel_name(TrackChannel channel);

  /// A named marker on the timeline. The animator fires these as the play
  /// head crosses them; scripts and Blueprints react to them by name.
  struct AnimationEventKey
  {
    float time = 0.0f;
    std::string name;
    std::string stringValue;
    float floatValue = 0.0f;
  };

  /// One animation event that actually fired this frame, with the entity and
  /// clip that produced it.
  struct AnimationEventFired
  {
    std::string name;
    std::string stringValue;
    float floatValue = 0.0f;
    std::string clip;
    float time = 0.0f;
  };

  /// A local-space pose: one TRS per skeleton joint, indexed by joint index.
  /// A pose is always sized to the skeleton it was produced for.
  struct Pose
  {
    std::vector<math::Vec3> translations;
    std::vector<math::Quat> rotations;
    std::vector<math::Vec3> scales;

    std::size_t size() const { return rotations.size(); }
    bool empty() const { return rotations.empty(); }

    void resize(std::size_t jointCount);
    void clear();

    /// Local matrix for one joint.
    math::Mat4 localMatrix(std::size_t joint) const;
  };

  /// Per-joint blend weights in [0,1]. An empty mask means "every joint at
  /// full weight" — the common case, kept allocation-free.
  struct BoneMask
  {
    std::vector<float> weights;

    bool empty() const { return weights.empty(); }
    float weightFor(std::size_t joint) const
    {
      if (weights.empty())
      {
        return 1.0f;
      }
      return joint < weights.size() ? weights[joint] : 0.0f;
    }
  };
}

#endif
