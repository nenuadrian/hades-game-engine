#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "test_support.hpp"

#include "../engine/animation/rig_asset.hpp"
#include "../engine/animation/skeleton.hpp"
#include "../engine/assets/model_asset.hpp"
#include "../engine/assets/model_asset_cache.hpp"

namespace hades
{
  using test_support::ScopedDirectoryCleanup;
  using test_support::unique_test_directory;

  namespace
  {
    constexpr float kTolerance = 1e-4f;

    bool vec3_close(const math::Vec3 &a, const math::Vec3 &b, float tolerance = kTolerance)
    {
      return std::fabs(a.x - b.x) <= tolerance &&
             std::fabs(a.y - b.y) <= tolerance &&
             std::fabs(a.z - b.z) <= tolerance;
    }

    /// q and -q are the same rotation, so closeness is an absolute dot.
    bool quat_close(const math::Quat &a, const math::Quat &b, float tolerance = kTolerance)
    {
      const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
      return std::fabs(std::fabs(dot) - 1.0f) <= tolerance;
    }

    bool mat4_close_to_identity(const math::Mat4 &matrix, float tolerance = kTolerance)
    {
      for (int column = 0; column < 4; ++column)
      {
        for (int row = 0; row < 4; ++row)
        {
          const float expected = column == row ? 1.0f : 0.0f;
          if (std::fabs(matrix.m[column][row] - expected) > tolerance)
          {
            return false;
          }
        }
      }
      return true;
    }

    bool mat4_close(const math::Mat4 &a, const math::Mat4 &b, float tolerance = kTolerance)
    {
      for (int column = 0; column < 4; ++column)
      {
        for (int row = 0; row < 4; ++row)
        {
          if (std::fabs(a.m[column][row] - b.m[column][row]) > tolerance)
          {
            return false;
          }
        }
      }
      return true;
    }

    int find_node(const ModelAsset &asset, const std::string &name)
    {
      for (std::size_t i = 0; i < asset.nodes.size(); ++i)
      {
        if (asset.nodes[i].name == name)
        {
          return static_cast<int>(i);
        }
      }
      return -1;
    }

    /// A tall unrigged quad — the case a user rigs by hand in the editor.
    /// Two vertices sit at the base and two four units up, so a two-joint rig
    /// has an unambiguous nearest joint for each of them.
    ModelAsset make_unrigged_quad()
    {
      ModelAsset asset;
      asset.nodes.push_back({"Quad", -1, math::Mat4::identity()});

      const float positions[4][3] = {{0.0f, 0.0f, 0.0f},
                                     {1.0f, 0.0f, 0.0f},
                                     {1.0f, 4.0f, 0.0f},
                                     {0.0f, 4.0f, 0.0f}};

      ModelMeshData mesh;
      for (const auto &position : positions)
      {
        ModelVertex vertex{};
        vertex.px = position[0];
        vertex.py = position[1];
        vertex.pz = position[2];
        vertex.nz = 1.0f;
        // The rigid-import shape: everything on one pseudo-bone.
        vertex.boneIndices[0] = 0;
        vertex.boneWeights[0] = 1.0f;
        mesh.vertices.push_back(vertex);
      }
      mesh.indices = {0, 1, 2, 0, 2, 3};
      mesh.nodeIndex = 0;
      asset.meshes.push_back(std::move(mesh));

      asset.bones.push_back({0, math::Mat4::identity()});
      asset.hasSkeleton = false;
      asset.finalize();
      return asset;
    }

    /// The same quad, but hanging off a node that carries a placement of its
    /// own — the shape almost every real file has (13 of the 14 loadable
    /// assimp FBX samples), and the one no fixture used to cover. Its vertices
    /// are still mesh-local, so they only reach model space (0,10,0)..(2,18,0)
    /// through the "Quad" node.
    ModelAsset make_unrigged_quad_under_a_placed_node()
    {
      ModelAsset asset = make_unrigged_quad();
      asset.nodes.clear();
      asset.nodes.push_back({"Root", -1, math::Mat4::identity()});
      asset.nodes.push_back({"Quad", 0,
                             math::buildModelMatrix({0.0f, 10.0f, 0.0f}, math::Quat{}, {2.0f, 2.0f, 2.0f})});
      asset.meshes[0].nodeIndex = 1;
      asset.bones[0] = {1, math::Mat4::identity()};
      asset.finalize();
      return asset;
    }

    /// Bind-pose position of one vertex, skinned exactly the way the GPU does.
    math::Vec3 skinned_bind_position(const ModelAsset &asset, std::size_t meshIndex, std::size_t vertexIndex)
    {
      const ModelVertex &vertex = asset.meshes[meshIndex].vertices[vertexIndex];
      const std::vector<math::Mat4> &palette = asset.bindPose();
      math::Vec3 skinned{0.0f, 0.0f, 0.0f};
      float total = 0.0f;
      for (int slot = 0; slot < 4; ++slot)
      {
        if (vertex.boneWeights[slot] <= 0.0f || vertex.boneIndices[slot] >= palette.size())
        {
          continue;
        }
        skinned = skinned + palette[vertex.boneIndices[slot]].transformPoint({vertex.px, vertex.py, vertex.pz}) *
                                vertex.boneWeights[slot];
        total += vertex.boneWeights[slot];
      }
      return total > 0.0f ? skinned : math::Vec3{vertex.px, vertex.py, vertex.pz};
    }

    int duplicate_node_names(const ModelAsset &asset)
    {
      int duplicates = 0;
      for (std::size_t i = 0; i < asset.nodes.size(); ++i)
      {
        for (std::size_t j = i + 1; j < asset.nodes.size(); ++j)
        {
          duplicates += asset.nodes[i].name == asset.nodes[j].name ? 1 : 0;
        }
      }
      return duplicates;
    }

    /// Weight every vertex of mesh 0 to the joint nearest it in model space:
    /// the lower half to `j_root` at y=10, the upper half to `j_tip` at y=18.
    void bind_quad_by_height(const ModelAsset &asset, RigAsset &rig)
    {
      RigMeshBinding binding;
      binding.meshIndex = 0;
      for (const auto &vertex : asset.meshes[0].vertices)
      {
        const bool upper = vertex.py > 2.0f;
        binding.jointIndices.push_back(upper ? 1 : 0);
        binding.jointIndices.push_back(-1);
        binding.jointIndices.push_back(-1);
        binding.jointIndices.push_back(-1);
        binding.weights.push_back(1.0f);
        binding.weights.push_back(0.0f);
        binding.weights.push_back(0.0f);
        binding.weights.push_back(0.0f);
      }
      rig.meshes.push_back(std::move(binding));
    }

