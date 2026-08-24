#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "test_support.hpp"

#include "../engine/assets/model_asset.hpp"
#include "../engine/assets/model_asset_cache.hpp"
#include "../engine/assets/model_loader.hpp"
#include "../engine/components/animation_component.hpp"
#include "../engine/components/light_component.hpp"
#include "../engine/components/model_component.hpp"
#include "../engine/components/name_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_factory.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/scene_serializer.hpp"
#include "../engine/core/ecs/world_utils.hpp"
#include "../engine/rendering/scene_renderer.hpp"
#include "../engine/systems/animation_system.hpp"

namespace hades
{
  using test_support::ScopedDirectoryCleanup;
  using test_support::unique_test_directory;

  namespace
  {
    void write_text_file(const std::filesystem::path &path, const std::string &content)
    {
      std::ofstream out(path, std::ios::binary);
      out << content;
    }

    template <typename T>
    void append_bytes(std::vector<uint8_t> &buffer, const T &value)
    {
      const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
      buffer.insert(buffer.end(), bytes, bytes + sizeof(T));
    }

    // Build a minimal skinned glTF on disk: a triangle rigged to two joints
    // (Bone1 offset one unit up from Bone0) plus a 1-second animation that
    // rotates Bone1 by 90 degrees around Z.
    void write_skinned_gltf(const std::filesystem::path &directory)
    {
      std::vector<uint8_t> bin;

      // Positions (vec3 float ×3) at offset 0.
      const float positions[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
      const size_t positionsOffset = bin.size();
      for (const auto &p : positions)
        for (float f : p)
          append_bytes(bin, f);

      // Normals (vec3 float ×3).
      const size_t normalsOffset = bin.size();
      for (int i = 0; i < 3; ++i)
      {
        append_bytes(bin, 0.0f);
        append_bytes(bin, 0.0f);
        append_bytes(bin, 1.0f);
      }

      // JOINTS_0 (u16vec4 ×3): v0 → joint 0, v1 → joint 1, v2 → both.
      const size_t jointsOffset = bin.size();
      const uint16_t joints[3][4] = {{0, 0, 0, 0}, {1, 0, 0, 0}, {0, 1, 0, 0}};
      for (const auto &j : joints)
        for (uint16_t v : j)
          append_bytes(bin, v);

      // WEIGHTS_0 (vec4 float ×3).
      const size_t weightsOffset = bin.size();
      const float weights[3][4] = {{1, 0, 0, 0}, {1, 0, 0, 0}, {0.5f, 0.5f, 0, 0}};
      for (const auto &w : weights)
        for (float v : w)
          append_bytes(bin, v);

      // Indices (u16 ×3), padded to 4 bytes.
      const size_t indicesOffset = bin.size();
      for (uint16_t i : {uint16_t{0}, uint16_t{1}, uint16_t{2}})
        append_bytes(bin, i);
      append_bytes(bin, uint16_t{0}); // padding

      // Animation key times (float ×2).
      const size_t timesOffset = bin.size();
      append_bytes(bin, 0.0f);
      append_bytes(bin, 1.0f);

      // Animation rotations (quat float ×2): identity → 90° around Z.
      const size_t rotationsOffset = bin.size();
      const float halfSqrt2 = 0.70710678f;
      const float rotations[2][4] = {{0, 0, 0, 1}, {0, 0, halfSqrt2, halfSqrt2}};
      for (const auto &r : rotations)
        for (float v : r)
          append_bytes(bin, v);

      // Inverse bind matrices (mat4 float ×2, column-major):
      // identity for Bone0, translate(0,-1,0) for Bone1.
      const size_t ibmOffset = bin.size();
      const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
      const float ibm1[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1};
      for (float v : identity)
        append_bytes(bin, v);
      for (float v : ibm1)
        append_bytes(bin, v);

      {
        std::ofstream out(directory / "model.bin", std::ios::binary);
        out.write(reinterpret_cast<const char *>(bin.data()), static_cast<std::streamsize>(bin.size()));
      }

      nlohmann::json gltf;
      gltf["asset"] = {{"version", "2.0"}};
      gltf["scene"] = 0;
      gltf["scenes"] = {{{"nodes", {0, 1}}}};
      gltf["nodes"] = {
          {{"name", "MeshNode"}, {"mesh", 0}, {"skin", 0}},
          {{"name", "Bone0"}, {"children", {2}}},
          {{"name", "Bone1"}, {"translation", {0.0, 1.0, 0.0}}}};
      gltf["meshes"] = {
          {{"primitives",
            {{{"attributes",
               {{"POSITION", 0}, {"NORMAL", 1}, {"JOINTS_0", 2}, {"WEIGHTS_0", 3}}},
              {"indices", 4}}}}}};
      gltf["skins"] = {
          {{"inverseBindMatrices", 7}, {"joints", {1, 2}}}};
      gltf["animations"] = {
          {{"name", "spin"},
           {"channels",
            {{{"sampler", 0}, {"target", {{"node", 2}, {"path", "rotation"}}}}}},
           {"samplers",
            {{{"input", 5}, {"output", 6}, {"interpolation", "LINEAR"}}}}}};

      const auto bufferView = [](size_t offset, size_t length) {
        return nlohmann::json{{"buffer", 0}, {"byteOffset", offset}, {"byteLength", length}};
      };
      gltf["bufferViews"] = {
          bufferView(positionsOffset, 36),
          bufferView(normalsOffset, 36),
          bufferView(jointsOffset, 24),
          bufferView(weightsOffset, 48),
          bufferView(indicesOffset, 6),
          bufferView(timesOffset, 8),
          bufferView(rotationsOffset, 32),
          bufferView(ibmOffset, 128)};

      gltf["accessors"] = {
          {{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"},
           {"min", {0.0, 0.0, 0.0}}, {"max", {1.0, 1.0, 0.0}}},
          {{"bufferView", 1}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
          {{"bufferView", 2}, {"componentType", 5123}, {"count", 3}, {"type", "VEC4"}},
          {{"bufferView", 3}, {"componentType", 5126}, {"count", 3}, {"type", "VEC4"}},
          {{"bufferView", 4}, {"componentType", 5123}, {"count", 3}, {"type", "SCALAR"}},
          {{"bufferView", 5}, {"componentType", 5126}, {"count", 2}, {"type", "SCALAR"},
           {"min", {0.0}}, {"max", {1.0}}},
          {{"bufferView", 6}, {"componentType", 5126}, {"count", 2}, {"type", "VEC4"}},
          {{"bufferView", 7}, {"componentType", 5126}, {"count", 2}, {"type", "MAT4"}}};

      gltf["buffers"] = {
          {{"uri", "model.bin"}, {"byteLength", bin.size()}}};

      write_text_file(directory / "model.gltf", gltf.dump(2));
    }

    math::Vec3 skin_point(
        const std::vector<math::Mat4> &palette, int boneIndex, const math::Vec3 &p)
    {
      return palette[static_cast<size_t>(boneIndex)].transformPoint(p);
    }
  }

  TEST(ModelAssetTest, ObjImportProducesRigidPseudoBonedMesh)
  {
    const std::filesystem::path testRoot = unique_test_directory("hades-model-obj");
    ScopedDirectoryCleanup cleanup(testRoot);
    std::filesystem::create_directories(testRoot);

    write_text_file(
        testRoot / "quad.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 1 1 0\n"
        "v 0 1 0\n"
        "vn 0 0 1\n"
        "f 1//1 2//1 3//1\n"
        "f 1//1 3//1 4//1\n");

    ModelAsset asset;
    std::string error;
    ASSERT_TRUE(load_model_asset(testRoot / "quad.obj", asset, &error)) << error;

    ASSERT_EQ(asset.meshes.size(), 1U);
    EXPECT_EQ(asset.triangleCount(), 2U);
    EXPECT_TRUE(asset.clips.empty());
    EXPECT_GT(asset.boundsRadius(), 0.0f);

    // Rigid meshes are bound to a single pseudo-bone with full weight.
    ASSERT_EQ(asset.bones.size(), 1U);
    for (const auto &vertex : asset.meshes[0].vertices)
    {
      EXPECT_EQ(vertex.boneIndices[0], 0U);
      EXPECT_FLOAT_EQ(vertex.boneWeights[0], 1.0f);
    }

    // Bind pose of a rigid mesh keeps vertices where they were authored.
    ASSERT_EQ(asset.bindPose().size(), 1U);
    const auto &vertex = asset.meshes[0].vertices[1];
    const math::Vec3 skinned = skin_point(asset.bindPose(), 0, {vertex.px, vertex.py, vertex.pz});
    EXPECT_NEAR(skinned.x, vertex.px, 1e-4f);
    EXPECT_NEAR(skinned.y, vertex.py, 1e-4f);
    EXPECT_NEAR(skinned.z, vertex.pz, 1e-4f);
  }

  TEST(ModelAssetTest, GltfSkinnedImportAndPoseSampling)
  {
    const std::filesystem::path testRoot = unique_test_directory("hades-model-gltf");
    ScopedDirectoryCleanup cleanup(testRoot);
    std::filesystem::create_directories(testRoot);
    write_skinned_gltf(testRoot);

    ModelAsset asset;
    std::string error;
    ASSERT_TRUE(load_model_asset(testRoot / "model.gltf", asset, &error)) << error;

    ASSERT_EQ(asset.meshes.size(), 1U);
    ASSERT_EQ(asset.bones.size(), 2U);
    ASSERT_EQ(asset.clips.size(), 1U);
    EXPECT_NEAR(asset.clips[0].duration, 1.0f, 1e-3f);

    // Bind pose leaves the mesh where it was authored.
    std::vector<math::Mat4> pose;
    asset.samplePose(0, 0.0f, pose);
    ASSERT_EQ(pose.size(), 2U);
    const math::Vec3 v1Bind = skin_point(pose, 1, {1.0f, 0.0f, 0.0f});
    EXPECT_NEAR(v1Bind.x, 1.0f, 1e-3f);
    EXPECT_NEAR(v1Bind.y, 0.0f, 1e-3f);
    EXPECT_NEAR(v1Bind.z, 0.0f, 1e-3f);

    // At the end of the clip Bone1 rotated 90° around Z about its pivot at
    // (0,1,0): (1,0,0) → (1,2,0).
    asset.samplePose(0, 1.0f, pose);
    const math::Vec3 v1End = skin_point(pose, 1, {1.0f, 0.0f, 0.0f});
    EXPECT_NEAR(v1End.x, 1.0f, 2e-3f);
    EXPECT_NEAR(v1End.y, 2.0f, 2e-3f);
    EXPECT_NEAR(v1End.z, 0.0f, 2e-3f);

    // Halfway through the rotation is 45°: the pivot-relative vector
    // (1,-1,0) rotates onto (sqrt2, 0, 0).
    asset.samplePose(0, 0.5f, pose);
    const math::Vec3 v1Mid = skin_point(pose, 1, {1.0f, 0.0f, 0.0f});
    EXPECT_NEAR(v1Mid.x, 1.41421f, 5e-3f);
    EXPECT_NEAR(v1Mid.y, 1.0f, 5e-3f);
    EXPECT_NEAR(v1Mid.z, 0.0f, 5e-3f);
  }

  TEST(ModelAssetTest, SamplePoseInterpolatesSyntheticPositionKeys)
  {
    ModelAsset asset;
    asset.nodes.push_back({"root", -1, math::Mat4::identity()});
    asset.bones.push_back({0, math::Mat4::identity()});

    AnimationClip clip;
    clip.name = "slide";
    clip.duration = 1.0f;
    AnimationChannel channel;
    channel.nodeIndex = 0;
    channel.positions = {{0.0f, {0.0f, 0.0f, 0.0f}}, {1.0f, {2.0f, 0.0f, 0.0f}}};
    clip.channels.push_back(channel);
    asset.clips.push_back(clip);
    asset.finalize();

    std::vector<math::Mat4> pose;
    asset.samplePose(0, 0.5f, pose);
    ASSERT_EQ(pose.size(), 1U);
    const math::Vec3 moved = pose[0].transformPoint({0.0f, 0.0f, 0.0f});
    EXPECT_NEAR(moved.x, 1.0f, 1e-4f);

    // Out-of-range clip index falls back to the bind pose.
    asset.samplePose(7, 0.5f, pose);
    const math::Vec3 bind = pose[0].transformPoint({0.0f, 0.0f, 0.0f});
    EXPECT_NEAR(bind.x, 0.0f, 1e-4f);
  }

  TEST(ModelAssetTest, AnimationSystemAdvancesWrapsAndStops)
  {
    const std::filesystem::path testRoot = unique_test_directory("hades-model-anim");
    ScopedDirectoryCleanup cleanup(testRoot);
    std::filesystem::create_directories(testRoot);
    write_skinned_gltf(testRoot);

    auto &cache = ModelAssetCache::instance();
    cache.setAssetRoot(testRoot);
    cache.clear();

    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);
    const auto world = EntityFactory::createWorld(entityManager, componentManager, "World", true);
    const auto entity = EntityFactory::createModel(entityManager, componentManager, world);
    componentManager.getComponent<ModelComponent>(entity).assetPath = "model.gltf";

    AnimationSystem system;

    // Looping playback wraps: 0.75 + 0.5 → 0.25 into the 1-second clip.
    auto &anim = componentManager.getComponent<AnimationComponent>(entity);
    anim.time = 0.75f;
    system.update(0.5f, componentManager, entityManager);
    EXPECT_NEAR(componentManager.getComponent<AnimationComponent>(entity).time, 0.25f, 1e-4f);
    EXPECT_TRUE(componentManager.getComponent<AnimationComponent>(entity).playing);

    // One-shot playback clamps at the clip end and stops.
    auto &animAgain = componentManager.getComponent<AnimationComponent>(entity);
    animAgain.looping = false;
    animAgain.time = 0.9f;
    system.update(0.5f, componentManager, entityManager);
    EXPECT_NEAR(componentManager.getComponent<AnimationComponent>(entity).time, 1.0f, 1e-4f);
    EXPECT_FALSE(componentManager.getComponent<AnimationComponent>(entity).playing);

    cache.clear();
    cache.setAssetRoot({});
  }

  TEST(ModelAssetTest, SceneRendererAddsHeadlightOnlyWhenSceneHasNoLights)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);
    const auto world = EntityFactory::createWorld(entityManager, componentManager, "World", true);
    EntityFactory::createCube(entityManager, componentManager, world);

