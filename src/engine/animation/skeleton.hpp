#ifndef HADES_ENGINE_ANIMATION_SKELETON_HPP
#define HADES_ENGINE_ANIMATION_SKELETON_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include "../rendering/math3d.hpp"
#include "animation_types.hpp"

namespace hades
{
  class ModelAsset;

  /// One joint of a skeleton. Joints map 1:1 onto `ModelAsset::nodes`, in the
  /// same order, so a joint index is also a node index — that is what lets a
  /// pose be turned back into a GPU bone palette.
  ///
  /// The rest transform is the node's authored local transform, decomposed so
  /// the editor can show and key translation / rotation / scale separately.
  struct SkeletonJoint
  {
    std::string name;
    int parent = -1;
    math::Vec3 restTranslation;
    math::Quat restRotation;
    math::Vec3 restScale{1.0f, 1.0f, 1.0f};

    /// True when some vertex is actually skinned to this joint (i.e. it owns
    /// at least one entry in `ModelAsset::bones`). Joints that fail this are
    /// still real hierarchy nodes — the editor draws them, but moving one
    /// only affects its skinned descendants.
    bool skinned = false;

    /// Whatever the node's local transform carries that TRS cannot express,
    /// factored out so it survives the decomposition:
    /// `buildModelMatrix(restTranslation, restRotation, restScale) *
    /// restCorrection == node.localTransform`.
    ///
    /// A COLLADA `<matrix>` node — or `<scale>` written before `<rotate>` —
    /// yields a sheared linear part that decomposes lossily, and dropping
    /// the leftover would put the animator's rest pose in a different space
    /// from the bind pose the offset matrices were derived against: the mesh
    /// then deforms the moment the animator takes over, with nothing logged.
    /// Identity for every rig whose nodes really are TRS, which is what
    /// `hasCorrection` gates the extra multiply on.
    math::Mat4 restCorrection = math::Mat4::identity();
    bool hasCorrection = false;
  };

  /// A flat, parent-before-child joint hierarchy built from a ModelAsset.
  ///
  /// Skeletons are cheap value types rebuilt from the asset whenever needed;
  /// they are never cached across frames, because ModelAssetCache::clear()
  /// can invalidate the asset they were derived from at any workspace switch.
  class Skeleton
  {
  public:
    static Skeleton from_model(const ModelAsset &asset);

    const std::vector<SkeletonJoint> &joints() const { return joints_; }
    std::size_t size() const { return joints_.size(); }
    bool empty() const { return joints_.empty(); }

    /// Joint index by name, or -1. Names come from the source file and are
    /// what authored clips bind to, which is what makes a clip retargetable
    /// onto any skeleton sharing the naming.
    int find(const std::string &name) const;

    const SkeletonJoint &joint(std::size_t index) const { return joints_[index]; }

    /// Direct children of `joint`, in hierarchy order.
    const std::vector<int> &children(std::size_t joint) const { return children_[joint]; }

    /// Indices whose parent is -1.
    const std::vector<int> &roots() const { return roots_; }

    /// The pose every clip starts from: each joint at its rest transform.
    Pose rest_pose() const;

    /// Compose local TRS up the hierarchy into model-space joint matrices.
    /// `pose` must be sized to this skeleton; short poses are padded with the
    /// rest transform.
    void local_to_global(const Pose &pose, std::vector<math::Mat4> &outGlobals) const;

    /// Model-space position of every joint, for the viewport overlay.
    void global_positions(const std::vector<math::Mat4> &globals, std::vector<math::Vec3> &outPositions) const;

    /// Turn model-space joint matrices into the skinning palette the mesh
    /// pipeline uploads: `globalInverse * globals[bone.nodeIndex] * offset`.
    static void globals_to_palette(
        const ModelAsset &asset,
        const std::vector<math::Mat4> &globals,
        std::vector<math::Mat4> &outPalette);

    /// Convenience: pose -> palette in one call.
    void pose_to_palette(
        const ModelAsset &asset,
        const Pose &pose,
        std::vector<math::Mat4> &outPalette) const;

  private:
    std::vector<SkeletonJoint> joints_;
    std::vector<std::vector<int>> children_;
    std::vector<int> roots_;
    std::unordered_map<std::string, int> byName_;
  };
}

#endif