    /// A mesh that arrived already skinned, by two bones that do NOT agree on
    /// where the mesh sits.
    ///
    /// That is not a corrupt file: plenty of exporters leave the node
    /// hierarchy at the *current* pose and put the bind pose only in the bone
    /// offset matrices, so `nodeGlobal * offset` comes out different per bone.
    /// assimp's own animation_with_skeleton.fbx does exactly this — its 13
    /// bones disagree by up to 74 units — which is why no single mesh-to-model
    /// matrix can place such a mesh.
    ///
    /// Bone "Lower" places the mesh at the origin, bone "Upper" four units up,
    /// so the three vertices render at y = 0, +4 and +2 relative to their
    /// stored positions.
    ModelAsset make_skinned_mesh_whose_bones_disagree()
    {
      ModelAsset asset;
      asset.nodes.push_back({"Root", -1, math::Mat4::identity()});
      asset.nodes.push_back({"Lower", 0, math::Mat4::translate({0.0f, 3.0f, 0.0f})});
      asset.nodes.push_back({"Upper", 1, math::Mat4::translate({0.0f, 5.0f, 0.0f})});

      // offset = inverse(nodeGlobal) * placement, the assimp convention.
      asset.bones.push_back({1, math::Mat4::translate({0.0f, -3.0f, 0.0f})});
      asset.bones.push_back({2, math::Mat4::translate({0.0f, -4.0f, 0.0f})});

      ModelMeshData mesh;
      const float heights[3] = {0.0f, 1.0f, 2.0f};
      for (int i = 0; i < 3; ++i)
      {
        ModelVertex vertex{};
        vertex.py = heights[i];
        vertex.nz = 1.0f;
        vertex.boneIndices[0] = 0;
        vertex.boneIndices[1] = 1;
        // Vertex 0 is bone 0 only, vertex 1 bone 1 only, vertex 2 an even
        // blend — so no single bone's placement is right for all three.
        vertex.boneWeights[0] = i == 0 ? 1.0f : (i == 1 ? 0.0f : 0.5f);
        vertex.boneWeights[1] = 1.0f - vertex.boneWeights[0];
        mesh.vertices.push_back(vertex);
      }
      mesh.indices = {0, 1, 2};
      // Skinned meshes carry no node: the palette places them.
      mesh.nodeIndex = -1;
      asset.meshes.push_back(std::move(mesh));

      asset.hasSkeleton = true;
      asset.finalize();
      return asset;
    }

    /// Root at the origin with a tip four units above it, matching the quad.
    RigAsset make_two_joint_rig()
    {
      RigAsset rig;
      rig.sourceModel = "props/quad.obj";
      rig.replaceImportedSkeleton = true;

      RigJoint root;
      root.name = "j_root";

      RigJoint tip;
      tip.name = "j_tip";
      tip.parent = "j_root";
      tip.translation = {0.0f, 4.0f, 0.0f};

      rig.joints = {root, tip};
      return rig;
    }

    /// Two quads hanging off a root, carrying the node names real exports
    /// actually have. assimp leaves plenty of nodes unnamed (its own
    /// cubes_nonames.fbx has four) and plenty of files name two different
    /// nodes the same (cubes_with_names.fbx has two "Куб1"); neither can be
    /// found again by name, so a rig seeded from one has to remember which
    /// node each joint came from.
    ModelAsset make_two_quads(const std::string &firstName, const std::string &secondName)
    {
      ModelAsset asset;
      asset.nodes.push_back({"RootNode", -1, math::Mat4::identity()});
      asset.nodes.push_back({firstName, 0, math::Mat4::translate({-3.0f, 0.0f, 0.0f})});
      asset.nodes.push_back({secondName, 0, math::Mat4::translate({3.0f, 1.0f, 0.0f})});

      const float corners[4][3] = {{0.0f, 0.0f, 0.0f},
                                   {1.0f, 0.0f, 0.0f},
                                   {1.0f, 1.0f, 0.0f},
                                   {0.0f, 1.0f, 0.0f}};
      for (int quad = 0; quad < 2; ++quad)
      {
        ModelMeshData mesh;
        for (const auto &corner : corners)
        {
          ModelVertex vertex{};
          vertex.px = corner[0];
          vertex.py = corner[1];
          vertex.pz = corner[2];
          vertex.nz = 1.0f;
          vertex.boneIndices[0] = static_cast<std::uint32_t>(quad);
          vertex.boneWeights[0] = 1.0f;
          mesh.vertices.push_back(vertex);
        }
        mesh.indices = {0, 1, 2, 0, 2, 3};
        mesh.nodeIndex = quad + 1;
        asset.meshes.push_back(std::move(mesh));
        // The rigid-import shape: one pseudo-bone at the mesh's own node with
        // an identity offset, so the node global is what places the mesh.
        asset.bones.push_back({quad + 1, math::Mat4::identity()});
      }

      asset.hasSkeleton = false;
      asset.finalize();
      return asset;
    }

    std::vector<math::Vec3> skinned_bind_positions(const ModelAsset &asset)
    {
      std::vector<math::Vec3> positions;
      for (std::size_t mesh = 0; mesh < asset.meshes.size(); ++mesh)
      {
        for (std::size_t vertex = 0; vertex < asset.meshes[mesh].vertices.size(); ++vertex)
        {
          positions.push_back(skinned_bind_position(asset, mesh, vertex));
        }
      }
      return positions;
    }

    /// The Rig tab's whole journey, three times over: seed from the model,
    /// bind every mesh, save, and let the next load apply what was saved.
    /// Nothing about the model may grow, because every joint of a seeded rig
    /// already IS one of its nodes.
    void expect_seeding_is_idempotent(ModelAsset &asset)
    {
      const std::size_t nodesBefore = asset.nodes.size();
      const math::Vec3 boundsMin = asset.boundsMin();
      const math::Vec3 boundsMax = asset.boundsMax();
      const std::vector<math::Vec3> importedVertices = skinned_bind_positions(asset);
      std::size_t bonesAfterFirstRound = 0;

      for (int round = 0; round < 3; ++round)
      {
        RigAsset rig = rig_from_model(asset, "props/quads.fbx");
        compute_auto_weights(asset, rig, AutoWeightMode::Rigid, 4.0f, 4);

        // Through the document, because that is the only path the editor has:
        // the panel saves the rig and the cache applies whatever it reads
        // back, so a source node that does not survive serialisation is a
        // source node apply_rig never sees.
        RigAsset saved;
        std::string error;
        ASSERT_TRUE(RigAsset::from_json(rig.to_json(), saved, &error)) << error;
        ASSERT_TRUE(apply_rig(asset, saved, &error)) << error;

        EXPECT_EQ(asset.nodes.size(), nodesBefore) << "round " << round;
        // A shadow node would double the palette too, which is what puts a
        // 64-bone character over kMaxModelBones and gets the rig refused.
        EXPECT_LE(asset.bones.size(), nodesBefore) << "round " << round;
        if (round == 0)
        {
          bonesAfterFirstRound = asset.bones.size();
        }
        else
        {
          EXPECT_EQ(asset.bones.size(), bonesAfterFirstRound) << "round " << round;
        }

        // Every merged node answers to exactly one joint, so a name-bound key
        // has one place to land.
        EXPECT_EQ(duplicate_node_names(asset), 0) << "round " << round;

        EXPECT_TRUE(vec3_close(asset.boundsMin(), boundsMin)) << "round " << round;
        EXPECT_TRUE(vec3_close(asset.boundsMax(), boundsMax)) << "round " << round;
        const std::vector<math::Vec3> now = skinned_bind_positions(asset);
        ASSERT_EQ(now.size(), importedVertices.size());
        for (std::size_t i = 0; i < now.size(); ++i)
        {
          EXPECT_TRUE(vec3_close(now[i], importedVertices[i]))
              << "round " << round << ", vertex " << i;
        }
      }
    }
  }

