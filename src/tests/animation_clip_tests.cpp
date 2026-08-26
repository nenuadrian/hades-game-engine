#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../engine/animation/animation_clip.hpp"
#include "../engine/animation/animation_types.hpp"
#include "../engine/animation/skeleton.hpp"
#include "../engine/assets/model_asset.hpp"

namespace hades
{
  namespace
  {
    constexpr float kTolerance = 1e-4f;
    constexpr float kQuarterTurn = 1.5707963f;

    bool vec3_close(const math::Vec3 &a, const math::Vec3 &b, float tolerance = kTolerance)
    {
      return std::fabs(a.x - b.x) <= tolerance &&
             std::fabs(a.y - b.y) <= tolerance &&
             std::fabs(a.z - b.z) <= tolerance;
    }

    /// Quaternions double-cover rotations, so q and -q describe the same
    /// orientation: compare the absolute dot product, not the components.
    bool quat_close(const math::Quat &a, const math::Quat &b, float tolerance = kTolerance)
    {
      const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
      return std::fabs(std::fabs(dot) - 1.0f) <= tolerance;
    }

    /// Two joints, with "Bone" two units above its parent so a rotation-only
    /// clip has a rest translation it could destroy.
    ModelAsset make_two_joint_model()
    {
      ModelAsset asset;
      asset.nodes.push_back({"Root", -1, math::Mat4::identity()});
      asset.nodes.push_back({"Bone", 0, math::Mat4::translate({0.0f, 2.0f, 0.0f})});
      asset.bones.push_back({1, math::Mat4::translate({0.0f, -2.0f, 0.0f})});
      asset.finalize();
      return asset;
    }
  }

  TEST(AnimationClipTest, SettingAKeyWithinTheEpsilonReplacesItAndKeepsTheSubTrackSorted)
  {
    AnimationClipAsset clip;
    // Deliberately out of order: the clip is responsible for the sorting.
    clip.set_translation_key("Bone", 1.0f, {1.0f, 0.0f, 0.0f});
    clip.set_translation_key("Bone", 0.0f, {0.0f, 0.0f, 0.0f});
    clip.set_translation_key("Bone", 0.5f, {5.0f, 0.0f, 0.0f});

    const AnimationBoneTrack *track = clip.find_track("Bone");
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->translations.size(), 3U);
    EXPECT_FLOAT_EQ(track->translations[0].time, 0.0f);
    EXPECT_FLOAT_EQ(track->translations[1].time, 0.5f);
    EXPECT_FLOAT_EQ(track->translations[2].time, 1.0f);

    // Re-keying a hair off an existing key replaces it in place: auto-keying
    // the same frame repeatedly must not drift the key off the frame grid.
    clip.set_translation_key(
        "Bone", 0.5f + AnimationClipAsset::kKeyEpsilon * 0.5f, {9.0f, 0.0f, 0.0f}, Interpolation::Step);

