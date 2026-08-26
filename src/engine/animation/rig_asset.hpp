#ifndef HADES_ENGINE_ANIMATION_RIG_ASSET_HPP
#define HADES_ENGINE_ANIMATION_RIG_ASSET_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../rendering/math3d.hpp"

namespace hades
{
  class ModelAsset;

  /// One authored joint. The parent is stored by NAME so inserting or
  /// reordering joints in the editor never corrupts the hierarchy, and so a
  /// rig can attach to a joint that came from the source file.
  struct RigJoint
  {
    std::string name;
    /// Empty parents the joint at the rig root. A name that matches a node
    /// of the source model attaches the joint under that node.
    std::string parent;
    math::Vec3 translation;
    math::Quat rotation;
    math::Vec3 scale{1.0f, 1.0f, 1.0f};

    /// Index of the imported node this joint was seeded from, or -1 for a
    /// joint somebody created by hand in the editor.
    ///
    /// The name alone cannot find that node again. `rig_from_model` has to
    /// give every joint a name unique within the rig, and real exports supply
    /// neither: assimp leaves nodes unnamed all the time (its own
    /// cubes_nonames.fbx has four), and a file may name two nodes the same
    /// (cubes_with_names.fbx has two "Куб1"), so the seed renames them
    /// "" -> "joint_1" and "Куб1" -> "Куб1_1". Those names match no node, so
    /// `apply_rig` used to read them as hand-made joints and append a shadow
    /// node — plus a second palette entry — for each one, every save.
    ///
    /// Inverting the suffix instead would be guesswork that mis-merges a rig
    /// whose author really did name a joint "Arm_1" onto "Arm"; the index is
    /// what the seed actually knew. It is validated against the current node
    /// count before use, because the model can be re-exported with fewer
    /// nodes under a rig that outlives it, and a stale index falls back to
    /// the name match.
    int sourceNode = -1;

    /// Whatever the source node's local transform carried that TRS cannot
    /// express, factored out so seeding does not destroy it:
    /// `buildModelMatrix(translation, rotation, scale) * restCorrection` is
    /// the node's own local matrix.
    ///
    /// A COLLADA `<matrix>` node — or `<scale>` written before `<rotate>` —
    /// has a sheared linear part that `decomposeTRS` reports as a success
    /// while silently dropping the shear. Baking that away in "generate rig
    /// from model" would move the node the moment the rig was applied, and
    /// with it every vertex the imported offset matrices place through it.
    /// The editor still authors plain TRS; this is the imported remainder,
    /// carried along untouched. Identity for every rig whose nodes really
    /// are TRS, which is what `hasCorrection` gates the extra multiply on.
    math::Mat4 restCorrection = math::Mat4::identity();
    bool hasCorrection = false;
  };

  /// Skin weights for one mesh of the model. `jointIndices` and `weights` are
  /// flat arrays of 4 entries per vertex, in vertex order; a joint index of
  /// -1 means the slot is unused. Weights are normalised on apply.
  struct RigMeshBinding
  {
    std::uint32_t meshIndex = 0;
    std::vector<std::int32_t> jointIndices;
    std::vector<float> weights;

    std::size_t vertexCount() const { return weights.size() / 4; }
    bool empty() const { return weights.empty(); }
  };

  /// How `compute_auto_weights` distributes influence.
  enum class AutoWeightMode : std::uint8_t
  {
    /// Nearest joint takes the whole vertex. Crisp, good for mechanical rigs.
    Rigid = 0,
    /// Falloff around the bone segment (parent -> joint), radius-limited.
    Envelope,
    /// Smooth inverse-distance over the nearest joints.
    Smooth,
  };

  const char *auto_weight_mode_name(AutoWeightMode mode);

  /// An authored rig that overlays an imported model.
  ///
  /// The source file is never modified: the rig lives beside it under
  /// `<assets>/.hades/rigs/`, and ModelAssetCache applies it whenever the
  /// model is loaded. That keeps re-importing a mesh from destroying the
  /// skeleton somebody built on top of it.
  class RigAsset
  {
  public:
    static constexpr int kFormatVersion = 1;