  TEST(RigAssetTest, TopologicalSortOrdersParentsFirstAndReportsACycle)
  {
    RigAsset rig;
    RigJoint tip;
    tip.name = "tip";
    tip.parent = "mid";
    RigJoint mid;
    mid.name = "mid";
    mid.parent = "root";
    RigJoint root;
    root.name = "root";
    // Deliberately child-first: the sort is what makes the hierarchy usable.
    rig.joints = {tip, mid, root};

    // Bindings address joints by index, so they must travel with the joints.
    RigMeshBinding binding;
    binding.meshIndex = 0;
    binding.jointIndices = {0, 2, -1, -1};
    binding.weights = {0.5f, 0.5f, 0.0f, 0.0f};
    rig.meshes.push_back(binding);

    ASSERT_TRUE(rig.topological_sort());

    const int rootIndex = rig.find_joint("root");
    const int midIndex = rig.find_joint("mid");
    const int tipIndex = rig.find_joint("tip");
    ASSERT_GE(rootIndex, 0);
    ASSERT_GE(midIndex, 0);
    ASSERT_GE(tipIndex, 0);
    EXPECT_LT(rootIndex, midIndex);
    EXPECT_LT(midIndex, tipIndex);
    EXPECT_EQ(rig.find_joint("nobody"), -1);

    EXPECT_EQ(rig.parent_index(static_cast<std::size_t>(rootIndex)), -1);
    EXPECT_EQ(rig.parent_index(static_cast<std::size_t>(midIndex)), rootIndex);
    EXPECT_EQ(rig.parent_index(static_cast<std::size_t>(tipIndex)), midIndex);

    // The old slot 0 was "tip" and the old slot 2 was "root".
    ASSERT_EQ(rig.meshes.size(), 1U);
    EXPECT_EQ(rig.meshes[0].jointIndices[0], tipIndex);
    EXPECT_EQ(rig.meshes[0].jointIndices[1], rootIndex);
    // An unused slot must stay unused: remapping -1 through the permutation
    // would hand the skinning palette an index nothing bounds-checks.
    EXPECT_EQ(rig.meshes[0].jointIndices[2], -1);
    EXPECT_EQ(rig.meshes[0].jointIndices[3], -1);

    // Sorting an already sorted rig changes nothing.
    const std::string firstName = rig.joints[0].name;
    ASSERT_TRUE(rig.topological_sort());
    EXPECT_EQ(rig.joints[0].name, firstName);

    RigAsset cyclic;
    RigJoint a;
    a.name = "a";
    a.parent = "b";
    RigJoint b;
    b.name = "b";
    b.parent = "a";
    cyclic.joints = {a, b};
    EXPECT_FALSE(cyclic.topological_sort());
    // A cycle leaves the rig untouched rather than half-sorted: a rig that
    // lost joints on a failed sort would silently unbind whatever was skinned
    // to them.
    ASSERT_EQ(cyclic.joints.size(), 2U);
    EXPECT_EQ(cyclic.joints[0].name, "a");
    EXPECT_EQ(cyclic.joints[1].name, "b");
  }

  TEST(RigAssetTest, UniqueJointNameAvoidsCollisionsWithExistingJoints)
  {
    RigAsset rig = make_two_joint_rig();
    EXPECT_EQ(rig.unique_joint_name("spine"), "spine");
    EXPECT_EQ(rig.unique_joint_name("j_root"), "j_root_1");
    EXPECT_FALSE(rig.unique_joint_name("").empty());
    EXPECT_TRUE(RigAsset{}.empty());
    EXPECT_FALSE(rig.empty());
  }

  TEST(RigAssetTest, ApplyRigSkinsAnUnriggedModelAndLeavesTheBindPoseAtIdentity)
  {
    ModelAsset asset = make_unrigged_quad();
    RigAsset rig = make_two_joint_rig();

    // The rig resolves against the model before it is applied.
    std::vector<math::Mat4> restGlobals;
    rig.global_rest_transforms(asset, restGlobals);
    ASSERT_EQ(restGlobals.size(), 2U);
    EXPECT_NEAR(restGlobals[0].m[3][1], 0.0f, kTolerance);
    EXPECT_NEAR(restGlobals[1].m[3][1], 4.0f, kTolerance);

    // Deliberately unnormalised: normalisation is apply_rig's job.
    RigMeshBinding binding;
    binding.meshIndex = 0;
    for (const auto &vertex : asset.meshes[0].vertices)
    {
      const bool upper = vertex.py > 2.0f;
      binding.jointIndices.push_back(upper ? 1 : 0);
      binding.jointIndices.push_back(upper ? 0 : 1);
      binding.jointIndices.push_back(-1);
      binding.jointIndices.push_back(-1);
      binding.weights.push_back(3.0f);
      binding.weights.push_back(1.0f);
      binding.weights.push_back(0.0f);
      binding.weights.push_back(0.0f);
    }
    rig.meshes.push_back(binding);

    std::string error;
    ASSERT_TRUE(apply_rig(asset, rig, &error)) << error;

    // Joints became nodes, and every joint drives a palette slot.
    EXPECT_TRUE(asset.hasSkeleton);
    EXPECT_NE(find_node(asset, "j_root"), -1);
    EXPECT_NE(find_node(asset, "j_tip"), -1);
    ASSERT_EQ(asset.bones.size(), rig.joints.size());
    EXPECT_LE(asset.bones.size(), static_cast<std::size_t>(kMaxModelBones));

    // Every vertex is fully weighted: the shader sums weighted palette
    // matrices, so a total below one shrinks the mesh towards the origin.
    for (const auto &vertex : asset.meshes[0].vertices)
    {
      float total = 0.0f;
      for (int slot = 0; slot < 4; ++slot)
      {
        EXPECT_GE(vertex.boneWeights[slot], 0.0f);
        EXPECT_LT(vertex.boneIndices[slot], static_cast<std::uint32_t>(asset.bones.size()));
        total += vertex.boneWeights[slot];
      }
      EXPECT_NEAR(total, 1.0f, kTolerance);
    }
    EXPECT_NEAR(asset.meshes[0].vertices[0].boneWeights[0], 0.75f, kTolerance);

    // The rig must not move the mesh: at bind time every palette matrix is
    // the identity, so a freshly rigged model renders exactly as it did.
    const std::vector<math::Mat4> &palette = asset.bindPose();
    ASSERT_EQ(palette.size(), asset.bones.size());
    for (std::size_t bone = 0; bone < palette.size(); ++bone)
    {
      EXPECT_TRUE(mat4_close_to_identity(palette[bone])) << "bone " << bone << " moves the mesh at bind time";
    }

    // An empty rig is not an error — it just means nobody has authored one.
    ModelAsset untouched = make_unrigged_quad();
    EXPECT_TRUE(apply_rig(untouched, RigAsset{}, &error));
    EXPECT_EQ(untouched.bones.size(), 1U);
  }

