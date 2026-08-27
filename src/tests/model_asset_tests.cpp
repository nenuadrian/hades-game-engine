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
  using test_support::write_skinned_gltf;
  using test_support::write_text_file;

  namespace
  {
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
    // Added explicitly: the Model preset now ships an AnimatorComponent. The
    // legacy clip player still loads and still runs, which is what this test
    // covers.
    componentManager.addComponent(entity, AnimationComponent{});

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
    // Added explicitly: the Model preset now ships an AnimatorComponent, and
    // AnimationComponent is a load-and-edit legacy path rather than something
    // new entities are given. Its serialization still has to round-trip.
    componentManager.addComponent(entity, AnimationComponent{});
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
