#include "pose_ops.hpp"

#include <algorithm>
#include <cmath>

#include "skeleton.hpp"

namespace hades
{
  namespace pose_ops
  {
    namespace
    {
      // The blend helpers return by value so a destination that aliases one
      // of the inputs (blend_into passes the target as both base and output)
      // is read before it is written. The 0 / 1 shortcuts are not just an
      // optimisation for slerp: they also keep a fully weighted joint bit
      // for bit equal to the source instead of a renormalised copy of it.
      math::Vec3 blend_vec(const math::Vec3 &from, const math::Vec3 &to, float w)
      {
        if (w <= 0.0f)
        {
          return from;
        }
        if (w >= 1.0f)
        {
          return to;
        }
        return math::lerp(from, to, w);
      }

      math::Quat blend_quat(const math::Quat &from, const math::Quat &to, float w)
      {
        if (w <= 0.0f)
        {
          return from;
        }
        if (w >= 1.0f)
        {
          return to;
        }
        return math::slerp(from, to, w);
      }

      /// Componentwise scale factor of an additive layer: lerp(1, s/r, w).
      float additive_scale_factor(float source, float reference, float w)
      {
        // A near-zero reference carries no ratio information, and dividing
        // by it would send the target's scale to infinity. Fall back to a
        // neutral factor so the joint simply keeps its own scale.
        if (std::fabs(reference) < 1e-6f)
        {
          return 1.0f;
        }
        return 1.0f + (source / reference - 1.0f) * w;
      }
    }

    void blend(const Pose &a, const Pose &b, float weight, const BoneMask &mask, Pose &out)
    {
      // Counts are captured before `out` is touched: `out` may alias either
      // input, and resizing it would change the loop bounds mid-flight.
      const std::size_t translationCount = std::min(a.translations.size(), b.translations.size());
      const std::size_t rotationCount = std::min(a.rotations.size(), b.rotations.size());
      const std::size_t scaleCount = std::min(a.scales.size(), b.scales.size());

      // The base pose defines the layout of the result; every joint is
      // written below, so the fill value of a grown array never survives.
      if (&out != &a)
      {
        out.translations.resize(a.translations.size());
        out.rotations.resize(a.rotations.size());
        out.scales.resize(a.scales.size());
      }

      for (std::size_t i = 0; i < a.translations.size(); ++i)
      {
        // Past the end of the source the base pose survives untouched.
        out.translations[i] = i < translationCount
                                  ? blend_vec(a.translations[i], b.translations[i], weight * mask.weightFor(i))
                                  : a.translations[i];
      }

      for (std::size_t i = 0; i < a.rotations.size(); ++i)
      {
        out.rotations[i] = i < rotationCount
                               ? blend_quat(a.rotations[i], b.rotations[i], weight * mask.weightFor(i))
                               : a.rotations[i];
      }

      for (std::size_t i = 0; i < a.scales.size(); ++i)
      {
        out.scales[i] = i < scaleCount
                            ? blend_vec(a.scales[i], b.scales[i], weight * mask.weightFor(i))
                            : a.scales[i];
      }
    }

    void blend_into(Pose &target, const Pose &source, float weight, const BoneMask &mask)
    {
      blend(target, source, weight, mask, target);
    }

    void add_additive(Pose &target, const Pose &source, const Pose &reference,
                      float weight, const BoneMask &mask)
    {
      const std::size_t translationCount =
          std::min(target.translations.size(), std::min(source.translations.size(), reference.translations.size()));
      const std::size_t rotationCount =
          std::min(target.rotations.size(), std::min(source.rotations.size(), reference.rotations.size()));
      const std::size_t scaleCount =
          std::min(target.scales.size(), std::min(source.scales.size(), reference.scales.size()));

      for (std::size_t i = 0; i < translationCount; ++i)
      {
        const float w = weight * mask.weightFor(i);
        if (w <= 0.0f)
        {
          continue;
        }
        target.translations[i] = target.translations[i] + (source.translations[i] - reference.translations[i]) * w;
      }

      for (std::size_t i = 0; i < rotationCount; ++i)
      {
        const float w = weight * mask.weightFor(i);
        if (w <= 0.0f)
        {
          continue;
        }

        // The layer's own rotation relative to its reference pose. Scaling
        // it toward identity by slerp (rather than lerping the raw
        // quaternion) keeps a half-weight aim offset at half the *angle*.
        const math::Quat delta = source.rotations[i] * reference.rotations[i].inverse();
        const math::Quat scaled = w >= 1.0f ? delta : math::slerp(math::Quat{}, delta, w);

        // Composition order: `delta * target`. With this Hamilton product
        // `x * y` applies y first, so the target's own rotation happens
        // first and the delta is layered on top of it, in the joint's
        // parent frame. That is what an aim offset needs — the offset
        // swings the bone by a fixed amount no matter how the locomotion
        // pose underneath has rotated it, instead of being dragged around
        // by it. Flipping the order would make the offset direction depend
        // on the base pose.
        target.rotations[i] = scaled * target.rotations[i];
      }

      for (std::size_t i = 0; i < scaleCount; ++i)
      {
        const float w = weight * mask.weightFor(i);
        if (w <= 0.0f)
        {
          continue;
        }

        // Scale is additive in the multiplicative sense: the layer
        // contributes the ratio it applies to its own reference.
        math::Vec3 &scale = target.scales[i];
        scale.x *= additive_scale_factor(source.scales[i].x, reference.scales[i].x, w);
        scale.y *= additive_scale_factor(source.scales[i].y, reference.scales[i].y, w);
        scale.z *= additive_scale_factor(source.scales[i].z, reference.scales[i].z, w);
      }
    }

    BoneMask build_mask(const Skeleton &skeleton, const std::vector<std::string> &bones,
                        bool includeDescendants)
    {
      BoneMask mask;
      if (bones.empty())
      {
        // An empty mask is the all-ones mask, and costs no allocation --
        // by far the most common case, so it stays the cheap one.
        return mask;
      }

      const std::size_t jointCount = skeleton.size();
      mask.weights.assign(jointCount, 0.0f);

      // Reused across every named bone so the descendant walk never
      // allocates per joint.
      std::vector<int> stack;

      for (const std::string &name : bones)
      {
        const int index = skeleton.find(name);
        if (index < 0 || static_cast<std::size_t>(index) >= jointCount)
        {
          // Masks outlive re-imports and retargets, so a name that no
          // longer resolves is ignored rather than treated as an error.
          continue;
        }

        mask.weights[static_cast<std::size_t>(index)] = 1.0f;
        if (!includeDescendants)
        {
          continue;
        }

        stack.clear();
        stack.push_back(index);
        while (!stack.empty())
        {
          const int current = stack.back();
          stack.pop_back();
          for (const int child : skeleton.children(static_cast<std::size_t>(current)))
          {
            if (child < 0 || static_cast<std::size_t>(child) >= jointCount)
            {
              continue;
            }
            // Already-weighted joints are skipped, which both dedupes
            // overlapping bone lists and stops a malformed cyclic
            // hierarchy from looping forever.
            if (mask.weights[static_cast<std::size_t>(child)] >= 1.0f)
            {
              continue;
            }
            mask.weights[static_cast<std::size_t>(child)] = 1.0f;
            stack.push_back(child);
          }
        }
      }

      return mask;
    }

    void normalize_rotations(Pose &pose)
    {
      for (math::Quat &rotation : pose.rotations)
      {
        rotation = rotation.normalized();
      }
    }
  }
}