  TEST(RigAssetTest, ApplyRigLeavesAModelPlacedByItsMeshNodeExactlyWhereItWas)
  {
    // A rig's offset matrices invert model-space joint transforms, so a mesh
    // that only reaches model space through its own node used to be relocated
    // and rescaled by the act of saving a rig — on this fixture it snapped
    // from (0,10,0)..(2,18,0) back onto the origin at half size.
    for (const bool replaceImportedSkeleton : {true, false})
    {
      ModelAsset asset = make_unrigged_quad_under_a_placed_node();
      const math::Vec3 boundsMin = asset.boundsMin();
      const math::Vec3 boundsMax = asset.boundsMax();
      ASSERT_TRUE(vec3_close(boundsMin, {0.0f, 10.0f, 0.0f}));
      ASSERT_TRUE(vec3_close(boundsMax, {2.0f, 18.0f, 0.0f}));

      std::vector<math::Vec3> before;
      for (std::size_t vertex = 0; vertex < asset.meshes[0].vertices.size(); ++vertex)
      {
        before.push_back(skinned_bind_position(asset, 0, vertex));
      }

      RigAsset rig = make_two_joint_rig();
      rig.replaceImportedSkeleton = replaceImportedSkeleton;
      // Authored where the mesh actually is, which is what the rig editor
      // shows: the joints straddle the quad in model space.
      rig.joints[0].translation = {0.0f, 10.0f, 0.0f};
      rig.joints[1].translation = {0.0f, 8.0f, 0.0f};
      bind_quad_by_height(asset, rig);

      std::string error;
      ASSERT_TRUE(apply_rig(asset, rig, &error)) << error;

      // The rig must not move the mesh, whatever node placed it.
      EXPECT_TRUE(vec3_close(asset.boundsMin(), boundsMin))
          << "replaceImportedSkeleton=" << replaceImportedSkeleton;
      EXPECT_TRUE(vec3_close(asset.boundsMax(), boundsMax))
          << "replaceImportedSkeleton=" << replaceImportedSkeleton;
      for (std::size_t vertex = 0; vertex < before.size(); ++vertex)
      {
        EXPECT_TRUE(vec3_close(skinned_bind_position(asset, 0, vertex), before[vertex]))
            << "vertex " << vertex << " moved when the rig was applied";
      }

      // ... and every palette slot the mesh now reads is the identity, so the
      // rig contributes nothing until a clip poses it. (In append mode the
      // model keeps the imported pseudo-bone as a slot nothing references any
      // more, which is why this asks the vertices rather than the palette.)
      for (const auto &vertex : asset.meshes[0].vertices)
      {
        for (int slot = 0; slot < 4; ++slot)
        {
          if (vertex.boneWeights[slot] <= 0.0f)
          {
            continue;
          }
          ASSERT_LT(vertex.boneIndices[slot], asset.bindPose().size());
          EXPECT_TRUE(mat4_close_to_identity(asset.bindPose()[vertex.boneIndices[slot]]))
              << "bone " << vertex.boneIndices[slot] << " moves the mesh at bind time";
        }
      }
    }
  }

  TEST(RigAssetTest, ASeededRigAppliesOntoTheImportedNodesInsteadOfShadowingThem)
  {
    ModelAsset asset = make_unrigged_quad_under_a_placed_node();
    const std::size_t nodesBefore = asset.nodes.size();
    const math::Vec3 boundsMin = asset.boundsMin();
    const math::Vec3 boundsMax = asset.boundsMax();

    // Exactly the Rig tab's journey: seed from the import, bind every mesh,
    // save, and let the cache re-apply the saved rig on the next load.
    RigAsset rig = rig_from_model(asset, "props/quad.obj");
    ASSERT_FALSE(rig.replaceImportedSkeleton);
    ASSERT_GE(rig.find_joint("Quad"), 0);
    compute_auto_weights(asset, rig, AutoWeightMode::Rigid, 4.0f, 4);

    std::string error;
    ASSERT_TRUE(apply_rig(asset, rig, &error)) << error;

    // A joint that carries an imported node's name IS that node. Appending a
    // twin builds a second skeleton: name-bound clips resolve to the imported
    // copy (first occurrence wins) while the skin follows the appended one, so
    // the mesh freezes at its bind pose.
    EXPECT_EQ(asset.nodes.size(), nodesBefore);
    EXPECT_EQ(duplicate_node_names(asset), 0);
    EXPECT_TRUE(vec3_close(asset.boundsMin(), boundsMin));
    EXPECT_TRUE(vec3_close(asset.boundsMax(), boundsMax));

    // The node a clip would drive is the node the skin follows.
    const Skeleton skeleton = Skeleton::from_model(asset);
    const std::uint32_t slot = asset.meshes[0].vertices[0].boneIndices[0];
    ASSERT_LT(slot, asset.bones.size());
    const int skinnedNode = asset.bones[slot].nodeIndex;
    ASSERT_GE(skinnedNode, 0);
    EXPECT_EQ(skeleton.find(asset.nodes[static_cast<std::size_t>(skinnedNode)].name), skinnedNode);

    // Re-seeding from the overlaid model and saving again is a no-op, rather
    // than stacking a third skeleton on top of the second.
    RigAsset reseeded = rig_from_model(asset, "props/quad.obj");
    compute_auto_weights(asset, reseeded, AutoWeightMode::Rigid, 4.0f, 4);
    ASSERT_TRUE(apply_rig(asset, reseeded, &error)) << error;
    EXPECT_EQ(asset.nodes.size(), nodesBefore);
    EXPECT_EQ(duplicate_node_names(asset), 0);
    EXPECT_TRUE(vec3_close(asset.boundsMin(), boundsMin));
    EXPECT_TRUE(vec3_close(asset.boundsMax(), boundsMax));
  }

  TEST(RigAssetTest, ApplyRigKeepsAMeshWhoseBonesDisagreeOnWhereItSits)
  {
    // A mesh the palette places has no single mesh-to-model matrix unless its
    // bones happen to agree. Reading the transform off one bone and applying
    // it to the whole mesh looks right for whichever vertex was sampled and
    // relocates the rest: on assimp's animation_with_skeleton.fbx that moved
    // 1664 of 4220 vertices by up to 3.85 units on a model of radius 54.6.
    ModelAsset asset = make_skinned_mesh_whose_bones_disagree();
    ASSERT_TRUE(vec3_close(skinned_bind_position(asset, 0, 0), {0.0f, 0.0f, 0.0f}));
    ASSERT_TRUE(vec3_close(skinned_bind_position(asset, 0, 1), {0.0f, 5.0f, 0.0f}));
    ASSERT_TRUE(vec3_close(skinned_bind_position(asset, 0, 2), {0.0f, 4.0f, 0.0f}));

    std::vector<math::Vec3> before;
    for (std::size_t vertex = 0; vertex < asset.meshes[0].vertices.size(); ++vertex)
    {
      before.push_back(skinned_bind_position(asset, 0, vertex));
    }

    RigAsset rig = rig_from_model(asset, "characters/hero.fbx");
    compute_auto_weights(asset, rig, AutoWeightMode::Rigid, 4.0f, 4);

    std::string error;
    ASSERT_TRUE(apply_rig(asset, rig, &error)) << error;

    for (std::size_t vertex = 0; vertex < before.size(); ++vertex)
    {
      EXPECT_TRUE(vec3_close(skinned_bind_position(asset, 0, vertex), before[vertex]))
          << "vertex " << vertex << " moved when the rig was applied";
    }
  }

