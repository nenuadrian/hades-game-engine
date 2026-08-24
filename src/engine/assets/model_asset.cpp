#include "model_asset.hpp"

#include <algorithm>
#include <cmath>

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
  }

  void ModelAsset::evaluatePalette(const AnimationClip *clip, float timeSeconds, std::vector<math::Mat4> &out) const
  {
    // Per-node local transforms: hierarchy defaults, overridden by channels.
    std::vector<math::Mat4> locals(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
      locals[i] = nodes[i].localTransform;
    }

    if (clip != nullptr)
    {
      const float time = std::clamp(timeSeconds, 0.0f, clip->duration);
      for (const auto &channel : clip->channels)
      {
        if (channel.nodeIndex < 0 || channel.nodeIndex >= static_cast<int>(nodes.size()))
        {
          continue;
        }

        const math::Vec3 position = sampleVectorKeys(channel.positions, time, {0.0f, 0.0f, 0.0f});
        const math::Quat rotation = sampleQuatKeys(channel.rotations, time, {});
        const math::Vec3 scale = sampleVectorKeys(channel.scales, time, {1.0f, 1.0f, 1.0f});
        locals[channel.nodeIndex] = math::buildModelMatrix(position, rotation, scale);
      }
    }

    // Nodes are stored parent-before-child, so one forward pass accumulates
    // global transforms in place.
    std::vector<math::Mat4> globals(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
      if (nodes[i].parent >= 0)
      {
        globals[i] = globals[nodes[i].parent] * locals[i];
      }
      else
      {
        globals[i] = locals[i];
      }
    }

    out.resize(bones.size());
    for (std::size_t i = 0; i < bones.size(); ++i)
    {
      const auto &bone = bones[i];
      if (bone.nodeIndex >= 0 && bone.nodeIndex < static_cast<int>(nodes.size()))
      {
        out[i] = globalInverseTransform * globals[bone.nodeIndex] * bone.offsetMatrix;
      }
      else
      {
        out[i] = math::Mat4::identity();
      }
    }
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

  void ModelAsset::finalize()
  {
    evaluatePalette(nullptr, 0.0f, bindPose_);

    triangleCount_ = 0;
    float maxDistSq = 0.0f;
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
      }
    }

    // Skinned poses can reach beyond the bind pose; pad the radius so
    // animated models are not frustum-culled mid-swing.
    const float padding = hasAnimations() ? 1.5f : 1.05f;
    boundsRadius_ = std::max(std::sqrt(maxDistSq) * padding, 0.01f);
  }
}