    track = clip.find_track("Bone");
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->translations.size(), 3U);
    EXPECT_FLOAT_EQ(track->translations[1].time, 0.5f);
    EXPECT_NEAR(track->translations[1].value.x, 9.0f, kTolerance);
    EXPECT_EQ(track->translations[1].interpolation, Interpolation::Step);
    EXPECT_EQ(clip.key_index_at("Bone", TrackChannel::Translation, 0.5f), 1);
    EXPECT_EQ(clip.key_index_at("Bone", TrackChannel::Translation, 0.25f), -1);
    EXPECT_EQ(clip.key_index_at("Missing", TrackChannel::Translation, 0.5f), -1);

    // A key far enough away is a new key, not a replacement.
    clip.set_translation_key("Bone", 0.5f + 0.01f, {3.0f, 0.0f, 0.0f});
    ASSERT_EQ(clip.find_track("Bone")->translations.size(), 4U);
    EXPECT_EQ(clip.total_key_count(), 4U);
  }

  TEST(AnimationClipTest, MovingAKeyOntoAnotherReplacesTheKeyAtTheDestination)
  {
    AnimationClipAsset clip;
    clip.set_translation_key("Bone", 0.0f, {0.0f, 0.0f, 0.0f});
    clip.set_translation_key("Bone", 0.5f, {5.0f, 0.0f, 0.0f});
    clip.set_translation_key("Bone", 1.0f, {1.0f, 0.0f, 0.0f});

    // The dope sheet cannot show two keys on one frame, so the mover wins.
    EXPECT_NEAR(clip.move_key("Bone", TrackChannel::Translation, 0.5f, 1.0f), 1.0f, kTolerance);
    const AnimationBoneTrack *track = clip.find_track("Bone");
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->translations.size(), 2U);
    EXPECT_FLOAT_EQ(track->translations[1].time, 1.0f);
    EXPECT_NEAR(track->translations[1].value.x, 5.0f, kTolerance);

    // Moving onto empty time keeps the key and re-sorts the sub-track.
    EXPECT_NEAR(clip.move_key("Bone", TrackChannel::Translation, 0.0f, 2.0f), 2.0f, kTolerance);
    track = clip.find_track("Bone");
    ASSERT_EQ(track->translations.size(), 2U);
    EXPECT_FLOAT_EQ(track->translations[0].time, 1.0f);
    EXPECT_FLOAT_EQ(track->translations[1].time, 2.0f);
    EXPECT_NEAR(track->translations[0].value.x, 5.0f, kTolerance);
    EXPECT_NEAR(track->translations[1].value.x, 0.0f, kTolerance);

    // Nothing at the source time, and an unknown track, both report failure.
    EXPECT_LT(clip.move_key("Bone", TrackChannel::Translation, 0.25f, 0.75f), 0.0f);
    EXPECT_LT(clip.move_key("Bone", TrackChannel::Rotation, 1.0f, 0.5f), 0.0f);
    EXPECT_LT(clip.move_key("Missing", TrackChannel::Translation, 1.0f, 0.5f), 0.0f);

    EXPECT_TRUE(clip.remove_key("Bone", TrackChannel::Translation, 2.0f));
    EXPECT_FALSE(clip.remove_key("Bone", TrackChannel::Translation, 2.0f));
    EXPECT_EQ(clip.find_track("Bone")->translations.size(), 1U);
  }

  TEST(AnimationClipTest, SamplingHoldsTheFirstKeyBeforeItAndTheLastKeyAfterIt)
  {
    const ModelAsset asset = make_two_joint_model();
    const Skeleton skeleton = Skeleton::from_model(asset);
    const int joint = skeleton.find("Bone");
    ASSERT_GE(joint, 0);
    const std::size_t index = static_cast<std::size_t>(joint);

    AnimationClipAsset clip;
    clip.duration = 1.0f;
    clip.set_translation_key("Bone", 0.25f, {1.0f, 0.0f, 0.0f});
    clip.set_translation_key("Bone", 0.75f, {3.0f, 0.0f, 0.0f});

    Pose pose = skeleton.rest_pose();
    clip.sample(skeleton, 0.0f, pose);
    EXPECT_TRUE(vec3_close(pose.translations[index], {1.0f, 0.0f, 0.0f}));

    clip.sample(skeleton, 1.0f, pose);
    EXPECT_TRUE(vec3_close(pose.translations[index], {3.0f, 0.0f, 0.0f}));

    // Out-of-range times clamp into the clip rather than extrapolating.
    clip.sample(skeleton, 25.0f, pose);
    EXPECT_TRUE(vec3_close(pose.translations[index], {3.0f, 0.0f, 0.0f}));
    clip.sample(skeleton, -5.0f, pose);
    EXPECT_TRUE(vec3_close(pose.translations[index], {1.0f, 0.0f, 0.0f}));

    // A joint the skeleton does not have is skipped, not an error: that is
    // what makes a clip retargetable. Asserting the pose size alone would
    // pass for a sample() that did nothing at all, so check that the joints
    // this skeleton does have came out exactly as they would have without
    // the stray track: "Bone" keyed, "Root" still at its rest translation.
    const int root = skeleton.find("Root");
    ASSERT_GE(root, 0);
    const std::size_t rootIndex = static_cast<std::size_t>(root);
    const math::Vec3 rootRest = pose.translations[rootIndex];

    clip.set_translation_key("NotOnThisSkeleton", 0.0f, {7.0f, 7.0f, 7.0f});
    clip.sample(skeleton, 0.5f, pose);
    EXPECT_EQ(pose.translations.size(), skeleton.size());
    EXPECT_TRUE(vec3_close(pose.translations[index], {2.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(vec3_close(pose.translations[rootIndex], rootRest));
  }

  TEST(AnimationClipTest, LinearInterpolationReachesTheExactMidpointBetweenTwoKeys)
  {
    const ModelAsset asset = make_two_joint_model();
    const Skeleton skeleton = Skeleton::from_model(asset);
    // find() returns -1 for a joint this skeleton does not have, and casting
    // that to size_t indexes the pose far out of bounds — check it first so a
    // regression fails the test instead of crashing the whole binary.
    const int joint = skeleton.find("Bone");
    ASSERT_GE(joint, 0);
    const std::size_t index = static_cast<std::size_t>(joint);

    AnimationClipAsset clip;
    clip.duration = 1.0f;
    clip.set_translation_key("Bone", 0.0f, {0.0f, 0.0f, 0.0f}, Interpolation::Linear);
    clip.set_translation_key("Bone", 1.0f, {2.0f, 4.0f, 6.0f}, Interpolation::Linear);

    Pose pose = skeleton.rest_pose();
    clip.sample(skeleton, 0.5f, pose);
    EXPECT_NEAR(pose.translations[index].x, 1.0f, kTolerance);
    EXPECT_NEAR(pose.translations[index].y, 2.0f, kTolerance);
    EXPECT_NEAR(pose.translations[index].z, 3.0f, kTolerance);

    clip.sample(skeleton, 0.25f, pose);
    EXPECT_NEAR(pose.translations[index].x, 0.5f, kTolerance);
  }

  TEST(AnimationClipTest, StepInterpolationHoldsTheLeftValueRightUpToTheNextKey)
  {
    const ModelAsset asset = make_two_joint_model();
    const Skeleton skeleton = Skeleton::from_model(asset);
    const int joint = skeleton.find("Bone");
    ASSERT_GE(joint, 0);
    const std::size_t index = static_cast<std::size_t>(joint);

    AnimationClipAsset clip;
    clip.duration = 1.0f;
    clip.set_translation_key("Bone", 0.0f, {0.0f, 0.0f, 0.0f}, Interpolation::Step);
    clip.set_translation_key("Bone", 1.0f, {10.0f, 0.0f, 0.0f}, Interpolation::Step);

    Pose pose = skeleton.rest_pose();
    for (const float time : {0.0f, 0.25f, 0.5f, 0.9f, 0.999f})
    {
      clip.sample(skeleton, time, pose);
      EXPECT_NEAR(pose.translations[index].x, 0.0f, kTolerance) << "held value at t=" << time;
    }

    // The next key takes over the instant the play head reaches it.
    clip.sample(skeleton, 1.0f, pose);
    EXPECT_NEAR(pose.translations[index].x, 10.0f, kTolerance);

    EXPECT_NEAR(apply_easing(Interpolation::Step, 0.99f), 0.0f, kTolerance);
  }

  TEST(AnimationClipTest, EaseInOutIsMonotonePassesThroughZeroAndOneAndLagsLinearInTheFirstHalf)
  {
    EXPECT_NEAR(apply_easing(Interpolation::EaseInOut, 0.0f), 0.0f, kTolerance);
    EXPECT_NEAR(apply_easing(Interpolation::EaseInOut, 1.0f), 1.0f, kTolerance);
    // The curve is symmetric about its own midpoint.
    EXPECT_NEAR(apply_easing(Interpolation::EaseInOut, 0.5f), 0.5f, 1e-3f);

    float previous = 0.0f;
    for (int step = 1; step <= 32; ++step)
    {
      const float t = static_cast<float>(step) / 32.0f;
      const float eased = apply_easing(Interpolation::EaseInOut, t);

      EXPECT_GE(eased, previous - kTolerance) << "not monotone at t=" << t;
      EXPECT_GE(eased, -kTolerance);
      EXPECT_LE(eased, 1.0f + kTolerance);
      if (t < 0.49f)
      {
        EXPECT_LT(eased, t) << "ease-in-out should lag linear before the midpoint";
      }
      previous = eased;
    }

    // Out-of-range parameters clamp instead of overshooting the segment.
    EXPECT_NEAR(apply_easing(Interpolation::EaseInOut, -3.0f), 0.0f, kTolerance);
    EXPECT_NEAR(apply_easing(Interpolation::EaseInOut, 3.0f), 1.0f, kTolerance);
    EXPECT_NEAR(apply_easing(Interpolation::Linear, 0.3f), 0.3f, kTolerance);
  }

  TEST(AnimationClipTest, ARotationOnlyTrackLeavesTranslationAndScaleAtTheRestValue)
  {
    const ModelAsset asset = make_two_joint_model();
    const Skeleton skeleton = Skeleton::from_model(asset);
    const int joint = skeleton.find("Bone");
    ASSERT_GE(joint, 0);
    const std::size_t index = static_cast<std::size_t>(joint);
    ASSERT_TRUE(vec3_close(skeleton.joint(index).restTranslation, {0.0f, 2.0f, 0.0f}));

    const math::Quat turn = math::Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, kQuarterTurn);

    AnimationClipAsset clip;
    clip.duration = 1.0f;
    clip.set_rotation_key("Bone", 0.0f, math::Quat{});
    clip.set_rotation_key("Bone", 1.0f, turn);
    ASSERT_TRUE(clip.find_track("Bone")->translations.empty());

    Pose pose = skeleton.rest_pose();
    clip.sample(skeleton, 1.0f, pose);

    EXPECT_TRUE(quat_close(pose.rotations[index], turn));
    // The regression that matters: an unkeyed channel keeps whatever the
    // pose already held, so a rotation-only clip must not teleport the joint
    // to the origin.
    EXPECT_TRUE(vec3_close(pose.translations[index], {0.0f, 2.0f, 0.0f}));
    EXPECT_TRUE(vec3_close(pose.scales[index], {1.0f, 1.0f, 1.0f}));

    // Same story once the pose has been composed up the hierarchy.
    std::vector<math::Mat4> globals;
    skeleton.local_to_global(pose, globals);
    ASSERT_EQ(globals.size(), skeleton.size());
    EXPECT_NEAR(globals[index].m[3][0], 0.0f, kTolerance);
    EXPECT_NEAR(globals[index].m[3][1], 2.0f, kTolerance);
    EXPECT_NEAR(globals[index].m[3][2], 0.0f, kTolerance);
  }

  TEST(AnimationClipTest, EventsFireExactlyOnceAcrossAWrappedLoopAndNeverOnAZeroLengthWindow)
  {
    AnimationClipAsset clip;
    clip.duration = 1.0f;
    clip.events.push_back({0.1f, "start", "", 0.0f});
    clip.events.push_back({0.5f, "middle", "", 0.0f});
    clip.events.push_back({0.9f, "end", "", 0.0f});

    std::vector<const AnimationEventKey *> fired;

    // A paused frame advances nothing, so it fires nothing.
    clip.events_in_range(0.5f, 0.5f, false, fired);
    EXPECT_TRUE(fired.empty());
    clip.events_in_range(0.5f, 0.5f, true, fired);
    EXPECT_TRUE(fired.empty());

    // Time going backwards without a loop is a scrub, not playback.
    clip.events_in_range(0.9f, 0.2f, false, fired);
    EXPECT_TRUE(fired.empty());

    // One full pass at 0.25s a frame: every event fires exactly once, the
    // wrapping frame included.
    std::map<std::string, int> counts;
    float time = 0.0f;
    for (int frame = 0; frame < 4; ++frame)
    {
      const float advanced = time + 0.25f;
      const bool looped = advanced >= clip.duration;
      const float next = looped ? advanced - clip.duration : advanced;

      clip.events_in_range(time, next, looped, fired);
      for (const AnimationEventKey *event : fired)
      {
        ++counts[event->name];
      }
      time = next;
    }
    EXPECT_NEAR(time, 0.0f, kTolerance);
    EXPECT_EQ(counts["start"], 1);
    EXPECT_EQ(counts["middle"], 1);
    EXPECT_EQ(counts["end"], 1);

    // A wrapped window reports in play order: the tail of the clip, then its
    // head.
    clip.events_in_range(0.8f, 0.2f, true, fired);
    ASSERT_EQ(fired.size(), 2U);
    EXPECT_EQ(fired[0]->name, "end");
    EXPECT_EQ(fired[1]->name, "start");
  }

  TEST(AnimationClipTest, JsonRoundTripPreservesEveryFieldIncludingEasingAndBezierControlPoints)
  {
    AnimationClipAsset clip;
    clip.name = "attack";
    clip.sourceModel = "characters/hero.fbx";
    clip.duration = 2.5f;
    clip.frameRate = 24.0f;
    clip.looping = false;
    clip.additive = true;
    clip.additiveReferenceTime = 0.25f;

    const EaseCurve curve{0.17f, 0.67f, 0.83f, 0.13f};
    clip.set_translation_key("Bone", 0.0f, {1.0f, 2.0f, 3.0f}, Interpolation::Bezier, curve);
    clip.set_translation_key("Bone", 1.5f, {4.0f, 5.0f, 6.0f}, Interpolation::Step);
    clip.set_rotation_key(
        "Bone", 0.5f, math::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, kQuarterTurn), Interpolation::EaseInOut);
    clip.set_scale_key("Spine", 0.25f, {2.0f, 2.0f, 2.0f}, Interpolation::EaseIn);
    clip.events.push_back({1.0f, "footstep", "left", 0.75f});

    AnimationClipAsset restored;
    std::string error;
    ASSERT_TRUE(AnimationClipAsset::from_json(clip.to_json(), restored, &error)) << error;

    EXPECT_EQ(restored.name, clip.name);
    EXPECT_EQ(restored.sourceModel, clip.sourceModel);
    EXPECT_NEAR(restored.duration, clip.duration, kTolerance);
    EXPECT_NEAR(restored.frameRate, clip.frameRate, kTolerance);
    EXPECT_FALSE(restored.looping);
    EXPECT_TRUE(restored.additive);
    EXPECT_NEAR(restored.additiveReferenceTime, 0.25f, kTolerance);
    ASSERT_EQ(restored.tracks.size(), 2U);

    const AnimationBoneTrack *bone = restored.find_track("Bone");
    ASSERT_NE(bone, nullptr);
    ASSERT_EQ(bone->translations.size(), 2U);
    EXPECT_NEAR(bone->translations[0].time, 0.0f, kTolerance);
    EXPECT_TRUE(vec3_close(bone->translations[0].value, {1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(bone->translations[0].interpolation, Interpolation::Bezier);
    EXPECT_NEAR(bone->translations[0].ease.x1, curve.x1, kTolerance);
    EXPECT_NEAR(bone->translations[0].ease.y1, curve.y1, kTolerance);
    EXPECT_NEAR(bone->translations[0].ease.x2, curve.x2, kTolerance);
    EXPECT_NEAR(bone->translations[0].ease.y2, curve.y2, kTolerance);
    EXPECT_EQ(bone->translations[1].interpolation, Interpolation::Step);
    EXPECT_TRUE(vec3_close(bone->translations[1].value, {4.0f, 5.0f, 6.0f}));

    ASSERT_EQ(bone->rotations.size(), 1U);
    EXPECT_NEAR(bone->rotations[0].time, 0.5f, kTolerance);
    EXPECT_EQ(bone->rotations[0].interpolation, Interpolation::EaseInOut);
    EXPECT_TRUE(quat_close(bone->rotations[0].value, clip.find_track("Bone")->rotations[0].value));

    const AnimationBoneTrack *spine = restored.find_track("Spine");
    ASSERT_NE(spine, nullptr);
    ASSERT_EQ(spine->scales.size(), 1U);
    EXPECT_TRUE(vec3_close(spine->scales[0].value, {2.0f, 2.0f, 2.0f}));
    EXPECT_EQ(spine->scales[0].interpolation, Interpolation::EaseIn);

    ASSERT_EQ(restored.events.size(), 1U);
    EXPECT_NEAR(restored.events[0].time, 1.0f, kTolerance);
    EXPECT_EQ(restored.events[0].name, "footstep");
    EXPECT_EQ(restored.events[0].stringValue, "left");
    EXPECT_NEAR(restored.events[0].floatValue, 0.75f, kTolerance);

    // A document that is not an object is rejected with a message.
    AnimationClipAsset rejected;
    std::string rejectError;
    EXPECT_FALSE(AnimationClipAsset::from_json(nlohmann::json::array(), rejected, &rejectError));
    EXPECT_FALSE(rejectError.empty());
  }

  TEST(AnimationClipTest, BakeFromModelBindsImportedChannelsByNodeName)
  {
    ModelAsset asset;
    asset.nodes.push_back({"Root", -1, math::Mat4::identity()});
    asset.nodes.push_back({"Bone", 0, math::Mat4::translate({0.0f, 2.0f, 0.0f})});
    asset.bones.push_back({1, math::Mat4::identity()});

    AnimationClip imported;
    imported.name = "spin";
    imported.duration = 2.0f;
    AnimationChannel channel;
    channel.nodeIndex = 1;
    channel.rotations = {
        {0.0f, math::Quat{}},
        {2.0f, math::Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, kQuarterTurn)}};
    imported.channels.push_back(channel);
    asset.clips.push_back(imported);
    asset.finalize();

    AnimationClipAsset baked;
    ASSERT_TRUE(AnimationClipAsset::bake_from_model(asset, 0, baked));
    EXPECT_EQ(baked.name, "spin");
    EXPECT_NEAR(baked.duration, 2.0f, kTolerance);
    ASSERT_EQ(baked.tracks.size(), 1U);
    // Bound by NAME, so a re-import that renumbers the nodes still plays.
    EXPECT_EQ(baked.tracks[0].bone, "Bone");
    EXPECT_EQ(baked.tracks[0].rotations.size(), 2U);
    EXPECT_TRUE(baked.tracks[0].translations.empty());
    EXPECT_TRUE(baked.tracks[0].scales.empty());
    EXPECT_EQ(baked.find_track("Root"), nullptr);

    // The baked clip plays back through the skeleton it came from.
    const Skeleton skeleton = Skeleton::from_model(asset);
    Pose pose = skeleton.rest_pose();
    baked.sample(skeleton, 2.0f, pose);
    const int joint = skeleton.find("Bone");
    ASSERT_GE(joint, 0);
    const std::size_t index = static_cast<std::size_t>(joint);
    EXPECT_TRUE(quat_close(pose.rotations[index], imported.channels[0].rotations[1].value));
    EXPECT_TRUE(vec3_close(pose.translations[index], {0.0f, 2.0f, 0.0f}));

    EXPECT_FALSE(AnimationClipAsset::bake_from_model(asset, 7, baked));
    EXPECT_FALSE(AnimationClipAsset::bake_from_model(asset, -1, baked));
  }

  TEST(AnimationClipTest, BakeReplacesATrackWhenTwoChannelsResolveToTheSameNode)
  {
    // The loader binds channels through a first-name-wins name map, so a file
    // with two nodes called "Bone" hands BOTH channels node 1. Appending the
    // second onto the first leaves a track whose keys alternate between two
    // unrelated curves at identical times, and the binary search then walks a
    // zig-zag neither channel describes.
    ModelAsset asset;
    asset.nodes.push_back({"Root", -1, math::Mat4::identity()});
    asset.nodes.push_back({"Bone", 0, math::Mat4::translate({1.0f, 0.0f, 0.0f})});
    asset.nodes.push_back({"Bone", 0, math::Mat4::translate({1.0f, 0.0f, 0.0f})});
    asset.bones.push_back({1, math::Mat4::identity()});

    AnimationClip imported;
    imported.name = "wave";
    imported.duration = 1.0f;

    AnimationChannel first;
    first.nodeIndex = 1;
    first.positions = {{0.0f, {1.0f, 0.0f, 0.0f}}, {1.0f, {1.0f, -5.0f, 0.0f}}};
    imported.channels.push_back(first);

    AnimationChannel second;
    second.nodeIndex = 1;
    second.positions = {{0.0f, {-1.0f, 0.0f, 0.0f}}, {1.0f, {-1.0f, 5.0f, 0.0f}}};
    imported.channels.push_back(second);

    asset.clips.push_back(imported);
    asset.finalize();

    AnimationClipAsset baked;
    ASSERT_TRUE(AnimationClipAsset::bake_from_model(asset, 0, baked));
    const AnimationBoneTrack *track = baked.find_track("Bone");
    ASSERT_NE(track, nullptr);
    // Two keys, not four: the later channel replaced the earlier one.
    ASSERT_EQ(track->translations.size(), 2U);
    EXPECT_TRUE(vec3_close(track->translations[0].value, {-1.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(vec3_close(track->translations[1].value, {-1.0f, 5.0f, 0.0f}));

    // "Last channel wins" is the same rule the imported preview applies:
    // evaluateNodeGlobals rebuilds the node's whole local matrix per channel.
    const Skeleton skeleton = Skeleton::from_model(asset);
    Pose pose = skeleton.rest_pose();
    std::vector<math::Mat4> globals;
    for (float time = 0.0f; time <= 1.0f + kTolerance; time += 0.25f)
    {
      baked.sample(skeleton, time, pose);
      asset.evaluateNodeGlobals(&asset.clips[0], time, globals);
      const math::Vec3 imported_value{globals[1].m[3][0], globals[1].m[3][1], globals[1].m[3][2]};
      EXPECT_TRUE(vec3_close(pose.translations[1], imported_value))
          << "t=" << time << " baked y=" << pose.translations[1].y << " imported y=" << imported_value.y;
    }
  }

  TEST(AnimationClipTest, RestPoseClipKeysEveryJointAndMaintenanceCleansUpTheTimeline)
  {
    const ModelAsset asset = make_two_joint_model();
    const Skeleton skeleton = Skeleton::from_model(asset);

    AnimationClipAsset clip = AnimationClipAsset::from_rest_pose(skeleton, "rest");
    EXPECT_EQ(clip.name, "rest");
    ASSERT_EQ(clip.tracks.size(), skeleton.size());
    const AnimationBoneTrack *bone = clip.find_track("Bone");
    ASSERT_NE(bone, nullptr);
    ASSERT_EQ(bone->translations.size(), 1U);
    EXPECT_TRUE(vec3_close(bone->translations[0].value, {0.0f, 2.0f, 0.0f}));

    // Duration only ever grows: an author may want trailing hold time.
    clip.duration = 1.0f;
    clip.set_translation_key("Bone", 3.0f, {0.0f, 5.0f, 0.0f});
    clip.recompute_duration();
    EXPECT_NEAR(clip.duration, 3.0f, kTolerance);
    clip.duration = 9.0f;
    clip.recompute_duration();
    EXPECT_NEAR(clip.duration, 9.0f, kTolerance);
    EXPECT_NEAR(clip.last_key_time(), 3.0f, kTolerance);

    clip.track_for("Empty");
    EXPECT_NE(clip.find_track("Empty"), nullptr);
    clip.prune_empty_tracks();
    EXPECT_EQ(clip.find_track("Empty"), nullptr);

    std::vector<float> times;
    clip.key_times(times);
    ASSERT_EQ(times.size(), 2U);
    EXPECT_NEAR(times[0], 0.0f, kTolerance);
    EXPECT_NEAR(times[1], 3.0f, kTolerance);

    clip.remove_track("Bone");
    EXPECT_EQ(clip.find_track("Bone"), nullptr);
  }
}