  TEST(RigAssetTest, ApplyRigBuildsOffsetsFromTheHierarchyItActuallyProduces)
  {
    // A joint re-points the node it shares a name with, so every node below
    // that one moves with it. A joint parented to one of those nodes therefore
    // rests somewhere else than the pre-merge hierarchy said it would, and an
    // offset matrix built from the stale rest transform leaves its palette
    // entry non-identity at bind time — which drags its vertices away the
    // moment the rig is saved.
    ModelAsset asset;
    asset.nodes.push_back({"Root", -1, math::Mat4::identity()});
    asset.nodes.push_back({"Hips", 0, math::Mat4::translate({0.0f, 1.0f, 0.0f})});
    // Nothing skins to "Deco", so seeding never turns it into a joint — but a
    // joint can still be parented to it by name.
    asset.nodes.push_back({"Deco", 1, math::Mat4::translate({0.0f, 1.0f, 0.0f})});
    asset.bones.push_back({1, math::Mat4::identity()});

    ModelMeshData mesh;
    for (int i = 0; i < 3; ++i)
    {
      ModelVertex vertex{};
      vertex.py = static_cast<float>(i);
      vertex.nz = 1.0f;
      vertex.boneIndices[0] = 0;
      vertex.boneWeights[0] = 1.0f;
      mesh.vertices.push_back(vertex);
    }
    mesh.indices = {0, 1, 2};
    mesh.nodeIndex = 1;
    asset.meshes.push_back(std::move(mesh));
    asset.finalize();

    std::vector<math::Vec3> before;
    for (std::size_t vertex = 0; vertex < asset.meshes[0].vertices.size(); ++vertex)
    {
      before.push_back(skinned_bind_position(asset, 0, vertex));
    }

    RigAsset rig = rig_from_model(asset, "characters/hero.fbx");
    const int hips = rig.find_joint("Hips");
    ASSERT_GE(hips, 0);
    ASSERT_EQ(rig.find_joint("Deco"), -1);
    // The user drags Hips five units up and hangs a control joint off "Deco",
    // which is now five units higher than it was when the rig resolved.
    rig.joints[static_cast<std::size_t>(hips)].translation.y += 5.0f;
    RigJoint control;
    control.name = "Ctrl";
    control.parent = "Deco";
    rig.joints.push_back(control);
    const int ctrl = rig.find_joint("Ctrl");
    ASSERT_GE(ctrl, 0);

    RigMeshBinding binding;
    binding.meshIndex = 0;
    for (std::size_t vertex = 0; vertex < asset.meshes[0].vertices.size(); ++vertex)
    {
      binding.jointIndices.push_back(vertex == 2 ? ctrl : hips);
      binding.jointIndices.push_back(-1);
      binding.jointIndices.push_back(-1);
      binding.jointIndices.push_back(-1);
      binding.weights.push_back(1.0f);
      binding.weights.push_back(0.0f);
      binding.weights.push_back(0.0f);
      binding.weights.push_back(0.0f);
    }
    rig.meshes.push_back(std::move(binding));

    std::string error;
    ASSERT_TRUE(apply_rig(asset, rig, &error)) << error;

    // Moving a joint moves the bone, not the bind pose: the rig contributes
    // nothing until a clip poses it.
    for (const auto &vertex : asset.meshes[0].vertices)
    {
      for (int slot = 0; slot < 4; ++slot)
      {
        if (vertex.boneWeights[slot] <= 0.0f)
        {
          continue;
        }
        ASSERT_LT(vertex.boneIndices[slot], asset.bindPose().size());
        EXPECT_TRUE(mat4_close_to_identity(asset.bindPose()[vertex.boneIndices[slot]]))
            << "bone " << vertex.boneIndices[slot] << " moves the mesh at bind time";
      }
    }
    for (std::size_t vertex = 0; vertex < before.size(); ++vertex)
    {
      EXPECT_TRUE(vec3_close(skinned_bind_position(asset, 0, vertex), before[vertex]))
          << "vertex " << vertex << " moved when the rig was applied";
    }
  }

  TEST(RigAssetTest, AutoWeightsMeasureVerticesAgainstJointsInModelSpace)
  {
    const ModelAsset asset = make_unrigged_quad_under_a_placed_node();

    RigAsset rig = make_two_joint_rig();
    rig.joints[0].translation = {0.0f, 10.0f, 0.0f}; // sits on the quad's base
    rig.joints[1].translation = {0.0f, 8.0f, 0.0f};  // ... and on its top edge

    compute_auto_weights(asset, rig, AutoWeightMode::Rigid, 1.0f, 4);
    ASSERT_EQ(rig.meshes.size(), 1U);
    const RigMeshBinding &binding = rig.meshes[0];
    for (std::size_t vertex = 0; vertex < binding.vertexCount(); ++vertex)
    {
      // Vertices are mesh-local and joints are model-space; measuring the two
      // against each other unmeasured put every vertex on j_root.
      const int expected = asset.meshes[0].vertices[vertex].py > 2.0f ? 1 : 0;
      int dominant = -1;
      for (std::size_t slot = 0; slot < 4; ++slot)
      {
        if (binding.weights[vertex * 4 + slot] > 0.5f)
        {
          dominant = binding.jointIndices[vertex * 4 + slot];
        }
      }
      EXPECT_EQ(dominant, expected) << "vertex " << vertex;
    }

    // Envelope has to reach the mesh at all: measured in the wrong space every
    // vertex sits ten units outside every envelope and silently degrades to
    // the nearest-joint fallback, which can never blend two joints.
    RigAsset envelope = rig;
    envelope.meshes.clear();
    compute_auto_weights(asset, envelope, AutoWeightMode::Envelope, 3.0f, 4);
    int blended = 0;
    for (std::size_t vertex = 0; vertex < envelope.meshes[0].vertexCount(); ++vertex)
    {
      int influences = 0;
      for (std::size_t slot = 0; slot < 4; ++slot)
      {
        influences += envelope.meshes[0].weights[vertex * 4 + slot] > 0.0f ? 1 : 0;
      }
      blended += influences > 1 ? 1 : 0;
    }
    EXPECT_GT(blended, 0) << "no vertex was reached by more than one envelope";
  }

