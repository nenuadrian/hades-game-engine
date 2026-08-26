#include "model_asset.hpp"

#include <algorithm>
#include <cmath>

#include "../core/log.hpp"

namespace hades
{
  namespace
  {
    template <typename Key>
    std::size_t findKeyIndex(const std::vector<Key> &keys, float time)
    {
      // Last key whose time is <= time; keys are sorted ascending.
      for (std::size_t i = 1; i < keys.size(); ++i)
      {
        if (keys[i].time > time)
        {
          return i - 1;
        }
      }
      return keys.size() - 1;
    }

    math::Vec3 sampleVectorKeys(const std::vector<VectorKey> &keys, float time, const math::Vec3 &fallback)
    {
      if (keys.empty())
        return fallback;
      if (keys.size() == 1 || time <= keys.front().time)
        return keys.front().value;
      if (time >= keys.back().time)
        return keys.back().value;

      const std::size_t i = findKeyIndex(keys, time);
      const auto &a = keys[i];
      const auto &b = keys[i + 1];
      const float span = b.time - a.time;
      const float t = span > 1e-6f ? (time - a.time) / span : 0.0f;
      return math::lerp(a.value, b.value, t);
    }

    math::Quat sampleQuatKeys(const std::vector<QuatKey> &keys, float time, const math::Quat &fallback)
    {
      if (keys.empty())
        return fallback;
      if (keys.size() == 1 || time <= keys.front().time)
        return keys.front().value;
      if (time >= keys.back().time)
        return keys.back().value;

      const std::size_t i = findKeyIndex(keys, time);
      const auto &a = keys[i];
      const auto &b = keys[i + 1];
      const float span = b.time - a.time;
      const float t = span > 1e-6f ? (time - a.time) / span : 0.0f;
      return math::slerp(a.value, b.value, t);
    }

    /// Largest absolute element-wise difference between two matrices.
    float maxElementDelta(const math::Mat4 &a, const math::Mat4 &b)
    {
      float delta = 0.0f;
      for (int column = 0; column < 4; ++column)
      {
        for (int row = 0; row < 4; ++row)
        {
          delta = std::fmax(delta, std::fabs(a.m[column][row] - b.m[column][row]));
        }
      }
      return delta;
    }

    /// Whatever `local` carries that translation/rotation/scale cannot
    /// express, factored out on the right so that
    /// `buildModelMatrix(t, r, s) * correction == local`.
    ///
    /// Returns false — leaving `outCorrection` untouched — when there is
    /// nothing to correct, which is every rig whose nodes really are TRS.
    /// Call only with a decomposition decomposeTRS actually accepted.
    bool restCorrectionFor(const math::Mat4 &local,
                           const math::Vec3 &translation,
                           const math::Quat &rotation,
                           const math::Vec3 &scale,
                           math::Mat4 &outCorrection)
    {
      // buildModelMatrix composes T * R * S, so its inverse is S' * R' * T'.
      // Inverted factor by factor rather than through math::Mat4::inverse():
      // the cofactor inverse gives up and returns IDENTITY when |det| < 1e-12,
      // and det here is exactly sx*sy*sz, so a node uniformly scaled by 1e-4
      // — a model authored in micrometres, or a rig carrying its unit
      // conversion on one node — would come back with `correction == local`,
      // which the caller would then apply a SECOND time on top of the sampled
      // TRS and destroy that node and its whole subtree. There is no such
      // cliff here: the caller only gets this far when decomposeTRS
      // succeeded, and that already refused every scale component below 1e-8,
      // so each reciprocal is finite and the three factors invert exactly.
      // (`rotation` comes from Quat::fromMat4, which normalises, so the
      // quaternion inverse really is the rotation matrix's inverse.)
      const math::Mat4 inverseRebuilt =
          math::Mat4::scaleMatrix({1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z}) *
          rotation.inverse().toMat4() *
          math::Mat4::translate({-translation.x, -translation.y, -translation.z});
      const math::Mat4 correction = inverseRebuilt * local;

      // A well-formed rig rebuilds to itself, so the residual is identity to
      // within float noise. Flagging that case keeps the cost of this repair
      // at one matrix product and an identity compare per keyed node for
      // every FBX/glTF skeleton, and zero extra work per sample.
      if (maxElementDelta(correction, math::Mat4::identity()) <= 1e-5f)
      {
        return false;
      }

      // Trusted only when it actually reconstructs what the file said: an
      // ill-conditioned linear part (a rig mixing 1e4 and 1e-4 scale on one
      // node) can lose enough precision in the round trip that the residual
      // is noise, and folding noise into every sample would be worse than the
      // lossy decomposition this is repairing.
      const math::Mat4 reconstructed =
          math::buildModelMatrix(translation, rotation, scale) * correction;
      float magnitude = 1.0f;
      for (int column = 0; column < 4; ++column)
      {
        for (int row = 0; row < 4; ++row)
        {
          magnitude = std::fmax(magnitude, std::fabs(local.m[column][row]));
        }
      }
      if (maxElementDelta(reconstructed, local) > 1e-4f * magnitude)
      {
        return false;
      }

      outCorrection = correction;
      return true;
    }
  }

