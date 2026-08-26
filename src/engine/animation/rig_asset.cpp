#include "rig_asset.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <utility>

#include "../assets/model_asset.hpp"

namespace hades
{
  namespace
  {
    /// Below this a basis vector counts as collapsed: a joint scaled that far
    /// down has no invertible rest matrix.
    constexpr float kMinScale = 1e-5f;

    /// Tolerant readers. `json::value()` throws when the stored type does not
    /// match the fallback, and a hand-edited rig must never take down the
    /// asset cache, so every field is type-checked before it is read.
    std::string read_string(const nlohmann::json &parent, const char *key)
    {
      const auto it = parent.find(key);
      return (it != parent.end() && it->is_string()) ? it->get<std::string>() : std::string{};
    }

    bool read_bool(const nlohmann::json &parent, const char *key, bool fallback)
    {
      const auto it = parent.find(key);
      return (it != parent.end() && it->is_boolean()) ? it->get<bool>() : fallback;
    }

    int read_int(const nlohmann::json &parent, const char *key, int fallback)
    {
      const auto it = parent.find(key);
      return (it != parent.end() && it->is_number()) ? it->get<int>() : fallback;
    }

    template <typename T>
    std::vector<T> read_number_array(const nlohmann::json &parent, const char *key, T fallback)
    {
      std::vector<T> values;
      const auto it = parent.find(key);
      if (it == parent.end() || !it->is_array())
      {
        return values;
      }
      values.reserve(it->size());
      for (const auto &value : *it)
      {
        values.push_back(value.is_number() ? value.get<T>() : fallback);
      }
      return values;
    }

