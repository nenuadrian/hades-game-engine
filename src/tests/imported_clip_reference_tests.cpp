#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <string>

#include "test_support.hpp"

#include "../engine/animation/animation_clip.hpp"
#include "../engine/animation/animation_clip_cache.hpp"
#include "../engine/animation/animator_instance.hpp"
#include "../engine/animation/skeleton.hpp"
#include "../engine/assets/model_asset.hpp"
#include "../engine/animation/animation_runtime.hpp"
#include "../engine/assets/model_asset_cache.hpp"
#include "../engine/components/animator_component.hpp"
#include "../engine/components/model_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/systems/animator_system.hpp"

namespace hades
{
  using test_support::ScopedDirectoryCleanup;
  using test_support::unique_test_directory;
  using test_support::write_skinned_gltf;

  namespace
  {
    /// The fixture glTF carries one animation, "spin", which turns Bone1 90
    /// degrees about Z over one second.
    constexpr const char *kModel = "model.gltf";
    constexpr const char *kImported = "model.gltf#spin";

    /// Point both caches at a fresh directory and put them back afterwards.
    /// setAssetRoot drops every entry, so this doubles as the isolation
    /// between one test and the next.
    struct ScopedAssetRoots
    {
      explicit ScopedAssetRoots(const std::filesystem::path &root)
          : previousClips(AnimationClipCache::instance().assetRoot()),
            previousModels(ModelAssetCache::instance().assetRoot())
      {
        AnimationClipCache::instance().setAssetRoot(root);
        ModelAssetCache::instance().setAssetRoot(root);
      }

      ~ScopedAssetRoots()
      {
        AnimationClipCache::instance().setAssetRoot(previousClips);
        ModelAssetCache::instance().setAssetRoot(previousModels);
      }

      std::filesystem::path previousClips;
      std::filesystem::path previousModels;
    };
  }

  TEST(ImportedClipReferenceTest, SplitAcceptsAModelAndClipAndRejectsEverythingElse)
  {
    std::string model;
    std::string clip;

    EXPECT_TRUE(AnimationClipCache::split_imported_reference("character.fbx#Walk", model, clip));
    EXPECT_EQ(model, "character.fbx");
    EXPECT_EQ(clip, "Walk");

    EXPECT_TRUE(AnimationClipCache::split_imported_reference("Models/a.gltf#Idle 01", model, clip));
    EXPECT_EQ(model, "Models/a.gltf");
    EXPECT_EQ(clip, "Idle 01");

    // An authored reference must stay one, or every existing clip name that
    // happens to contain no '#' would still be routed through the model path.
    EXPECT_FALSE(AnimationClipCache::split_imported_reference("run", model, clip));
    EXPECT_FALSE(AnimationClipCache::split_imported_reference("Clips/run.json", model, clip));
    // Half a reference names nothing, and letting it through would resolve to
    // a path with a '#' in it and fail far from the mistake.
    EXPECT_FALSE(AnimationClipCache::split_imported_reference("#Walk", model, clip));
    EXPECT_FALSE(AnimationClipCache::split_imported_reference("model.gltf#", model, clip));
  }

  TEST(ImportedClipReferenceTest, ClipInsideAModelResolvesWithoutBeingBakedToDisk)
  {
    const std::filesystem::path root = unique_test_directory("hades-imported-clip");
    std::filesystem::create_directories(root);
    ScopedDirectoryCleanup cleanup(root);
    write_skinned_gltf(root);
    ScopedAssetRoots roots(root);

    const AnimationClipAsset *clip = AnimationClipCache::instance().clip(kImported);
    ASSERT_NE(clip, nullptr);
    EXPECT_EQ(clip->name, "spin");
    EXPECT_EQ(clip->sourceModel, kModel);
    EXPECT_GT(clip->duration, 0.0f);

    // Bound by joint NAME, which is what makes an imported clip retargetable
    // and what an index-addressed AnimationComponent can never be.
    ASSERT_FALSE(clip->tracks.empty());
    bool boundToBone1 = false;
    for (const AnimationBoneTrack &track : clip->tracks)
    {
      boundToBone1 = boundToBone1 || track.bone == "Bone1";
    }
    EXPECT_TRUE(boundToBone1);

    // Nothing was written: the point is that the animator reads the model
    // file directly rather than requiring a bake into .hades/animations.
    EXPECT_FALSE(std::filesystem::exists(
        AnimationClipCache::clips_directory(root) / "spin.json"));

    // Memoised, not re-baked per call.
    EXPECT_EQ(AnimationClipCache::instance().clip(kImported), clip);
  }