    /// Workspace-relative path of the model this rig belongs to.
    std::string sourceModel;
    std::vector<RigJoint> joints;
    std::vector<RigMeshBinding> meshes;
    /// When true, the rig replaces any skeleton the import produced. When
    /// false, its joints are appended to the imported hierarchy.
    bool replaceImportedSkeleton = true;

    int find_joint(const std::string &name) const;
    /// Parent index of `joint` within this rig, or -1 when the parent is a
    /// model node (or the root).
    int parent_index(std::size_t joint) const;
    /// Unique name derived from `base` ("bone", "bone_1", ...).
    std::string unique_joint_name(const std::string &base) const;

    /// Model-space rest matrix of every joint, resolved against `asset` so
    /// joints parented to imported nodes land in the right place.
    void global_rest_transforms(const ModelAsset &asset, std::vector<math::Mat4> &out) const;

    /// Reorder joints so parents always precede children. Returns false when
    /// the hierarchy contains a cycle.
    bool topological_sort();

    bool empty() const { return joints.empty(); }

    nlohmann::json to_json() const;
    static bool from_json(const nlohmann::json &document, RigAsset &out,
                          std::string *errorMessage = nullptr);
  };

  /// Rewrite `asset` in place so it is skinned by `rig`: joints become nodes,
  /// bones and offset matrices are rebuilt, per-vertex influences are
  /// replaced for every bound mesh, and `finalize()` is re-run.
  ///
  /// A joint that came from the import re-points its own node rather than
  /// appending a second one, so applying a rig seeded from the import is
  /// idempotent instead of stacking a shadow skeleton that every name-bound
  /// clip would resolve to. `RigJoint::sourceNode` says which node that is —
  /// covering the unnamed and duplicate-named nodes a name can never find —
  /// and a joint without one (hand-made, or seeded by an older build) still
  /// merges on an exact name match. The vertices of each bound mesh are baked into
  /// model space, which is what lets one offset matrix per joint serve meshes
  /// their own nodes placed differently.
  ///
  /// Returns false and fills `errorMessage` when the rig cannot be applied
  /// (unknown parent, mesh index out of range, a joint parented under a node
  /// the hierarchy evaluates after it, more bones than `kMaxModelBones`).
  bool apply_rig(ModelAsset &asset, const RigAsset &rig, std::string *errorMessage = nullptr);

  /// Fill `rig.meshes` by binding every vertex of every mesh to the nearest
  /// joints. `falloff` is the envelope radius in model units (Envelope) or
  /// the inverse-distance exponent scale (Smooth); `maxInfluences` is clamped
  /// to 4.
  void compute_auto_weights(const ModelAsset &asset, RigAsset &rig,
                            AutoWeightMode mode, float falloff, int maxInfluences = 4);

  /// Weight one mesh only — what the editor's "bind selected mesh" does.
  void compute_auto_weights_for_mesh(const ModelAsset &asset, RigAsset &rig,
                                     std::uint32_t meshIndex, AutoWeightMode mode,
                                     float falloff, int maxInfluences = 4);

  /// Seed a rig from whatever skeleton the model already has, so an imported
  /// rig can be edited rather than rebuilt. Every seeded joint records the
  /// node it came from (`RigJoint::sourceNode`) and the part of that node's
  /// local transform TRS cannot hold (`RigJoint::restCorrection`), so
  /// applying the result gives the model back exactly what was imported.
  RigAsset rig_from_model(const ModelAsset &asset, const std::string &sourceModel);

  // ---- Storage -------------------------------------------------------------

  /// Where the rig for `modelReference` lives under `assetRoot`. The model
  /// path is flattened into a single file name, so two models in different
  /// folders never collide.
  std::filesystem::path rig_path_for_model(const std::filesystem::path &assetRoot,
                                           const std::string &modelReference);

  bool save_rig_asset(const std::filesystem::path &path, const RigAsset &rig,
                      std::string *errorMessage = nullptr);
  bool load_rig_asset(const std::filesystem::path &path, RigAsset &out,
                      std::string *errorMessage = nullptr);
}

#endif