    void setError(std::string *errorMessage, std::string message)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = std::move(message);
      }
    }

    /// Index of the first node of `asset` named `name`, or -1. First match
    /// wins, which is the rule the importer already uses to resolve bones to
    /// nodes, so a rig parented by name lands on the same node a bone would.
    int find_model_node(const ModelAsset &asset, const std::string &name)
    {
      if (name.empty())
      {
        return -1;
      }
      for (std::size_t i = 0; i < asset.nodes.size(); ++i)
      {
        if (asset.nodes[i].name == name)
        {
          return static_cast<int>(i);
        }
      }
      return -1;
    }

    math::Mat4 joint_local_matrix(const RigJoint &joint)
    {
      // A rotation read back from JSON has usually drifted off unit length;
      // normalising keeps the rest basis orthogonal so its inverse stays a
      // rigid transform.
      const math::Mat4 trs =
          math::buildModelMatrix(joint.translation, joint.rotation.normalized(), joint.scale);
      // The shear an imported node carried that TRS cannot express. Only a
      // seeded joint has one, and only from a file that authored a non-TRS
      // local (a COLLADA `<matrix>`), so this multiply is skipped for every
      // hand-built and every FBX/glTF rig.
      return joint.hasCorrection ? trs * joint.restCorrection : trs;
    }

    math::Vec3 matrix_position(const math::Mat4 &matrix)
    {
      return math::Vec3{matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]};
    }

    bool is_close_to_identity(const math::Mat4 &matrix)
    {
      for (int column = 0; column < 4; ++column)
      {
        for (int row = 0; row < 4; ++row)
        {
          const float expected = column == row ? 1.0f : 0.0f;
          if (std::abs(matrix.m[column][row] - expected) > 1e-6f)
          {
            return false;
          }
        }
      }
      return true;
    }

    /// Split a node's local transform into the TRS the rig editor authors and
    /// the remainder it cannot express, writing both into `joint`.
    ///
    /// `decomposeTRS` only reports a *degenerate* matrix, not a lossy one: a
    /// sheared but invertible linear part — a COLLADA `<matrix>` node, or
    /// `<scale>` composed before `<rotate>` — decomposes "successfully" and
    /// drops the shear on the floor. Seeding a rig from that node and
    /// applying it back would rewrite the node without its shear, moving it
    /// and every vertex the imported offset matrices place through it, with
    /// nothing logged. Keeping the residual is what `Skeleton::from_model`
    /// does for the same reason, and what makes seed -> apply give the model
    /// back exactly what was imported.
    void decompose_into_joint(const math::Mat4 &local, RigJoint &joint)
    {
      if (!math::decomposeTRS(local, joint.translation, joint.rotation, joint.scale))
      {
        // Degenerate. Dividing by a basis that collapsed would fold the
        // collapse straight back into the joint, so no residual is recovered;
        // `rest_offset_matrix` is what keeps such a joint from stranding its
        // vertices at the model origin.
        return;
      }

      // Inverting T * R * S factor by factor rather than through
      // math::Mat4::inverse(): the cofactor inverse gives up and answers
      // IDENTITY when |det| < 1e-12, and det here is exactly sx*sy*sz, so a
      // node carrying a unit conversion (a model authored in micrometres)
      // would come back with `correction == local` and have that transform
      // applied a second time, destroying the node and its whole subtree.
      // There is no such cliff here: decomposeTRS already refused every scale
      // component below 1e-8, so each reciprocal is finite and the three
      // factors invert exactly. buildModelMatrix composes T * R * S, so the
      // inverse is S' * R' * T'.
      const math::Mat4 inverseRebuilt =
          math::Mat4::scaleMatrix({1.0f / joint.scale.x, 1.0f / joint.scale.y, 1.0f / joint.scale.z}) *
          joint.rotation.inverse().toMat4() *
          math::Mat4::translate({-joint.translation.x, -joint.translation.y, -joint.translation.z});
      const math::Mat4 correction = inverseRebuilt * local;
      if (is_close_to_identity(correction))
      {
        return; // a node that really is TRS rebuilds to itself
      }

      // Trusted only when it reconstructs what the file said. An
      // ill-conditioned linear part (a rig mixing 1e4 and 1e-4 scale on one
      // node) can lose enough precision in the round trip that the residual
      // is noise, and re-applying noise would be worse than the lossy
      // decomposition this repairs.
      const math::Mat4 reconstructed =
          math::buildModelMatrix(joint.translation, joint.rotation, joint.scale) * correction;
      float residual = 0.0f;
      float magnitude = 1.0f;
      for (int column = 0; column < 4; ++column)
      {
        for (int row = 0; row < 4; ++row)
        {
          residual = std::fmax(residual, std::fabs(reconstructed.m[column][row] - local.m[column][row]));
          magnitude = std::fmax(magnitude, std::fabs(local.m[column][row]));
        }
      }

      if (residual <= 1e-4f * magnitude)
      {
        joint.restCorrection = correction;
        joint.hasCorrection = true;
      }
    }

    /// Bind-pose transform that carries a mesh's stored vertices into model
    /// space.
    ///
    /// Vertices are kept exactly as the source file authored them, so a rigid
    /// mesh only reaches model space through the node that references it,
    /// while joints are always measured in model space. Anything that compares
    /// the two — auto-weighting, and the offset matrices apply_rig builds —
    /// has to apply this factor or it is comparing two different spaces.
    ///
    /// A mesh that arrived skinned has no such node: assimp folds the mesh's
    /// own placement into its bone offset matrices, so the transform lives in
    /// the palette instead, as `nodeGlobal * offset`. It is emphatically NOT
    /// one matrix per mesh. That product only agrees across a mesh's bones
    /// when the node hierarchy happens to sit at the bind pose, and plenty of
    /// files store the current pose there instead: on assimp's own
    /// animation_with_skeleton.fbx its 13 bones disagree by up to 74 units,
    /// and picking any one of them displaces 1664 of 4220 vertices by as much
    /// as 3.85 units. So blend per vertex — the same weighted sum the skinned
    /// shader and `ModelAsset::finalize()` already compute, which is exact for
    /// a consistent mesh and for an inconsistent one alike. (The blend divides
    /// by the total weight, which the shader does not: the importer always
    /// normalises to one, and a hand-built asset that does not is better
    /// placed than shrunk towards the origin.)
    ///
    /// Every entry collapses to the identity once `apply_rig` has baked the
    /// mesh (offset is then the inverse of the joint's own rest global), so
    /// applying a rig twice cannot bake twice.
    struct MeshSpace
    {
      /// Used when `boneToModel` is empty: a rigid mesh placed by one node.
      math::Mat4 uniform = math::Mat4::identity();
      /// `nodeGlobal * offset` per palette entry, for a mesh the palette
      /// places. Empty for a mesh its own node places.
      std::vector<math::Mat4> boneToModel;
      /// True when nothing has to be transformed at all.
      bool isIdentity = true;

      math::Mat4 forVertex(const ModelVertex &vertex) const
      {
        if (boneToModel.empty())
        {
          return uniform;
        }

        math::Mat4 blended; // zero-initialised
        float total = 0.0f;
        for (int k = 0; k < 4; ++k)
        {
          const float weight = vertex.boneWeights[k];
          if (!(weight > 0.0f) || vertex.boneIndices[k] >= boneToModel.size())
          {
            continue;
          }
          const math::Mat4 &bone = boneToModel[vertex.boneIndices[k]];
          for (int column = 0; column < 4; ++column)
          {
            for (int row = 0; row < 4; ++row)
            {
              blended.m[column][row] += bone.m[column][row] * weight;
            }
          }
          total += weight;
        }

        if (!(total > 0.0f) || !std::isfinite(total))
        {
          // Nothing places this vertex; treat it as already in model space
          // rather than collapsing it onto the origin.
          return math::Mat4::identity();
        }

        const float inverseTotal = 1.0f / total;
        for (int column = 0; column < 4; ++column)
        {
          for (int row = 0; row < 4; ++row)
          {
            blended.m[column][row] *= inverseTotal;
          }
        }
        return blended;
      }
    };

    MeshSpace mesh_space(const ModelAsset &asset, const std::vector<math::Mat4> &nodeGlobals,
                         const ModelMeshData &mesh)
    {
      MeshSpace space;
      if (mesh.nodeIndex >= 0 && mesh.nodeIndex < static_cast<int>(nodeGlobals.size()))
      {
        space.uniform = nodeGlobals[static_cast<std::size_t>(mesh.nodeIndex)];
        space.isIdentity = is_close_to_identity(space.uniform);
        return space;
      }

      space.boneToModel.reserve(asset.bones.size());
      for (const ModelBone &bone : asset.bones)
      {
        math::Mat4 toModel = math::Mat4::identity();
        if (bone.nodeIndex >= 0 && bone.nodeIndex < static_cast<int>(nodeGlobals.size()))
        {
          toModel = nodeGlobals[static_cast<std::size_t>(bone.nodeIndex)] * bone.offsetMatrix;
        }
        space.isIdentity = space.isIdentity && is_close_to_identity(toModel);
        space.boneToModel.push_back(toModel);
      }
      return space;
    }

    /// Normals transform by the inverse transpose, so multiply by the rows of
    /// the already-computed inverse rather than building a second matrix.
    math::Vec3 transform_normal(const math::Mat4 &inverseTransform, const math::Vec3 &normal)
    {
      const math::Vec3 transformed{
          inverseTransform.m[0][0] * normal.x + inverseTransform.m[0][1] * normal.y + inverseTransform.m[0][2] * normal.z,
          inverseTransform.m[1][0] * normal.x + inverseTransform.m[1][1] * normal.y + inverseTransform.m[1][2] * normal.z,
          inverseTransform.m[2][0] * normal.x + inverseTransform.m[2][1] * normal.y + inverseTransform.m[2][2] * normal.z};
      return transformed.lengthSquared() > 1e-12f ? transformed.normalized() : normal;
    }

    /// Move `mesh` into model space once and for all, and record that it is
    /// there. A rig's offset matrices are inverses of model-space joint rest
    /// transforms, so a mesh still carrying its node's placement would be
    /// relocated and rescaled by the rig; baking is the cheap half of the fix
    /// (the alternative is one palette entry per joint *and* bound mesh).
    void bake_mesh_into_model_space(ModelMeshData &mesh, const MeshSpace &space)
    {
      mesh.nodeIndex = -1;
      if (space.isIdentity)
      {
        return;
      }

      // A rigid mesh shares one matrix, so its inverse is worth hoisting; a
      // skinned one needs the vertex's own blend either way.
      const bool uniform = space.boneToModel.empty();
      const math::Mat4 uniformInverse = uniform ? space.uniform.inverse() : math::Mat4::identity();

      for (auto &vertex : mesh.vertices)
      {
        const math::Mat4 toModel = uniform ? space.uniform : space.forVertex(vertex);
        const math::Vec3 position = toModel.transformPoint({vertex.px, vertex.py, vertex.pz});
        vertex.px = position.x;
        vertex.py = position.y;
        vertex.pz = position.z;

        const math::Vec3 normal =
            transform_normal(uniform ? uniformInverse : toModel.inverse(), {vertex.nx, vertex.ny, vertex.nz});
        vertex.nx = normal.x;
        vertex.ny = normal.y;
        vertex.nz = normal.z;
      }
    }

    /// Joint order in which every parent precedes its children. Returns false
    /// when some joints never became resolvable (a parent cycle); `order`
    /// then holds only the joints that did resolve, and `resolved` marks them.
    bool build_resolution_order(const RigAsset &rig, std::vector<int> &order, std::vector<bool> &resolved)
    {
      const std::size_t count = rig.joints.size();
      order.clear();
      order.reserve(count);
      resolved.assign(count, false);

      // Repeated stable passes rather than a DFS: joints keep their authored
      // order within each wave, so sorting an already-sorted rig is a no-op
      // and the result never depends on hash iteration order.
      bool progress = true;
      while (order.size() < count && progress)
      {
        progress = false;
        for (std::size_t i = 0; i < count; ++i)
        {
          if (resolved[i])
          {
            continue;
          }
          // A parent outside the rig (a model node, or no parent at all) is
          // ready by definition; one inside it has to be emitted first.
          const int parent = rig.parent_index(i);
          if (parent >= 0 && !resolved[static_cast<std::size_t>(parent)])
          {
            continue;
          }
          resolved[i] = true;
          order.push_back(static_cast<int>(i));
          progress = true;
        }
      }

      return order.size() == count;
    }

    /// Inverse of a joint's model-space rest matrix.
    ///
    /// math::Mat4::inverse() answers identity for a singular matrix, and an
    /// identity offset would strand every vertex bound to the joint at the
    /// model origin, so a joint with a collapsed axis is rebuilt at unit
    /// scale before being inverted.
    math::Mat4 rest_offset_matrix(const math::Mat4 &restGlobal)
    {
      math::Vec3 translation;
      math::Quat rotation;
      math::Vec3 scale{1.0f, 1.0f, 1.0f};
      const bool decomposed = math::decomposeTRS(restGlobal, translation, rotation, scale);
      const bool degenerate = std::abs(scale.x) < kMinScale || scale.y < kMinScale || scale.z < kMinScale;
      if (decomposed && !degenerate)
      {
        return restGlobal.inverse();
      }

      const math::Vec3 safeScale{
          std::abs(scale.x) >= kMinScale ? scale.x : 1.0f,
          scale.y >= kMinScale ? scale.y : 1.0f,
          scale.z >= kMinScale ? scale.z : 1.0f};
      return math::buildModelMatrix(translation, rotation, safeScale).inverse();
    }

    /// Distance from `p` to the segment [a, b]. A bone is a segment, not a
    /// point, so envelope weights follow the whole limb instead of pooling
    /// around the joint head.
    float distance_to_segment(const math::Vec3 &p, const math::Vec3 &a, const math::Vec3 &b)
    {
      const math::Vec3 ab = b - a;
      const float lengthSquared = ab.lengthSquared();
      if (lengthSquared < 1e-12f)
      {
        return (p - a).length();
      }
      const float t = std::clamp((p - a).dot(ab) / lengthSquared, 0.0f, 1.0f);
      return (p - (a + ab * t)).length();
    }

    int nearest_joint(const std::vector<math::Vec3> &jointPositions, const math::Vec3 &position)
    {
      int best = -1;
      float bestDistanceSquared = 0.0f;
      for (std::size_t i = 0; i < jointPositions.size(); ++i)
      {
        const float distanceSquared = (jointPositions[i] - position).lengthSquared();
        if (best < 0 || distanceSquared < bestDistanceSquared)
        {
          best = static_cast<int>(i);
          bestDistanceSquared = distanceSquared;
        }
      }
      return best;
    }

    struct Influence
    {
      int joint = -1;
      float weight = 0.0f;
    };

    /// Keep the `keep` highest scores, best first. Ties go to the lower joint
    /// index (the comparison is strict), so auto-weighting is reproducible.
    void insert_influence(Influence *best, int keep, int joint, float score)
    {
      for (int slot = 0; slot < keep; ++slot)
      {
        if (score > best[slot].weight)
        {
          for (int k = keep - 1; k > slot; --k)
          {
            best[k] = best[k - 1];
          }
          best[slot] = Influence{joint, score};
          return;
        }
      }
    }

    /// Model-space rest position of every joint, plus the position its bone
    /// starts from (the parent joint, the parent model node, or the joint
    /// itself for a root).
    void joint_rest_positions(const ModelAsset &asset, const RigAsset &rig,
                              std::vector<math::Vec3> &jointPositions,
                              std::vector<math::Vec3> &parentPositions)
    {
      std::vector<math::Mat4> globals;
      rig.global_rest_transforms(asset, globals);

      std::vector<math::Mat4> nodeGlobals;
      asset.bindPoseNodeGlobals(nodeGlobals);

      jointPositions.assign(rig.joints.size(), math::Vec3{});
      parentPositions.assign(rig.joints.size(), math::Vec3{});
      for (std::size_t i = 0; i < rig.joints.size(); ++i)
      {
        jointPositions[i] = matrix_position(globals[i]);
        // A root bone has no segment; it degenerates to its own point.
        parentPositions[i] = jointPositions[i];

        const int rigParent = rig.parent_index(i);
        if (rigParent >= 0)
        {
          parentPositions[i] = matrix_position(globals[static_cast<std::size_t>(rigParent)]);
          continue;
        }

        const int node = find_model_node(asset, rig.joints[i].parent);
        if (node >= 0 && node < static_cast<int>(nodeGlobals.size()))
        {
          parentPositions[i] = matrix_position(nodeGlobals[static_cast<std::size_t>(node)]);
        }
      }
    }

    RigMeshBinding &binding_for_mesh(RigAsset &rig, std::uint32_t meshIndex)
    {
      for (auto &binding : rig.meshes)
      {
        if (binding.meshIndex == meshIndex)
        {
          return binding;
        }
      }
      RigMeshBinding created;
      created.meshIndex = meshIndex;
      rig.meshes.push_back(std::move(created));
      return rig.meshes.back();
    }

    void bind_mesh(const ModelMeshData &mesh, const MeshSpace &meshToModel,
                   const std::vector<math::Vec3> &jointPositions,
                   const std::vector<math::Vec3> &parentPositions,
                   AutoWeightMode mode, float falloff, int maxInfluences,
                   RigMeshBinding &binding)
    {
      const int keep = std::clamp(maxInfluences, 1, 4);
      const std::size_t vertexCount = mesh.vertices.size();
      binding.jointIndices.assign(vertexCount * 4, -1);
      binding.weights.assign(vertexCount * 4, 0.0f);

      // One knob, two meanings: an envelope radius in model units, or the
      // inverse-distance exponent. Both are clamped so a slider at zero (or
      // dragged absurdly high) cannot produce infinities.
      const float radius = std::max(falloff, 1e-4f);
      const float exponent = std::clamp(falloff, 0.0f, 16.0f);

      for (std::size_t v = 0; v < vertexCount; ++v)
      {
        const ModelVertex &vertex = mesh.vertices[v];
        // Joint positions are model-space, so the vertex has to be lifted out
        // of mesh space before any distance is measured. Transforming the
        // vertex rather than pulling the joints down into mesh space keeps
        // `falloff` in the model units its documentation promises, even when
        // the mesh node carries a scale.
        const math::Vec3 position =
            meshToModel.forVertex(vertex).transformPoint({vertex.px, vertex.py, vertex.pz});
        Influence best[4]{};

        switch (mode)
        {
        case AutoWeightMode::Rigid:
          best[0] = Influence{nearest_joint(jointPositions, position), 1.0f};
          break;

        case AutoWeightMode::Envelope:
          for (std::size_t j = 0; j < jointPositions.size(); ++j)
          {
            const float distance = distance_to_segment(position, parentPositions[j], jointPositions[j]);
            const float t = 1.0f - distance / radius;
            if (t <= 0.0f)
            {
              continue; // outside the envelope: this bone does not reach here
            }
            insert_influence(best, keep, static_cast<int>(j), t * t);
          }
          break;

        case AutoWeightMode::Smooth:
          for (std::size_t j = 0; j < jointPositions.size(); ++j)
          {
            const float distance = std::max((jointPositions[j] - position).length(), 1e-4f);
            const float score = std::min(1.0f / std::pow(distance, exponent), 1e12f);
            insert_influence(best, keep, static_cast<int>(j), score);
          }
          break;
        }

        float total = 0.0f;
        for (int k = 0; k < keep; ++k)
        {
          if (best[k].joint >= 0)
          {
            total += best[k].weight;
          }
        }

        if (!(total > 0.0f) || !std::isfinite(total))
        {
          // No bone reached this vertex. Anchoring it to the nearest joint is
          // wrong-ish but visible; leaving it unbound is invisible and worse.
          for (int k = 0; k < 4; ++k)
          {
            best[k] = Influence{};
          }
          best[0] = Influence{nearest_joint(jointPositions, position), 1.0f};
          total = 1.0f;
        }

        const std::size_t base = v * 4;
        const float inverseTotal = 1.0f / total;
        std::size_t slot = 0;
        for (int k = 0; k < keep; ++k)
        {
          if (best[k].joint < 0 || best[k].weight <= 0.0f)
          {
            continue;
          }
          binding.jointIndices[base + slot] = best[k].joint;
          binding.weights[base + slot] = best[k].weight * inverseTotal;
          ++slot;
        }
      }
    }
  }

  const char *auto_weight_mode_name(AutoWeightMode mode)
  {
    switch (mode)
    {
    case AutoWeightMode::Rigid:
      return "Rigid";
    case AutoWeightMode::Envelope:
      return "Envelope";
    case AutoWeightMode::Smooth:
      return "Smooth";
    }
    return "Rigid";
  }

  // ---- RigAsset ------------------------------------------------------------

  int RigAsset::find_joint(const std::string &name) const
  {
    if (name.empty())
    {
      return -1;
    }
    for (std::size_t i = 0; i < joints.size(); ++i)
    {
      if (joints[i].name == name)
      {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  int RigAsset::parent_index(std::size_t joint) const
  {
    if (joint >= joints.size())
    {
      return -1;
    }
    return find_joint(joints[joint].parent);
  }

  std::string RigAsset::unique_joint_name(const std::string &base) const
  {
    const std::string root = base.empty() ? std::string("bone") : base;
    if (find_joint(root) < 0)
    {
      return root;
    }

    // At most joints.size() names are taken, so a suffix in that range is
    // always free — the loop cannot run away.
    for (std::size_t suffix = 1; suffix <= joints.size() + 1; ++suffix)
    {
      std::string candidate = root + "_" + std::to_string(suffix);
      if (find_joint(candidate) < 0)
      {
        return candidate;
      }
    }
    return root;
  }

  void RigAsset::global_rest_transforms(const ModelAsset &asset, std::vector<math::Mat4> &out) const
  {
    out.assign(joints.size(), math::Mat4::identity());
    if (joints.empty())
    {
      return;
    }

    std::vector<math::Mat4> nodeGlobals;
    asset.bindPoseNodeGlobals(nodeGlobals);

    // A parent has to be resolved before its children. The rig on disk is
    // sorted, but one being edited may not be, so the order is derived here
    // instead of trusting the caller; `out` still follows this rig's own
    // joint indices.
    std::vector<int> order;
    std::vector<bool> resolved;
    if (!build_resolution_order(*this, order, resolved))
    {
      // Cyclic parents: resolve what is left in authored order so the editor
      // still gets finite, deterministic transforms to draw.
      for (std::size_t i = 0; i < joints.size(); ++i)
      {
        if (!resolved[i])
        {
          order.push_back(static_cast<int>(i));
        }
      }
    }

    for (const int index : order)
    {
      const std::size_t joint = static_cast<std::size_t>(index);
      math::Mat4 parentGlobal = math::Mat4::identity();

      const int rigParent = parent_index(joint);
      if (rigParent >= 0)
      {
        parentGlobal = out[static_cast<std::size_t>(rigParent)];
      }
      else
      {
        const int node = find_model_node(asset, joints[joint].parent);
        if (node >= 0 && node < static_cast<int>(nodeGlobals.size()))
        {
          parentGlobal = nodeGlobals[static_cast<std::size_t>(node)];
        }
      }

      out[joint] = parentGlobal * joint_local_matrix(joints[joint]);
    }
  }

  bool RigAsset::topological_sort()
  {
    std::vector<int> order;
    std::vector<bool> resolved;
    if (!build_resolution_order(*this, order, resolved))
    {
      return false; // cycle: nothing has been touched
    }

    std::vector<RigJoint> reordered;
    reordered.reserve(joints.size());
    std::vector<int> remap(joints.size(), -1);
    for (std::size_t i = 0; i < order.size(); ++i)
    {
      const std::size_t from = static_cast<std::size_t>(order[i]);
      remap[from] = static_cast<int>(i);
      reordered.push_back(joints[from]);
    }
    joints.swap(reordered);

    // Bindings address joints by index, so they have to move with them —
    // otherwise every skinned vertex would quietly attach to a different bone.
    for (auto &binding : meshes)
    {
      for (auto &index : binding.jointIndices)
      {
        index = (index >= 0 && index < static_cast<std::int32_t>(remap.size()))
                    ? remap[static_cast<std::size_t>(index)]
                    : -1;
      }
    }

    return true;
  }

  nlohmann::json RigAsset::to_json() const
  {
    nlohmann::json document;
    document["version"] = kFormatVersion;
    document["sourceModel"] = sourceModel;
    document["replaceImportedSkeleton"] = replaceImportedSkeleton;

    nlohmann::json jointArray = nlohmann::json::array();
    for (const auto &joint : joints)
    {
      nlohmann::json entry;
      entry["name"] = joint.name;
      entry["parent"] = joint.parent;
      entry["t"] = {joint.translation.x, joint.translation.y, joint.translation.z};
      entry["r"] = {joint.rotation.x, joint.rotation.y, joint.rotation.z, joint.rotation.w};
      entry["s"] = {joint.scale.x, joint.scale.y, joint.scale.z};

      // Both of these are import provenance, absent from every hand-made
      // joint and from every rig an older build wrote; writing them only when
      // they carry something keeps those files byte-identical and keeps the
      // readers' fallbacks (-1, identity) on the same meaning.
      if (joint.sourceNode >= 0)
      {
        entry["sourceNode"] = joint.sourceNode;
      }
      if (joint.hasCorrection)
      {
        nlohmann::json correction = nlohmann::json::array();
        for (int column = 0; column < 4; ++column)
        {
          for (int row = 0; row < 4; ++row)
          {
            correction.push_back(joint.restCorrection.m[column][row]);
          }
        }
        entry["restCorrection"] = std::move(correction);
      }

      jointArray.push_back(std::move(entry));
    }
    document["joints"] = std::move(jointArray);

    nlohmann::json meshArray = nlohmann::json::array();
    for (const auto &binding : meshes)
    {
      nlohmann::json entry;
      entry["meshIndex"] = binding.meshIndex;
      entry["indices"] = binding.jointIndices;
      entry["weights"] = binding.weights;
      meshArray.push_back(std::move(entry));
    }
    document["meshes"] = std::move(meshArray);

    return document;
  }

  bool RigAsset::from_json(const nlohmann::json &document, RigAsset &out, std::string *errorMessage)
  {
    if (!document.is_object())
    {
      setError(errorMessage, "rig document is not a JSON object");
      return false;
    }

    const int version = read_int(document, "version", kFormatVersion);
    if (version > kFormatVersion)
    {
      setError(errorMessage, "rig format version " + std::to_string(version) +
                                 " is newer than this build understands (" +
                                 std::to_string(kFormatVersion) + ")");
      return false;
    }

    RigAsset parsed;
    parsed.sourceModel = read_string(document, "sourceModel");
    parsed.replaceImportedSkeleton = read_bool(document, "replaceImportedSkeleton", true);

    const auto jointsIt = document.find("joints");
    if (jointsIt != document.end() && jointsIt->is_array())
    {
      for (const auto &entry : *jointsIt)
      {
        if (!entry.is_object())
        {
          continue;
        }

        RigJoint joint;
        joint.name = read_string(entry, "name");
        if (joint.name.empty())
        {
          continue; // nothing can be parented to an unnamed joint
        }
        joint.parent = read_string(entry, "parent");

        const auto translation = read_number_array<float>(entry, "t", 0.0f);
        if (translation.size() >= 3)
        {
          joint.translation = math::Vec3{translation[0], translation[1], translation[2]};
        }

        const auto rotation = read_number_array<float>(entry, "r", 0.0f);
        if (rotation.size() >= 4)
        {
          joint.rotation = math::Quat{rotation[0], rotation[1], rotation[2], rotation[3]};
        }

        const auto scale = read_number_array<float>(entry, "s", 1.0f);
        if (scale.size() >= 3)
        {
          joint.scale = math::Vec3{scale[0], scale[1], scale[2]};
        }

        // Absent in every rig written before this field existed, and in every
        // hand-made joint, so the fallback has to be the "no source node"
        // value rather than node 0 — which would merge the joint onto the
        // model's root. apply_rig re-validates it against the model anyway:
        // the file it was seeded from can be re-exported with fewer nodes.
        joint.sourceNode = std::max(read_int(entry, "sourceNode", -1), -1);

        const auto correction = read_number_array<float>(entry, "restCorrection", 0.0f);
        if (correction.size() >= 16)
        {
          for (int column = 0; column < 4; ++column)
          {
            for (int row = 0; row < 4; ++row)
            {
              joint.restCorrection.m[column][row] = correction[static_cast<std::size_t>(column * 4 + row)];
            }
          }
          // A stored identity is the same as none: skip the multiply rather
          // than trust a hand-edited file to have written exactly 1s and 0s.
          joint.hasCorrection = !is_close_to_identity(joint.restCorrection);
          if (!joint.hasCorrection)
          {
            joint.restCorrection = math::Mat4::identity();
          }
        }

        parsed.joints.push_back(std::move(joint));
      }
    }

    const auto meshesIt = document.find("meshes");
    if (meshesIt != document.end() && meshesIt->is_array())
    {
      for (const auto &entry : *meshesIt)
      {
        if (!entry.is_object())
        {
          continue;
        }

        RigMeshBinding binding;
        binding.meshIndex = static_cast<std::uint32_t>(std::max(read_int(entry, "meshIndex", 0), 0));
        // An unreadable slot becomes "unused" rather than joint 0, which would
        // silently glue vertices to the first bone.
        binding.jointIndices = read_number_array<std::int32_t>(entry, "indices", -1);
        binding.weights = read_number_array<float>(entry, "weights", 0.0f);

        // Both arrays are four slots per vertex; a truncated file is trimmed
        // back to whole vertices rather than rejected.
        const std::size_t slots =
            (std::min(binding.jointIndices.size(), binding.weights.size()) / 4) * 4;
        binding.jointIndices.resize(slots, -1);
        binding.weights.resize(slots, 0.0f);
        if (binding.empty())
        {
          continue;
        }

        // One binding per mesh. `binding_for_mesh` (auto-weighting) edits the
        // first match while apply_rig replays them in order and lets the last
        // one win, so a duplicated entry in a hand-edited file would make
        // re-weighting look like a silent no-op. Keep the first, drop the rest.
        const bool duplicate =
            std::any_of(parsed.meshes.begin(), parsed.meshes.end(),
                        [&](const RigMeshBinding &existing)
                        { return existing.meshIndex == binding.meshIndex; });
        if (duplicate)
        {
          continue;
        }

        parsed.meshes.push_back(std::move(binding));
      }
    }

    out = std::move(parsed);
    return true;
  }

  // ---- Applying ------------------------------------------------------------

  bool apply_rig(ModelAsset &asset, const RigAsset &rig, std::string *errorMessage)
  {
    if (rig.joints.empty())
    {
      // A rig nobody has authored yet: leave the import exactly as it is.
      return true;
    }

    // Work on a sorted copy. `ModelAsset::nodes` must stay parent-before-child
    // for the global-transform pass to be a single forward sweep, and
    // topological_sort() renumbers the bindings with the joints so the copy
    // stays self-consistent.
    RigAsset sorted = rig;
    if (!sorted.topological_sort())
    {
      setError(errorMessage, "rig hierarchy contains a parent cycle");
      return false;
    }

    for (const auto &joint : sorted.joints)
    {
      if (joint.parent.empty() || sorted.find_joint(joint.parent) >= 0 ||
          find_model_node(asset, joint.parent) >= 0)
      {
        continue;
      }
      setError(errorMessage,
               "rig joint '" + joint.name + "' has unknown parent '" + joint.parent + "'");
      return false;
    }

    for (const auto &binding : sorted.meshes)
    {
      if (binding.meshIndex < asset.meshes.size())
      {
        continue;
      }
      setError(errorMessage, "rig binds mesh " + std::to_string(binding.meshIndex) +
                                 " but the model has " + std::to_string(asset.meshes.size()) +
                                 (asset.meshes.size() == 1 ? " mesh" : " meshes"));
      return false;
    }

    // Resolve every joint against the *imported* hierarchy before anything is
    // mutated, so a refusal below still leaves the model untouched.
    //
    // A joint that came out of the import IS one of these nodes: the seeded
    // rig is a copy of the imported chain, and appending a twin builds a
    // second, shadow skeleton — every name-bound clip then resolves (first
    // occurrence wins) to the imported copy while the skin follows the
    // appended one, so the mesh freezes at its bind pose.
    const std::size_t importedNodeCount = asset.nodes.size();
    std::vector<int> nodeForJoint(sorted.joints.size(), -1);
    std::vector<int> parentNodeForJoint(sorted.joints.size(), -1);
    std::vector<bool> nodeClaimed(importedNodeCount, false);
    int nextAppendedNode = static_cast<int>(importedNodeCount);
    for (std::size_t i = 0; i < sorted.joints.size(); ++i)
    {
      // Which node this joint IS. The name only answers for a node that has
      // one and does not share it; the seed records the index precisely
      // because assimp hands out unnamed and duplicate-named nodes, whose
      // joints were renamed to stay addressable within the rig and so match
      // nothing here.
      //
      // The index is trusted whenever it is in range and no earlier joint has
      // taken that node — a rig outliving a re-export with fewer nodes must
      // never index past the end, and two joints must never collapse onto one
      // node because a file was hand-edited. The exception is a joint whose
      // exact name is still on some *other* node: a name that resolves is
      // better evidence than an index into a hierarchy that has moved, so
      // that case keeps the name match it had before this field existed.
      const int byName = find_model_node(asset, sorted.joints[i].name);
      const int recorded = sorted.joints[i].sourceNode;
      const bool recordedUsable = recorded >= 0 && recorded < static_cast<int>(importedNodeCount) &&
                                  !nodeClaimed[static_cast<std::size_t>(recorded)];
      const int existing =
          (recordedUsable && (byName < 0 || byName == recorded)) ? recorded : byName;

      nodeForJoint[i] = existing >= 0 ? existing : nextAppendedNode++;
      if (existing >= 0)
      {
        nodeClaimed[static_cast<std::size_t>(existing)] = true;
      }

      // topological_sort put parents first, so a rig parent already has its
      // node. A parent outside the rig resolves against the imported nodes.
      const int rigParent = sorted.parent_index(i);
      parentNodeForJoint[i] = rigParent >= 0
                                  ? nodeForJoint[static_cast<std::size_t>(rigParent)]
                                  : find_model_node(asset, sorted.joints[i].parent);

      // `ModelAsset::nodes` must stay parent-before-child: globals are one
      // forward sweep, and Skeleton::from_model turns a backward edge into a
      // silent re-parent to the root. Reusing a node keeps its low index, so a
      // joint moved under a newly appended one is the one case that can break
      // the order — refuse it with a message instead of emitting a hierarchy
      // that evaluates wrongly.
      if (parentNodeForJoint[i] >= nodeForJoint[i])
      {
        setError(errorMessage, "rig joint '" + sorted.joints[i].name + "' is parented under '" +
                                   sorted.joints[i].parent +
                                   "', which the model hierarchy evaluates after it");
        return false;
      }
    }

    // Which meshes the rig rebinds, and which joints anything is actually
    // weighted to. A seeded rig that nobody has bound yet must not claim a
    // palette entry per joint: on a 64-bone character that alone doubles the
    // palette past kMaxModelBones and the whole rig is refused.
    std::vector<bool> meshIsBound(asset.meshes.size(), false);
    std::vector<bool> jointNeedsSlot(sorted.joints.size(), sorted.replaceImportedSkeleton);
    bool anyMeshBound = false;
    for (const auto &binding : sorted.meshes)
    {
      const ModelMeshData &mesh = asset.meshes[binding.meshIndex];
      meshIsBound[binding.meshIndex] = true;
      anyMeshBound = true;

      const std::size_t slotCount = std::min(binding.jointIndices.size(), binding.weights.size());
      for (std::size_t v = 0; v < mesh.vertices.size(); ++v)
      {
        const std::size_t base = v * 4;
        for (std::size_t k = 0; k < 4 && base + k < slotCount; ++k)
        {
          const std::int32_t joint = binding.jointIndices[base + k];
          if (joint >= 0 && joint < static_cast<std::int32_t>(sorted.joints.size()) &&
              binding.weights[base + k] > 0.0f)
          {
            jointNeedsSlot[static_cast<std::size_t>(joint)] = true;
          }
        }
      }
    }

    // A palette entry an unbound mesh still points into has to keep its
    // imported offset matrix, so only an entry nothing unbound depends on can
    // be re-pointed at a rig joint.
    std::vector<bool> boneLocked(asset.bones.size(), false);
    for (std::size_t m = 0; m < asset.meshes.size(); ++m)
    {
      if (meshIsBound[m])
      {
        continue;
      }
      for (const auto &vertex : asset.meshes[m].vertices)
      {
        for (int k = 0; k < 4; ++k)
        {
          if (vertex.boneWeights[k] > 0.0f && vertex.boneIndices[k] < asset.bones.size())
          {
            boneLocked[vertex.boneIndices[k]] = true;
          }
        }
      }
    }

    const std::size_t retainedBones = sorted.replaceImportedSkeleton ? 0u : asset.bones.size();
    std::vector<int> paletteSlotForJoint(sorted.joints.size(), -1);
    std::vector<bool> jointAppendsBone(sorted.joints.size(), false);
    std::vector<bool> boneClaimed(asset.bones.size(), false);
    std::size_t appendedBones = 0;
    // A rig that binds a mesh always needs somewhere to anchor a vertex its
    // weights missed, even when every one of its bindings turned out empty.
    if (anyMeshBound && std::find(jointNeedsSlot.begin(), jointNeedsSlot.end(), true) == jointNeedsSlot.end())
    {
      jointNeedsSlot[0] = true;
    }

    for (std::size_t i = 0; i < sorted.joints.size(); ++i)
    {
      if (!jointNeedsSlot[i])
      {
        continue; // nothing is weighted to this joint; it is a node, not a bone
      }

      if (!sorted.replaceImportedSkeleton)
      {
        // Re-point the entry the merged node already owns rather than adding a
        // twin, which is what keeps seed -> save idempotent.
        for (std::size_t bone = 0; bone < asset.bones.size(); ++bone)
        {
          if (asset.bones[bone].nodeIndex == nodeForJoint[i] && !boneLocked[bone] && !boneClaimed[bone])
          {
            paletteSlotForJoint[i] = static_cast<int>(bone);
            boneClaimed[bone] = true;
            break;
          }
        }
      }

      if (paletteSlotForJoint[i] < 0)
      {
        jointAppendsBone[i] = true;
        paletteSlotForJoint[i] = static_cast<int>(retainedBones + appendedBones);
        ++appendedBones;
      }
    }

    const std::size_t requiredBones = retainedBones + appendedBones;
    if (requiredBones > kMaxModelBones)
    {
      setError(errorMessage, "rig needs " + std::to_string(requiredBones) +
                                 " bones (" + std::to_string(appendedBones) +
                                 " joints + " + std::to_string(retainedBones) +
                                 " kept) but kMaxModelBones is " + std::to_string(kMaxModelBones));
      return false;
    }

    // The space the imported vertices were authored in, captured before the
    // nodes and the palette below are rewritten — both feed mesh_space.
    std::vector<math::Mat4> importedNodeGlobals;
    asset.bindPoseNodeGlobals(importedNodeGlobals);
    std::vector<MeshSpace> meshToModel(asset.meshes.size());
    for (std::size_t m = 0; m < asset.meshes.size(); ++m)
    {
      if (meshIsBound[m])
      {
        meshToModel[m] = mesh_space(asset, importedNodeGlobals, asset.meshes[m]);
      }
    }

    // ---- Nothing above this point has touched `asset` ------------------------

    for (std::size_t i = 0; i < sorted.joints.size(); ++i)
    {
      const math::Mat4 local = joint_local_matrix(sorted.joints[i]);
      if (nodeForJoint[i] < static_cast<int>(importedNodeCount))
      {
        ModelNode &node = asset.nodes[static_cast<std::size_t>(nodeForJoint[i])];
        node.parent = parentNodeForJoint[i];
        node.localTransform = local;

        // The merged node answers to its joint's name. Clips bind by name, so
        // a node the rig drives under a different label is a node no key can
        // reach: an unnamed node gains the identity it never had, and a joint
        // renamed in the Rig tab stops leaving the Animate tree showing the
        // old one. Skipped when another node already answers to that name —
        // the merge must never manufacture the duplicate it exists to remove.
        if (!sorted.joints[i].name.empty() && node.name != sorted.joints[i].name)
        {
          const int clash = find_model_node(asset, sorted.joints[i].name);
          if (clash < 0 || clash == nodeForJoint[i])
          {
            node.name = sorted.joints[i].name;
          }
        }
        continue;
      }

      ModelNode node;
      node.name = sorted.joints[i].name;
      node.parent = parentNodeForJoint[i];
      node.localTransform = local;
      asset.nodes.push_back(std::move(node));
    }

    if (sorted.replaceImportedSkeleton)
    {
      asset.bones.clear();
    }

    // Rest globals of the hierarchy that now exists, not of the one the rig
    // resolved against before the merge. Re-pointing a node moves everything
    // under it, so a joint parented to a model node below another joint has a
    // different rest global after the merge than `global_rest_transforms`
    // reported before it — and an offset built from the stale value leaves
    // that bone non-identity at bind pose, which drags its vertices away.
    // Reading the node back also keeps two joints that collided on one node
    // agreeing with each other.
    std::vector<math::Mat4> mergedNodeGlobals;
    asset.bindPoseNodeGlobals(mergedNodeGlobals);

    for (std::size_t i = 0; i < sorted.joints.size(); ++i)
    {
      if (paletteSlotForJoint[i] < 0)
      {
        continue;
      }

      ModelBone bone;
      bone.nodeIndex = nodeForJoint[i];
      // The offset matrix carries a model-space vertex into joint space at
      // bind time, so it is the inverse of the joint's model-space rest
      // matrix: `globalJoint * offset` is then the identity at rest and the
      // skinning product `globalInverse * globalJoint * offset` leaves every
      // bind-pose vertex where it was authored. Every mesh the rig binds is
      // baked into model space below, which is what makes one offset per
      // joint serve all of them.
      bone.offsetMatrix = rest_offset_matrix(mergedNodeGlobals[static_cast<std::size_t>(nodeForJoint[i])]);

      if (jointAppendsBone[i])
      {
        asset.bones.push_back(bone);
      }
      else
      {
        // A re-pointed entry must have its offset recomputed too: the node's
        // rest global may have moved with the joint, and every vertex in that
        // slot would otherwise shift at rest.
        asset.bones[static_cast<std::size_t>(paletteSlotForJoint[i])] = bone;
      }
    }

    // Any vertex the rig fails to weight anchors here rather than on palette
    // slot 0, which in append mode is an imported bone that has nothing to do
    // with this rig and would drag the vertex around.
    std::uint32_t anchorSlot = 0;
    for (const int slot : paletteSlotForJoint)
    {
      if (slot >= 0)
      {
        anchorSlot = static_cast<std::uint32_t>(slot);
        break;
      }
    }

    std::vector<bool> rebound(asset.meshes.size(), false);
    bool boundAnyVertex = false;
    for (const auto &binding : sorted.meshes)
    {
      ModelMeshData &mesh = asset.meshes[binding.meshIndex];
      if (!rebound[binding.meshIndex])
      {
        bake_mesh_into_model_space(mesh, meshToModel[binding.meshIndex]);
        rebound[binding.meshIndex] = true;
      }
      const std::size_t slotCount = std::min(binding.jointIndices.size(), binding.weights.size());

      for (std::size_t v = 0; v < mesh.vertices.size(); ++v)
      {
        const std::size_t base = v * 4;
        std::uint32_t indices[4]{0, 0, 0, 0};
        float weights[4]{0.0f, 0.0f, 0.0f, 0.0f};
        int used = 0;
        float total = 0.0f;

        for (std::size_t k = 0; k < 4 && base + k < slotCount; ++k)
        {
          const std::int32_t joint = binding.jointIndices[base + k];
          const float weight = binding.weights[base + k];
          if (joint < 0 || joint >= static_cast<std::int32_t>(sorted.joints.size()) ||
              paletteSlotForJoint[static_cast<std::size_t>(joint)] < 0 || !(weight > 0.0f))
          {
            continue;
          }
          // Bindings index rig joints; the GPU indexes palette slots.
          indices[used] = static_cast<std::uint32_t>(paletteSlotForJoint[static_cast<std::size_t>(joint)]);
          weights[used] = weight;
          total += weight;
          ++used;
        }

        if (used == 0 || !(total > 0.0f) || !std::isfinite(total))
        {
          // Never leave a vertex unweighted: the shader sums weighted palette
          // matrices, so a zero total collapses the vertex onto the origin.
          indices[0] = anchorSlot;
          weights[0] = 1.0f;
          used = 1;
          total = 1.0f;
        }
        else
        {
          boundAnyVertex = true;
        }

        const float inverseTotal = 1.0f / total;
        ModelVertex &vertex = mesh.vertices[v];
        for (int k = 0; k < 4; ++k)
        {
          vertex.boneIndices[k] = k < used ? indices[k] : 0u;
          vertex.boneWeights[k] = k < used ? weights[k] * inverseTotal : 0.0f;
        }
      }
    }

    if (sorted.replaceImportedSkeleton)
    {
      // Meshes the rig does not bind keep their influences, but the palette
      // they pointed into is gone; anything past the new end would be an
      // out-of-bounds palette fetch on the GPU.
      const std::uint32_t paletteSize = static_cast<std::uint32_t>(asset.bones.size());
      for (std::size_t m = 0; m < asset.meshes.size(); ++m)
      {
        if (rebound[m])
        {
          continue;
        }
        for (auto &vertex : asset.meshes[m].vertices)
        {
          for (int k = 0; k < 4; ++k)
          {
            if (vertex.boneIndices[k] >= paletteSize)
            {
              vertex.boneIndices[k] = 0u;
            }
          }
        }
      }
    }

    if (boundAnyVertex)
    {
      // The model is genuinely skinned now, whatever the source file carried.
      asset.hasSkeleton = true;
    }

    asset.finalize();
    return true;
  }

  // ---- Auto weighting ------------------------------------------------------

  void compute_auto_weights(const ModelAsset &asset, RigAsset &rig,
                            AutoWeightMode mode, float falloff, int maxInfluences)
  {
    if (rig.joints.empty())
    {
      return;
    }

    std::vector<math::Vec3> jointPositions;
    std::vector<math::Vec3> parentPositions;
    joint_rest_positions(asset, rig, jointPositions, parentPositions);

    std::vector<math::Mat4> nodeGlobals;
    asset.bindPoseNodeGlobals(nodeGlobals);

    for (std::size_t m = 0; m < asset.meshes.size(); ++m)
    {
      RigMeshBinding &binding = binding_for_mesh(rig, static_cast<std::uint32_t>(m));
      bind_mesh(asset.meshes[m], mesh_space(asset, nodeGlobals, asset.meshes[m]),
                jointPositions, parentPositions, mode, falloff, maxInfluences, binding);
    }
  }

  void compute_auto_weights_for_mesh(const ModelAsset &asset, RigAsset &rig,
                                     std::uint32_t meshIndex, AutoWeightMode mode,
                                     float falloff, int maxInfluences)
  {
    if (rig.joints.empty() || meshIndex >= asset.meshes.size())
    {
      return;
    }

    std::vector<math::Vec3> jointPositions;
    std::vector<math::Vec3> parentPositions;
    joint_rest_positions(asset, rig, jointPositions, parentPositions);

    std::vector<math::Mat4> nodeGlobals;
    asset.bindPoseNodeGlobals(nodeGlobals);

    const ModelMeshData &mesh = asset.meshes[meshIndex];
    RigMeshBinding &binding = binding_for_mesh(rig, meshIndex);
    bind_mesh(mesh, mesh_space(asset, nodeGlobals, mesh), jointPositions, parentPositions,
              mode, falloff, maxInfluences, binding);
  }

  RigAsset rig_from_model(const ModelAsset &asset, const std::string &sourceModel)
  {
    RigAsset rig;
    rig.sourceModel = sourceModel;
    // The import already skins these joints; replacing the skeleton would
    // throw those weights away, so the seeded rig only overlays it.
    rig.replaceImportedSkeleton = false;

    // A full node dump is unusable in the UI, so keep only nodes that skin
    // vertices plus the chain above them — a hierarchy with a gap in it
    // cannot be re-parented by name.
    std::vector<bool> keep(asset.nodes.size(), false);
    for (const auto &bone : asset.bones)
    {
      int node = bone.nodeIndex;
      while (node >= 0 && node < static_cast<int>(asset.nodes.size()) && !keep[static_cast<std::size_t>(node)])
      {
        keep[static_cast<std::size_t>(node)] = true;
        node = asset.nodes[static_cast<std::size_t>(node)].parent;
      }
    }

    std::vector<std::string> nameForNode(asset.nodes.size());
    for (std::size_t i = 0; i < asset.nodes.size(); ++i)
    {
      if (!keep[i])
      {
        continue;
      }

      RigJoint joint;
      // Joints are addressed by name inside the rig (parents, the editor's
      // lists), so every one needs a name of its own even when the node it
      // came from has none or shares one. `sourceNode` is what carries the
      // identity the renaming loses: apply_rig merges onto that node instead
      // of appending a shadow twin for it.
      joint.name = rig.unique_joint_name(asset.nodes[i].name.empty() ? std::string("joint")
                                                                    : asset.nodes[i].name);
      joint.sourceNode = static_cast<int>(i);
      nameForNode[i] = joint.name;

      const int parent = asset.nodes[i].parent;
      if (parent >= 0 && parent < static_cast<int>(i) && keep[static_cast<std::size_t>(parent)])
      {
        joint.parent = nameForNode[static_cast<std::size_t>(parent)];
      }

      decompose_into_joint(asset.nodes[i].localTransform, joint);
      rig.joints.push_back(std::move(joint));
    }

    return rig;
  }

  // ---- Storage -------------------------------------------------------------

  std::filesystem::path rig_path_for_model(const std::filesystem::path &assetRoot,
                                           const std::string &modelReference)
  {
    // Flatten the whole reference, not just its file name: two models called
    // "character.glb" in different folders must not share a rig.
    std::string flattened = modelReference;
    for (char &c : flattened)
    {
      if (c == '/' || c == '\\' || c == ':' || c == ' ')
      {
        c = '_';
      }
    }
    if (flattened.empty())
    {
      flattened = "model";
    }

    return assetRoot / ".hades" / "rigs" / (flattened + ".json");
  }

  bool save_rig_asset(const std::filesystem::path &path, const RigAsset &rig, std::string *errorMessage)
  {
    const std::filesystem::path directory = path.parent_path();
    if (!directory.empty())
    {
      std::error_code ec;
      std::filesystem::create_directories(directory, ec);
      if (ec)
      {
        setError(errorMessage, "Failed to create directory " + directory.string() + ": " + ec.message());
        return false;
      }
    }

    std::ofstream file(path);
    if (!file.is_open())
    {
      setError(errorMessage, "Failed to open file for writing: " + path.string());
      return false;
    }

    // `replace` rather than the default strict handler: joint names come from
    // node names in the source file, which are raw bytes and are regularly not
    // valid UTF-8 (older FBX exporters emit Latin-1). Strict dumping throws
    // type_error.316 for those, and this function promises a bool plus a
    // message, not an exception through the editor's save path.
    file << rig.to_json().dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    file.close();
    if (file.fail())
    {
      setError(errorMessage, "Failed to write data to: " + path.string());
      return false;
    }

    return true;
  }

  bool load_rig_asset(const std::filesystem::path &path, RigAsset &out, std::string *errorMessage)
  {
    std::ifstream file(path);
    if (!file.is_open())
    {
      setError(errorMessage, "Failed to open file: " + path.string());
      return false;
    }

    // allow_exceptions = false: a hand-edited rig should report an error, not
    // throw through the asset cache.
    const nlohmann::json document = nlohmann::json::parse(file, nullptr, false);
    if (document.is_discarded())
    {
      setError(errorMessage, "Not valid JSON: " + path.string());
      return false;
    }

    return RigAsset::from_json(document, out, errorMessage);
  }
}