  TEST(RigAssetTest, ApplyRigRejectsBadInputInsteadOfSilentlyTruncatingTheModel)
  {
    ModelAsset asset = make_unrigged_quad();
    const std::size_t bonesBefore = asset.bones.size();
    const std::size_t nodesBefore = asset.nodes.size();

    RigAsset oversized;
    oversized.sourceModel = "props/quad.obj";
    for (std::uint32_t i = 0; i <= kMaxModelBones; ++i)
    {
      RigJoint joint;
      joint.name = "j" + std::to_string(i);
      oversized.joints.push_back(joint);
    }
    ASSERT_GT(oversized.joints.size(), static_cast<std::size_t>(kMaxModelBones));

    std::string error;
    EXPECT_FALSE(apply_rig(asset, oversized, &error));
    EXPECT_FALSE(error.empty());
    // Refusing must leave the model exactly as it was, not half-rigged.
    EXPECT_EQ(asset.bones.size(), bonesBefore);
    EXPECT_EQ(asset.nodes.size(), nodesBefore);

    // An unknown parent is refused the same way.
    RigAsset orphan = make_two_joint_rig();
    orphan.joints[0].parent = "NoSuchNode";
    error.clear();
    EXPECT_FALSE(apply_rig(asset, orphan, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(asset.nodes.size(), nodesBefore);

    // So is a binding that names a mesh the model does not have.
    RigAsset strayMesh = make_two_joint_rig();
    RigMeshBinding binding;
    binding.meshIndex = 7;
    binding.jointIndices = {0, -1, -1, -1};
    binding.weights = {1.0f, 0.0f, 0.0f, 0.0f};
    strayMesh.meshes.push_back(binding);
    error.clear();
    EXPECT_FALSE(apply_rig(asset, strayMesh, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(asset.nodes.size(), nodesBefore);
  }

  TEST(RigAssetTest, ComputeAutoWeightsInRigidModeGivesTheNearestJointTheWholeVertex)
  {
    const ModelAsset asset = make_unrigged_quad();
    RigAsset rig = make_two_joint_rig();

    compute_auto_weights(asset, rig, AutoWeightMode::Rigid, 1.0f, 4);

    ASSERT_EQ(rig.meshes.size(), 1U);
    const RigMeshBinding &binding = rig.meshes[0];
    EXPECT_EQ(binding.meshIndex, 0U);
    ASSERT_EQ(binding.vertexCount(), asset.meshes[0].vertices.size());
    ASSERT_EQ(binding.jointIndices.size(), binding.weights.size());

    for (std::size_t vertex = 0; vertex < binding.vertexCount(); ++vertex)
    {
      // The quad's lower half is nearest j_root, its upper half nearest j_tip.
      const int expected = asset.meshes[0].vertices[vertex].py > 2.0f ? 1 : 0;

      int dominant = -1;
      float total = 0.0f;
      for (std::size_t slot = 0; slot < 4; ++slot)
      {
        const float weight = binding.weights[vertex * 4 + slot];
        total += weight;
        if (weight > 0.5f)
        {
          dominant = binding.jointIndices[vertex * 4 + slot];
        }
      }
      EXPECT_EQ(dominant, expected) << "vertex " << vertex;
      EXPECT_NEAR(total, 1.0f, kTolerance) << "vertex " << vertex;
    }

    // Weighting a single mesh reuses the binding rather than appending one.
    compute_auto_weights_for_mesh(asset, rig, 0, AutoWeightMode::Smooth, 2.0f, 4);
    EXPECT_EQ(rig.meshes.size(), 1U);
    // A mesh index the model does not have is ignored, not appended.
    compute_auto_weights_for_mesh(asset, rig, 9, AutoWeightMode::Rigid, 1.0f, 4);
    EXPECT_EQ(rig.meshes.size(), 1U);
  }

  TEST(RigAssetTest, RigPathFlattensTheModelReferenceSoNestedModelsNeverCollide)
  {
    const std::filesystem::path root = unique_test_directory("hades-rig-path");

    const std::filesystem::path nested = rig_path_for_model(root, "characters/hero/hero.fbx");
    const std::filesystem::path other = rig_path_for_model(root, "props/hero/hero.fbx");

    EXPECT_EQ(nested.parent_path().string(), (root / ".hades" / "rigs").string());
    EXPECT_EQ(nested.extension().string(), ".json");
    // Two models with the same file name in different folders must not share
    // a rig...
    EXPECT_NE(nested.string(), other.string());
    // ...and the same reference must always resolve to the same file.
    EXPECT_EQ(nested.string(), rig_path_for_model(root, "characters/hero/hero.fbx").string());

    // The whole reference is flattened, so the rigs directory stays flat.
    const std::string stem = nested.stem().string();
    EXPECT_EQ(stem.find('/'), std::string::npos);
    EXPECT_EQ(stem.find('\\'), std::string::npos);

    // An empty reference still resolves somewhere writable.
    EXPECT_FALSE(rig_path_for_model(root, "").filename().string().empty());
  }

  TEST(RigAssetTest, JsonRoundTripPreservesJointsAndMeshBindings)
  {
    RigAsset rig;
    rig.sourceModel = "characters/hero.fbx";
    rig.replaceImportedSkeleton = false;

    RigJoint root;
    root.name = "hips";
    root.parent = "Armature";
    root.translation = {1.0f, 2.0f, 3.0f};
    root.rotation = math::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 0.75f);
    root.scale = {2.0f, 2.0f, 2.0f};
    // Import provenance: the node this joint was seeded from, and the part of
    // that node's transform TRS cannot hold.
    root.sourceNode = 4;
    root.restCorrection.m[1][0] = 0.4f;
    root.hasCorrection = true;

    RigJoint child;
    child.name = "spine";
    child.parent = "hips";
    child.translation = {0.0f, 1.5f, 0.0f};

    rig.joints = {root, child};

    RigMeshBinding binding;
    binding.meshIndex = 2;
    binding.jointIndices = {0, 1, -1, -1, 1, -1, -1, -1};
    binding.weights = {0.6f, 0.4f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    rig.meshes.push_back(binding);

    RigAsset restored;
    std::string error;
    ASSERT_TRUE(RigAsset::from_json(rig.to_json(), restored, &error)) << error;

    EXPECT_EQ(restored.sourceModel, "characters/hero.fbx");
    EXPECT_FALSE(restored.replaceImportedSkeleton);
    ASSERT_EQ(restored.joints.size(), 2U);
    EXPECT_EQ(restored.joints[0].name, "hips");
    EXPECT_EQ(restored.joints[0].parent, "Armature");
    EXPECT_TRUE(vec3_close(restored.joints[0].translation, {1.0f, 2.0f, 3.0f}));
    EXPECT_TRUE(quat_close(restored.joints[0].rotation, root.rotation));
    EXPECT_TRUE(vec3_close(restored.joints[0].scale, {2.0f, 2.0f, 2.0f}));
    EXPECT_EQ(restored.joints[1].parent, "hips");
    EXPECT_TRUE(vec3_close(restored.joints[1].scale, {1.0f, 1.0f, 1.0f}));
    EXPECT_EQ(restored.joints[0].sourceNode, 4);
    EXPECT_TRUE(restored.joints[0].hasCorrection);
    EXPECT_TRUE(mat4_close(restored.joints[0].restCorrection, root.restCorrection));
    // A joint somebody added in the editor came from no node and carries no
    // remainder, and says so.
    EXPECT_EQ(restored.joints[1].sourceNode, -1);
    EXPECT_FALSE(restored.joints[1].hasCorrection);

    // A rig written before either field existed reads back the same way,
    // rather than defaulting the source node to 0 — which would merge every
    // joint of it onto the model's root node.
    nlohmann::json older = rig.to_json();
    for (auto &entry : older["joints"])
    {
      entry.erase("sourceNode");
      entry.erase("restCorrection");
    }
    RigAsset fromOlderBuild;
    ASSERT_TRUE(RigAsset::from_json(older, fromOlderBuild, &error)) << error;
    ASSERT_EQ(fromOlderBuild.joints.size(), 2U);
    EXPECT_EQ(fromOlderBuild.joints[0].sourceNode, -1);
    EXPECT_FALSE(fromOlderBuild.joints[0].hasCorrection);
    EXPECT_TRUE(mat4_close_to_identity(fromOlderBuild.joints[0].restCorrection));

    ASSERT_EQ(restored.meshes.size(), 1U);
    EXPECT_EQ(restored.meshes[0].meshIndex, 2U);
    EXPECT_EQ(restored.meshes[0].vertexCount(), 2U);
    ASSERT_EQ(restored.meshes[0].jointIndices.size(), 8U);
    EXPECT_EQ(restored.meshes[0].jointIndices[1], 1);
    EXPECT_EQ(restored.meshes[0].jointIndices[3], -1);
    EXPECT_NEAR(restored.meshes[0].weights[0], 0.6f, kTolerance);
    EXPECT_NEAR(restored.meshes[0].weights[4], 1.0f, kTolerance);

    RigAsset rejected;
    std::string rejectError;
    EXPECT_FALSE(RigAsset::from_json(nlohmann::json::array(), rejected, &rejectError));
    EXPECT_FALSE(rejectError.empty());
  }

  TEST(RigAssetTest, SaveAndLoadRoundTripThroughTheRigsDirectory)
  {
    const std::filesystem::path root = unique_test_directory("hades-rig-io");
    ScopedDirectoryCleanup cleanup(root);
    std::filesystem::create_directories(root);

    const RigAsset rig = make_two_joint_rig();
    const std::filesystem::path path = rig_path_for_model(root, "props/quad.obj");

    std::string error;
    ASSERT_TRUE(save_rig_asset(path, rig, &error)) << error;
    ASSERT_TRUE(std::filesystem::exists(path));

    RigAsset loaded;
    ASSERT_TRUE(load_rig_asset(path, loaded, &error)) << error;
    ASSERT_EQ(loaded.joints.size(), 2U);
    EXPECT_EQ(loaded.joints[0].name, "j_root");
    EXPECT_EQ(loaded.joints[1].parent, "j_root");
    EXPECT_TRUE(vec3_close(loaded.joints[1].translation, {0.0f, 4.0f, 0.0f}));
    EXPECT_EQ(loaded.sourceModel, "props/quad.obj");

    // A file that is not there is an error with a message, not a throw.
    RigAsset missing;
    error.clear();
    EXPECT_FALSE(load_rig_asset(root / "nothing-here.json", missing, &error));
    EXPECT_FALSE(error.empty());
  }

  TEST(RigAssetTest, ModelCacheReportsARigItRefusedInsteadOfDroppingIt)
  {
    const std::filesystem::path root = unique_test_directory("hades-rig-cache");
    ScopedDirectoryCleanup cleanup(root);
    std::filesystem::create_directories(root);
    {
      std::ofstream model(root / "quad.obj");
      model << "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
               "vn 0 0 1\n"
               "f 1//1 2//1 3//1\nf 1//1 3//1 4//1\n";
    }

    // A rig that no longer fits its model — the DCC iteration loop: rig the
    // file, then re-export it from Blender with a mesh removed.
    RigAsset broken = make_two_joint_rig();
    RigMeshBinding stray;
    stray.meshIndex = 5;
    stray.jointIndices = {0, -1, -1, -1};
    stray.weights = {1.0f, 0.0f, 0.0f, 0.0f};
    broken.meshes.push_back(stray);

    std::string error;
    ASSERT_TRUE(save_rig_asset(rig_path_for_model(root, "quad.obj"), broken, &error)) << error;

    auto &cache = ModelAssetCache::instance();
    cache.setAssetRoot(root);
    cache.clear();

    // The model itself loads fine, so the refusal must not be reported as a
    // load failure — but it must be reported somewhere, or the panel says
    // "Saved rig" over a viewport that never changed.
    const ModelAsset *asset = cache.get("quad.obj");
    ASSERT_NE(asset, nullptr);
    EXPECT_TRUE(cache.errorFor("quad.obj").empty());
    EXPECT_NE(cache.rigErrorFor("quad.obj").find("mesh 5"), std::string::npos)
        << "rig error was '" << cache.rigErrorFor("quad.obj") << "'";
    // The imported palette is what a bone-budget check has to measure against;
    // asset->bones already carries whatever overlay was applied.
    EXPECT_EQ(cache.importedBoneCount("quad.obj"), 1U);

    // A rig that applies leaves no error behind.
    ASSERT_TRUE(save_rig_asset(rig_path_for_model(root, "quad.obj"), make_two_joint_rig(), &error)) << error;
    cache.invalidate("quad.obj");
    ASSERT_NE(cache.get("quad.obj"), nullptr);
    EXPECT_TRUE(cache.rigErrorFor("quad.obj").empty());

    cache.clear();
    cache.setAssetRoot({});
  }

  TEST(RigAssetTest, RigFromModelSeedsAnEditableCopyOfTheImportedSkeleton)
  {
    ModelAsset asset;
    asset.nodes.push_back({"Armature", -1, math::Mat4::identity()});
    asset.nodes.push_back({"hips", 0, math::Mat4::translate({0.0f, 1.0f, 0.0f})});
    asset.nodes.push_back({"spine", 1, math::Mat4::translate({0.0f, 2.0f, 0.0f})});
    // A node nothing skins to is not worth showing in the rig editor.
    asset.nodes.push_back({"decoration", 0, math::Mat4::identity()});
    asset.bones.push_back({2, math::Mat4::identity()});
    asset.finalize();

    const RigAsset rig = rig_from_model(asset, "characters/hero.fbx");

    EXPECT_EQ(rig.sourceModel, "characters/hero.fbx");
    // Seeding overlays the import instead of replacing it: the imported
    // weights are the only ones that exist.
    EXPECT_FALSE(rig.replaceImportedSkeleton);

    // The skinned joint and the chain above it are kept, the rest dropped.
    EXPECT_GE(rig.find_joint("spine"), 0);
    EXPECT_GE(rig.find_joint("hips"), 0);
    EXPECT_GE(rig.find_joint("Armature"), 0);
    EXPECT_EQ(rig.find_joint("decoration"), -1);

    const int spine = rig.find_joint("spine");
    ASSERT_GE(spine, 0);
    EXPECT_EQ(rig.joints[static_cast<std::size_t>(spine)].parent, "hips");
    EXPECT_TRUE(vec3_close(rig.joints[static_cast<std::size_t>(spine)].translation, {0.0f, 2.0f, 0.0f}));

    // Which node each joint came from, so applying the rig can merge onto it
    // rather than appending a twin of it.
    EXPECT_EQ(rig.joints[static_cast<std::size_t>(spine)].sourceNode, 2);
    EXPECT_EQ(rig.joints[static_cast<std::size_t>(rig.find_joint("hips"))].sourceNode, 1);
    EXPECT_EQ(rig.joints[static_cast<std::size_t>(rig.find_joint("Armature"))].sourceNode, 0);
  }

  TEST(RigAssetTest, SeedingAModelWhoseNodesHaveNoNamesStillMergesOntoThem)
  {
    // Nodes a name can never find: the seed has to call them "joint" and
    // "joint_1" to keep them addressable inside the rig, and those names
    // match nothing in the model. Every save used to append one shadow node
    // and one shadow palette entry per joint — on assimp's cubes_nonames.fbx
    // (four unnamed nodes) 5 nodes became 9 and the palette 4 became 9.
    ModelAsset asset = make_two_quads(std::string(), std::string());
    ASSERT_EQ(duplicate_node_names(asset), 1);

    const RigAsset seeded = rig_from_model(asset, "props/quads.fbx");
    ASSERT_EQ(seeded.joints.size(), 3U);
    EXPECT_EQ(seeded.joints[1].sourceNode, 1);
    EXPECT_EQ(seeded.joints[2].sourceNode, 2);
    EXPECT_NE(seeded.joints[1].name, seeded.joints[2].name);
    EXPECT_EQ(find_node(asset, seeded.joints[1].name), -1) << "the seeded name matches no node";

    expect_seeding_is_idempotent(asset);

    // The merged nodes answer to their joints' names, so a clip keyed in the
    // Animate tab has something to bind to: an unnamed node is a node no
    // name-bound key can ever reach.
    for (const auto &node : asset.nodes)
    {
      EXPECT_FALSE(node.name.empty());
    }
  }

  TEST(RigAssetTest, SeedingAModelThatNamesTwoNodesTheSameMergesOntoBoth)
  {
    // The other half of the same gap: assimp's cubes_with_names.fbx names two
    // different nodes "Куб1", so the seed renames the second joint "Куб1_1"
    // — a name no node carries, which used to append a shadow node for it.
    ModelAsset asset = make_two_quads("Cube", "Cube");
    ASSERT_EQ(duplicate_node_names(asset), 1);

    const RigAsset seeded = rig_from_model(asset, "props/quads.fbx");
    ASSERT_EQ(seeded.joints.size(), 3U);
    EXPECT_EQ(seeded.joints[1].name, "Cube");
    EXPECT_EQ(seeded.joints[2].name, "Cube_1");
    EXPECT_EQ(seeded.joints[2].sourceNode, 2);

    expect_seeding_is_idempotent(asset);

    // The second node kept its own identity instead of sharing the first's,
    // which is what makes the two rows in the Animate tree separable.
    EXPECT_GE(find_node(asset, "Cube"), 0);
    EXPECT_GE(find_node(asset, "Cube_1"), 0);
  }

  TEST(RigAssetTest, ASourceNodeThatNoLongerFitsTheModelFallsBackToTheName)
  {
    // A rig outlives its model: the file is re-exported with the nodes in a
    // different order, or with fewer of them. A recorded index must never be
    // trusted blindly then — and must never be used to index the node list.
    ModelAsset reordered = make_unrigged_quad_under_a_placed_node();
    const std::size_t nodesBefore = reordered.nodes.size();
    const int quadNode = find_node(reordered, "Quad");
    ASSERT_EQ(quadNode, 1);

    RigAsset rig = rig_from_model(reordered, "props/quad.obj");
    const int quadJoint = rig.find_joint("Quad");
    ASSERT_GE(quadJoint, 0);
    // Past the end of the hierarchy the rig now meets.
    rig.joints[static_cast<std::size_t>(quadJoint)].sourceNode = 97;

    std::string error;
    ASSERT_TRUE(apply_rig(reordered, rig, &error)) << error;
    EXPECT_EQ(reordered.nodes.size(), nodesBefore) << "the name still identifies the node";
    EXPECT_EQ(duplicate_node_names(reordered), 0);
    EXPECT_EQ(find_node(reordered, "Quad"), quadNode);

    // In range but pointing at the wrong node, because the export moved it: a
    // name that resolves is better evidence than an index into a hierarchy
    // that has changed, so the name wins that one case.
    ModelAsset moved = make_unrigged_quad_under_a_placed_node();
    RigAsset stale = rig_from_model(moved, "props/quad.obj");
    stale.joints[static_cast<std::size_t>(stale.find_joint("Quad"))].sourceNode = 0;
    ASSERT_TRUE(apply_rig(moved, stale, &error)) << error;
    EXPECT_EQ(moved.nodes.size(), nodesBefore);
    EXPECT_EQ(duplicate_node_names(moved), 0);
    EXPECT_EQ(moved.nodes[0].name, "Root") << "the root was not renamed onto";

    // Two joints claiming one node — only a hand-edited rig gets here, but it
    // must not let the second joint quietly overwrite the first's node.
    ModelAsset shared = make_unrigged_quad_under_a_placed_node();
    RigAsset collided = rig_from_model(shared, "props/quad.obj");
    RigJoint twin = collided.joints[static_cast<std::size_t>(collided.find_joint("Quad"))];
    twin.name = "Quad_copy";
    twin.translation = {0.0f, 99.0f, 0.0f};
    collided.joints.push_back(twin);
    ASSERT_TRUE(apply_rig(shared, collided, &error)) << error;
    EXPECT_EQ(shared.nodes.size(), nodesBefore + 1) << "the twin got its own node";
    EXPECT_EQ(find_node(shared, "Quad"), quadNode);
  }

  TEST(RigAssetTest, SeedingKeepsWhatTrsCannotHoldInsteadOfBakingItAway)
  {
    // A node whose linear part is scale-then-rotate — what a COLLADA
    // `<matrix>`, or `<scale>` written before `<rotate>`, produces.
    // decomposeTRS reports success on it and silently drops the shear, so
    // seeding a rig from it and applying the rig back used to rewrite the
    // node without it, moving everything hanging off that node.
    const math::Mat4 shear = math::Mat4::translate({0.0f, 1.0f, 0.0f}) *
                             math::Mat4::scaleMatrix({2.0f, 0.5f, 1.0f}) *
                             math::Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, 0.6f).toMat4();

    ModelAsset asset = make_two_quads("Hips", "Chest");
    asset.nodes[1].localTransform = shear;
    asset.nodes[2].parent = 1;
    asset.finalize();
    const std::vector<math::Vec3> imported = skinned_bind_positions(asset);

    RigAsset rig = rig_from_model(asset, "props/sheared.dae");
    const int hips = rig.find_joint("Hips");
    ASSERT_GE(hips, 0);
    const RigJoint &joint = rig.joints[static_cast<std::size_t>(hips)];
    ASSERT_TRUE(joint.hasCorrection) << "the shear was decomposed away with nothing kept";
    EXPECT_TRUE(mat4_close(math::buildModelMatrix(joint.translation, joint.rotation, joint.scale) *
                               joint.restCorrection,
                           shear));

    // Rigging one mesh and leaving the other is what a user does; the mesh
    // that keeps its imported weights is the one a moved node drags away.
    compute_auto_weights_for_mesh(asset, rig, 0, AutoWeightMode::Rigid, 4.0f, 4);

    RigAsset saved;
    std::string error;
    ASSERT_TRUE(RigAsset::from_json(rig.to_json(), saved, &error)) << error;
    ASSERT_TRUE(saved.joints[static_cast<std::size_t>(saved.find_joint("Hips"))].hasCorrection);
    ASSERT_TRUE(apply_rig(asset, saved, &error)) << error;

    EXPECT_TRUE(mat4_close(asset.nodes[static_cast<std::size_t>(find_node(asset, "Hips"))].localTransform,
                           shear))
        << "the node came back without what it was authored with";
    const std::vector<math::Vec3> now = skinned_bind_positions(asset);
    ASSERT_EQ(now.size(), imported.size());
    for (std::size_t i = 0; i < now.size(); ++i)
    {
      EXPECT_TRUE(vec3_close(now[i], imported[i])) << "vertex " << i;
    }
  }
}