    SceneRenderer sceneRenderer;
    const RenderCamera camera = sceneRenderer.buildCamera(
        {0.0f, 1.0f, -5.0f}, {0.0f, 0.0f, 0.0f}, 60.0f, 1.0f, 0.1f, 100.0f);

    // No lights in the scene → a default headlight keeps geometry shaded.
    RenderList list = sceneRenderer.buildRenderList(camera, componentManager, entityManager, world);
    ASSERT_EQ(list.lights.size(), 1U);
    EXPECT_EQ(list.lights[0].type, 0);

    // A real light suppresses the headlight.
    const auto light = EntityFactory::createPointLight(entityManager, componentManager, world);
    componentManager.getComponent<LightComponent>(light).intensity = 5.0f;
    list = sceneRenderer.buildRenderList(camera, componentManager, entityManager, world);
    ASSERT_EQ(list.lights.size(), 1U);
    EXPECT_FLOAT_EQ(list.lights[0].intensity, 5.0f);
  }

  TEST(ModelAssetTest, ModelAndAnimationComponentsSerializeRoundTrip)
  {
    const std::filesystem::path testRoot = unique_test_directory("hades-model-serialize");
    ScopedDirectoryCleanup cleanup(testRoot);
    const std::filesystem::path workspaceRoot = testRoot / "Workspace";
    std::filesystem::create_directories(workspaceRoot);

    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);
    const auto world = EntityFactory::createWorld(entityManager, componentManager, "World", true);
    const auto entity = EntityFactory::createModel(entityManager, componentManager, world);
    componentManager.getComponent<NameComponent>(entity).value = "Hero";
    componentManager.getComponent<ModelComponent>(entity).assetPath = "characters/hero.fbx";
    auto &anim = componentManager.getComponent<AnimationComponent>(entity);
    anim.clipIndex = 3;
    anim.playing = false;
    anim.looping = false;
    anim.speed = 1.5f;