  void ModelAsset::evaluateNodeGlobals(const AnimationClip *clip, float timeSeconds, std::vector<math::Mat4> &outNodeGlobals) const
  {
    // Per-node local transforms: hierarchy defaults, overridden by channels.
    std::vector<math::Mat4> locals(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
      locals[i] = nodes[i].localTransform;
    }

    if (clip != nullptr)
    {
      // finalize() precomputes the rest corrections. A size mismatch means
      // `nodes` was filled in or changed without re-running it, so derive
      // them here instead of dropping the repair — see nodeRestCorrections_.
      const bool precomputed = nodeRestCorrections_.size() == nodes.size();

      const float time = std::clamp(timeSeconds, 0.0f, clip->duration);
      for (const auto &channel : clip->channels)
      {
        if (channel.nodeIndex < 0 || channel.nodeIndex >= static_cast<int>(nodes.size()))
        {
          continue;
        }

        const std::size_t node = static_cast<std::size_t>(channel.nodeIndex);

        // A channel replaces the node's whole local transform, so a track
        // that keys only rotation must fall back to the node's own rest
        // translation and scale rather than to origin/unit.
        const math::Mat4 &restLocal = nodes[node].localTransform;
        math::Vec3 restPosition{0.0f, 0.0f, 0.0f};
        math::Quat restRotation;
        math::Vec3 restScale{1.0f, 1.0f, 1.0f};
        const bool decomposed = math::decomposeTRS(restLocal, restPosition, restRotation, restScale);

        // decomposeTRS only reports a *degenerate* matrix, not a lossy one: a
        // sheared but invertible linear part (a COLLADA `<matrix>` node, or
        // `<scale>` composed before `<rotate>`) decomposes "successfully" and
        // silently loses the shear. Rebuilding T*R*S below would then move a
        // node the clip never keyed away from the bind pose its offset matrix
        // was derived against, and the mesh deforms the instant any clip
        // touches that node — the bind pose itself is safe only because it
        // copies localTransform verbatim. Re-apply the leftover, so a node
        // keyed on one channel still reproduces itself on the channels the
        // clip does not touch.
        //
        // A decomposition decomposeTRS refused carries an invented (identity)
        // rotation and possibly a zero scale component: dividing by that
        // basis to recover a residual would fold the collapse straight back
        // in, so a degenerate node keeps the older, lossy behaviour.
        math::Mat4 correction = math::Mat4::identity();
        bool hasCorrection = false;
        if (precomputed)
        {
          hasCorrection = nodeRestCorrections_[node].active;
          if (hasCorrection)
          {
            correction = nodeRestCorrections_[node].correction;
          }
        }
        else if (decomposed)
        {
          hasCorrection = restCorrectionFor(restLocal, restPosition, restRotation, restScale, correction);
        }

        const math::Vec3 position = sampleVectorKeys(channel.positions, time, restPosition);
        const math::Quat rotation = sampleQuatKeys(channel.rotations, time, restRotation);
        const math::Vec3 scale = sampleVectorKeys(channel.scales, time, restScale);
        locals[node] = math::buildModelMatrix(position, rotation, scale);
        if (hasCorrection)
        {
          locals[node] = locals[node] * correction;
        }
      }
    }

    // Nodes are stored parent-before-child, so one forward pass accumulates
    // global transforms in place.
    outNodeGlobals.resize(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
      if (nodes[i].parent >= 0)
      {
        outNodeGlobals[i] = outNodeGlobals[nodes[i].parent] * locals[i];
      }
      else
      {
        outNodeGlobals[i] = locals[i];
      }
    }
  }

  void ModelAsset::paletteFromNodeGlobals(const std::vector<math::Mat4> &globals, std::vector<math::Mat4> &out) const
  {
    out.resize(bones.size());
    for (std::size_t i = 0; i < bones.size(); ++i)
    {
      const auto &bone = bones[i];
      if (bone.nodeIndex >= 0 && bone.nodeIndex < static_cast<int>(globals.size()))
      {
        out[i] = globalInverseTransform * globals[bone.nodeIndex] * bone.offsetMatrix;
      }
      else
      {
        out[i] = math::Mat4::identity();
      }
    }
  }

