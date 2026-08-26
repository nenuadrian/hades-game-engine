#ifndef HADES_ENGINE_ANIMATION_POSE_OPS_HPP
#define HADES_ENGINE_ANIMATION_POSE_OPS_HPP

#include <string>
#include <vector>

#include "animation_types.hpp"

namespace hades
{
  class Skeleton;

  /// Pose arithmetic shared by the animator and the editor preview.
  ///
  /// Every operation is per-joint and index-aligned: poses are expected to be
  /// the same size, and the shorter one wins when they are not.
  namespace pose_ops
  {
    /// out = lerp(a, b, weight), with slerp for rotations. `mask` scales the
    /// per-joint weight; an empty mask means full weight everywhere.
    void blend(const Pose &a, const Pose &b, float weight, const BoneMask &mask, Pose &out);

    /// In-place variant: target = lerp(target, source, weight).
    void blend_into(Pose &target, const Pose &source, float weight, const BoneMask &mask);

    /// target += (source - reference) * weight, in local space: translations
    /// and scales add, rotations compose. This is how an additive layer
    /// (aim offset, recoil, lean) stacks onto locomotion.
    void add_additive(Pose &target, const Pose &source, const Pose &reference,
                      float weight, const BoneMask &mask);

    /// Build a mask from joint names. When `includeDescendants` is set, every
    /// joint below a named one is included too. An empty `bones` list yields
    /// an empty (all-ones) mask.
    BoneMask build_mask(const Skeleton &skeleton, const std::vector<std::string> &bones,
                        bool includeDescendants);

    /// Renormalise every rotation. Call after a chain of blends.
    void normalize_rotations(Pose &pose);
  }
}

#endif