    std::string errorMessage;
    ASSERT_TRUE(save_all_worlds(workspaceRoot, entityManager, componentManager, &errorMessage)) << errorMessage;

    EntityManager loadedEntityManager;
    ComponentManager loadedComponentManager(&loadedEntityManager);
    const auto loadedWorlds =
        load_all_worlds(workspaceRoot, loadedEntityManager, loadedComponentManager, &errorMessage);
    ASSERT_EQ(loadedWorlds.size(), 1U) << errorMessage;

    std::optional<Entity::EntityId> loadedEntity;
    for (const auto candidate : loadedEntityManager.getActiveEntities())
    {
      if (loadedComponentManager.hasComponent<ModelComponent>(candidate))
      {
        loadedEntity = candidate;
        break;
      }
    }
    ASSERT_TRUE(loadedEntity.has_value());

    EXPECT_EQ(
        loadedComponentManager.getComponent<ModelComponent>(*loadedEntity).assetPath,
        "characters/hero.fbx");
    const auto &loadedAnim = loadedComponentManager.getComponent<AnimationComponent>(*loadedEntity);
    EXPECT_EQ(loadedAnim.clipIndex, 3);
    EXPECT_FALSE(loadedAnim.playing);
    EXPECT_FALSE(loadedAnim.looping);
    EXPECT_FLOAT_EQ(loadedAnim.speed, 1.5f);
  }
}