  TEST(ImportedClipReferenceTest, AnAnimatorPlaysAnImportedClipAndPosesTheSkeleton)
  {
    const std::filesystem::path root = unique_test_directory("hades-imported-play");
    std::filesystem::create_directories(root);
    ScopedDirectoryCleanup cleanup(root);
    write_skinned_gltf(root);
    ScopedAssetRoots roots(root);

    const ModelAsset *asset = ModelAssetCache::instance().get(kModel);
    ASSERT_NE(asset, nullptr);
    ASSERT_FALSE(asset->clips.empty());

    AnimatorInstance instance;
    instance.play_clip(kImported, 0.0f, true);
    instance.update(0.0f, *asset, AnimationClipCache::instance());

    EXPECT_EQ(instance.current_clip(), kImported);
    const Pose restPose = Skeleton::from_model(*asset).rest_pose();
    const std::size_t bone1 = static_cast<std::size_t>(Skeleton::from_model(*asset).find("Bone1"));
    ASSERT_LT(bone1, restPose.size());

    // A quarter of the way through the spin the joint must have left its rest
    // rotation. Without imported references this whole path required the clip
    // to be copied into .hades/animations first.
    instance.update(0.25f, *asset, AnimationClipCache::instance());
    const math::Quat posed = instance.pose().rotations[bone1];
    const math::Quat rest = restPose.rotations[bone1];
    const float dot = std::fabs((posed.x * rest.x) + (posed.y * rest.y) +
                                (posed.z * rest.z) + (posed.w * rest.w));
    EXPECT_LT(dot, 0.999f);
  }

  TEST(ImportedClipReferenceTest, AMissingClipNameReportsWhichModelWasSearched)
  {
    const std::filesystem::path root = unique_test_directory("hades-imported-missing");
    std::filesystem::create_directories(root);
    ScopedDirectoryCleanup cleanup(root);
    write_skinned_gltf(root);
    ScopedAssetRoots roots(root);

    EXPECT_EQ(AnimationClipCache::instance().clip("model.gltf#nope"), nullptr);
    const std::string error = AnimationClipCache::instance().errorFor("model.gltf#nope");
    EXPECT_NE(error.find("model.gltf"), std::string::npos);
    EXPECT_NE(error.find("nope"), std::string::npos);
  }

  TEST(ImportedClipReferenceTest, AnImportedReferenceIsReadOnly)
  {
    const std::filesystem::path root = unique_test_directory("hades-imported-readonly");
    std::filesystem::create_directories(root);
    ScopedDirectoryCleanup cleanup(root);
    write_skinned_gltf(root);
    ScopedAssetRoots roots(root);

    // Saving would have to either edit the model file or write a JSON clip
    // under a name that still reads back as the model's own animation.
    std::string error;
    EXPECT_FALSE(AnimationClipCache::instance().saveClip(kImported, AnimationClipAsset{}, &error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(AnimationClipCache::instance().deleteClip(kImported, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(std::filesystem::exists(root / kModel));
  }

  TEST(ImportedClipReferenceTest, ListingOffersEveryNamedAnimationInTheModel)
  {
    const std::filesystem::path root = unique_test_directory("hades-imported-list");
    std::filesystem::create_directories(root);
    ScopedDirectoryCleanup cleanup(root);
    write_skinned_gltf(root);
    ScopedAssetRoots roots(root);

    const std::vector<std::string> references =
        AnimationClipCache::instance().listImportedClips(kModel);
    ASSERT_EQ(references.size(), 1u);
    EXPECT_EQ(references.front(), kImported);

    EXPECT_TRUE(AnimationClipCache::instance().listImportedClips("missing.gltf").empty());
    EXPECT_TRUE(AnimationClipCache::instance().listImportedClips("").empty());
  }

  TEST(ImportedClipReferenceTest, AnAnimatorNamingNothingStartsTheModelsOwnFirstAnimation)
  {
    const std::filesystem::path root = unique_test_directory("hades-imported-default");
    std::filesystem::create_directories(root);
    ScopedDirectoryCleanup cleanup(root);
    write_skinned_gltf(root);
    ScopedAssetRoots roots(root);
    AnimationRuntime::instance().clear();

    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);
    const Entity::EntityId entity = entityManager.createEntity();
    ModelComponent model;
    model.assetPath = kModel;
    componentManager.addComponent(entity, model);
    // Neither a graph nor a default clip: the zero-configuration case the
    // superseded AnimationComponent used to cover by starting at clipIndex 0.
    // Without the fallback a freshly created Model entity would sit on its
    // bind pose with nothing saying why.
    componentManager.addComponent(entity, AnimatorComponent{});

    AnimatorSystem system;
    system.update(0.016f, componentManager, entityManager);

    const AnimatorInstance *instance = AnimationRuntime::instance().find(entity);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(instance->current_clip(), kImported);

    AnimationRuntime::instance().clear();
  }
}
