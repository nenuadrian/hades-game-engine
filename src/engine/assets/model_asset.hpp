#ifndef HADES_ENGINE_ASSETS_MODEL_ASSET_HPP
#define HADES_ENGINE_ASSETS_MODEL_ASSET_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "../rendering/math3d.hpp"
#include "../rendering/render_types.hpp"

namespace hades
{
  /// Maximum bones per model. Matches the palette size in the skinned mesh
  /// shaders (Vulkan GLSL and WebGPU WGSL) — keep the three in sync.
  constexpr uint32_t kMaxModelBones = 128;

  /// Vertex layout for imported model meshes. Every imported mesh renders
  /// through the skinned pipeline: rigid meshes are bound to a single
  /// pseudo-bone for the node that references them, so the layout is uniform.
  struct ModelVertex
  {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    uint32_t boneIndices[4];
    float boneWeights[4];
  };

  /// One drawable mesh of a model, with its imported material.
  struct ModelMeshData
  {
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
    Material material;
  };

  /// Node of the imported scene hierarchy, flattened parent-before-child.
  struct ModelNode
  {
    std::string name;
    int parent = -1;
    math::Mat4 localTransform;
  };

  /// One palette entry: a node driving vertices, plus the offset matrix that
  /// moves mesh-space vertices into that node's space at bind time.
  struct ModelBone
  {
    int nodeIndex = -1;
    math::Mat4 offsetMatrix;
  };

  struct VectorKey
  {
    float time = 0.0f;
    math::Vec3 value;
  };

  struct QuatKey
  {
    float time = 0.0f;
    math::Quat value;
  };

  /// Animated TRS keyframes for one node. Key times are in seconds.
  struct AnimationChannel
  {
    int nodeIndex = -1;
    std::vector<VectorKey> positions;
    std::vector<QuatKey> rotations;
    std::vector<VectorKey> scales;
  };

  struct AnimationClip
  {
    std::string name;
    float duration = 0.0f;
    std::vector<AnimationChannel> channels;
  };

  /// CPU-side imported model: meshes, node hierarchy, bone palette and
  /// animation clips. Produced by load_model_asset(); owned by
  /// ModelAssetCache.
  class ModelAsset
  {
  public:
    std::vector<ModelMeshData> meshes;
    std::vector<ModelNode> nodes;
    std::vector<ModelBone> bones;
    std::vector<AnimationClip> clips;
    math::Mat4 globalInverseTransform = math::Mat4::identity();

    bool hasAnimations() const { return !clips.empty(); }
    std::size_t triangleCount() const { return triangleCount_; }
    float boundsRadius() const { return boundsRadius_; }
    const std::vector<math::Mat4> &bindPose() const { return bindPose_; }

    /// Evaluate the bone palette for `clipIndex` at `timeSeconds`.
    /// An out-of-range clip index yields the bind pose. `timeSeconds` is
    /// clamped into the clip; looping is the caller's concern.
    void samplePose(int clipIndex, float timeSeconds, std::vector<math::Mat4> &outBoneMatrices) const;

    /// Compute bind pose and bounds. Call once after all data is filled in.
    void finalize();

  private:
    void evaluatePalette(const AnimationClip *clip, float timeSeconds, std::vector<math::Mat4> &out) const;

    std::vector<math::Mat4> bindPose_;
    float boundsRadius_ = 0.5f;
    std::size_t triangleCount_ = 0;
  };
}

#endif