  void ModelAsset::evaluatePalette(const AnimationClip *clip, float timeSeconds, std::vector<math::Mat4> &out) const
  {
    std::vector<math::Mat4> globals;
    evaluateNodeGlobals(clip, timeSeconds, globals);
    paletteFromNodeGlobals(globals, out);
  }

  void ModelAsset::samplePose(int clipIndex, float timeSeconds, std::vector<math::Mat4> &outBoneMatrices) const
  {
    if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size()))
    {
      outBoneMatrices = bindPose_;
      return;
    }
    evaluatePalette(&clips[clipIndex], timeSeconds, outBoneMatrices);
  }

  void ModelAsset::samplePose(const AnimationClip &clip, float timeSeconds, std::vector<math::Mat4> &outBoneMatrices) const
  {
    evaluatePalette(&clip, timeSeconds, outBoneMatrices);
  }

  void ModelAsset::bindPoseNodeGlobals(std::vector<math::Mat4> &outNodeGlobals) const
  {
    evaluateNodeGlobals(nullptr, 0.0f, outNodeGlobals);
  }

  void ModelAsset::finalize()
  {
    // Rest corrections first: evaluateNodeGlobals reads them, and rebuilding
    // them here is what keeps them in step with `nodes` after apply_rig has
    // re-parented, renamed and appended its way through the hierarchy.
    nodeRestCorrections_.assign(nodes.size(), NodeRestCorrection{});
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
      math::Vec3 restPosition{0.0f, 0.0f, 0.0f};
      math::Quat restRotation;
      math::Vec3 restScale{1.0f, 1.0f, 1.0f};
      if (!math::decomposeTRS(nodes[i].localTransform, restPosition, restRotation, restScale))
      {
        continue;
      }
      nodeRestCorrections_[i].active = restCorrectionFor(
          nodes[i].localTransform, restPosition, restRotation, restScale, nodeRestCorrections_[i].correction);

      // Import time is the one place this can be said once. The node now
      // reproduces itself exactly, but its shear is frozen — no channel can
      // key it — so an author whose .dae relies on animated skew still needs
      // to know. Silence was the original complaint: the mesh simply came
      // apart the moment a clip touched the node.
      if (nodeRestCorrections_[i].active)
      {
        Log::warn_tagged(
            "model_asset",
            "node '%s' has a non-TRS local transform; the shear is preserved as a fixed residual but cannot be animated",
            nodes[i].name.c_str());
      }
    }

    evaluatePalette(nullptr, 0.0f, bindPose_);

    triangleCount_ = 0;
    float maxDistSq = 0.0f;
    math::Vec3 boundsMin{0.0f, 0.0f, 0.0f};
    math::Vec3 boundsMax{0.0f, 0.0f, 0.0f};
    bool haveBounds = false;
    for (const auto &mesh : meshes)
    {
      triangleCount_ += mesh.indices.size() / 3;
      for (const auto &v : mesh.vertices)
      {
        // Skin each vertex with the bind pose — the same transform the GPU
        // applies — so the bounds match what actually renders.
        math::Vec3 skinned{0.0f, 0.0f, 0.0f};
        float totalWeight = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
          const float w = v.boneWeights[i];
          if (w <= 0.0f || v.boneIndices[i] >= bindPose_.size())
          {
            continue;
          }
          skinned = skinned + bindPose_[v.boneIndices[i]].transformPoint({v.px, v.py, v.pz}) * w;
          totalWeight += w;
        }
        if (totalWeight <= 0.0f)
        {
          skinned = {v.px, v.py, v.pz};
        }
        maxDistSq = std::max(maxDistSq, skinned.lengthSquared());
        if (!haveBounds)
        {
          boundsMin = boundsMax = skinned;
          haveBounds = true;
        }
        else
        {
          boundsMin.x = std::min(boundsMin.x, skinned.x);
          boundsMin.y = std::min(boundsMin.y, skinned.y);
          boundsMin.z = std::min(boundsMin.z, skinned.z);
          boundsMax.x = std::max(boundsMax.x, skinned.x);
          boundsMax.y = std::max(boundsMax.y, skinned.y);
          boundsMax.z = std::max(boundsMax.z, skinned.z);
        }
      }
    }

    if (haveBounds)
    {
      boundsMin_ = boundsMin;
      boundsMax_ = boundsMax;
    }

    // Skinned poses can reach beyond the bind pose; pad the radius so
    // animated models are not frustum-culled mid-swing.
    const float padding = hasAnimations() ? 1.5f : 1.05f;
    boundsRadius_ = std::max(std::sqrt(maxDistSq) * padding, 0.01f);
  }
}
