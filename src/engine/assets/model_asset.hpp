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
    /// Node whose bind-pose global transform carries these vertices into
    /// model space, or -1 when they already live there.
    ///
    /// Vertices are stored exactly as the source file authored them, which is
    /// mesh-local: a rigid mesh only reaches model space through the node that
    /// references it. A mesh that arrived with skin weights is already in
    /// model space, because assimp folds that transform into the bone offset
    /// matrices — hence -1 for those, and for a mesh whose vertices have been
    /// baked into model space by `apply_rig`. Anything that measures or
    /// re-skins raw vertices (auto-weighting, rig application) has to apply
    /// this factor or it compares two different spaces.
    int nodeIndex = -1;
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

    /// True when the source file carried real skin data (bones with
    /// weights); false for rigid meshes bound to pseudo-bones only.
    bool hasSkeleton = false;

    bool hasAnimations() const { return !clips.empty(); }
    std::size_t triangleCount() const { return triangleCount_; }
    float boundsRadius() const { return boundsRadius_; }
    /// Bind-pose axis-aligned bounds in model space.
    const math::Vec3 &boundsMin() const { return boundsMin_; }
    const math::Vec3 &boundsMax() const { return boundsMax_; }
    const std::vector<math::Mat4> &bindPose() const { return bindPose_; }

    /// Evaluate the bone palette for `clipIndex` at `timeSeconds`.
    /// An out-of-range clip index yields the bind pose. `timeSeconds` is
    /// clamped into the clip; looping is the caller's concern.
    void samplePose(int clipIndex, float timeSeconds, std::vector<math::Mat4> &outBoneMatrices) const;

    /// Same, for a clip that is not (or not yet) part of this asset — the
    /// animation editor previews an unsaved clip through this overload.
    void samplePose(const AnimationClip &clip, float timeSeconds, std::vector<math::Mat4> &outBoneMatrices) const;

    /// Transform of every *node*, in ROOT-NODE space — not the space the
    /// skinned mesh is drawn in. A skeleton view wants
    /// `globalInverseTransform * nodeGlobal`, the same premultiplication the
    /// palette gets, or it draws the skeleton offset from its own mesh.
    ///
    /// This exists because the bone palette only covers nodes that skin
    /// vertices and is post-multiplied by the offset matrix, so joint
    /// positions cannot be recovered from it. Pass a null clip for the bind
    /// pose.
    ///
    /// An unkeyed node contributes its `localTransform` verbatim. A keyed one
    /// is rebuilt from sampled TRS, and whatever its `localTransform` carried
    /// that TRS cannot express (shear — a COLLADA `<matrix>` node) is
    /// re-applied as a fixed residual, so keying one channel of a sheared
    /// node does not silently move it off the bind pose on the channels the
    /// clip leaves alone. Costs nothing for a rig whose nodes really are TRS.
    void evaluateNodeGlobals(const AnimationClip *clip, float timeSeconds, std::vector<math::Mat4> &outNodeGlobals) const;

    /// Bind-pose node globals — `evaluateNodeGlobals(nullptr, 0, out)`.
    void bindPoseNodeGlobals(std::vector<math::Mat4> &outNodeGlobals) const;

    /// Turn model-space node transforms into the skinning palette.
    void paletteFromNodeGlobals(const std::vector<math::Mat4> &globals, std::vector<math::Mat4> &out) const;

    /// Compute bind pose, bounds and per-node rest corrections. Call once
    /// after all data is filled in.
    void finalize();

  private:
    void evaluatePalette(const AnimationClip *clip, float timeSeconds, std::vector<math::Mat4> &out) const;

    /// Non-TRS residual of one node's local transform, as described on
    /// evaluateNodeGlobals().
    struct NodeRestCorrection
    {
      math::Mat4 correction = math::Mat4::identity();
      bool active = false;
    };

    /// Parallel to `nodes`. A pure function of `nodes[i].localTransform`, so
    /// finalize() computes it once: deriving it per keyed channel instead
    /// costs three extra matrix products on a path scene_renderer walks per
    /// animated entity per frame — measured at 6.2 → 10.1 us per samplePose
    /// on a 128-bone, fully-keyed rig, against 6.6 us precomputed.
    /// evaluateNodeGlobals falls back to computing it inline when the sizes
    /// disagree, which is the "filled in nodes without re-running finalize()"
    /// case — slower, but never silently unrepaired.
    std::vector<NodeRestCorrection> nodeRestCorrections_;

    std::vector<math::Mat4> bindPose_;
    float boundsRadius_ = 0.5f;
    math::Vec3 boundsMin_{-0.5f, -0.5f, -0.5f};
    math::Vec3 boundsMax_{0.5f, 0.5f, 0.5f};
    std::size_t triangleCount_ = 0;
  };
}

#endif
