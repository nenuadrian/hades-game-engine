#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "test_support.hpp"

#include "../engine/animation/animation_clip.hpp"
#include "../engine/animation/animation_clip_cache.hpp"
#include "../engine/animation/animation_runtime.hpp"
#include "../engine/animation/animator_graph.hpp"
#include "../engine/animation/animator_instance.hpp"
#include "../engine/animation/script_animation.hpp"
#include "../engine/animation/skeleton.hpp"
#include "../engine/assets/model_asset.hpp"

namespace hades
{
  using test_support::ScopedDirectoryCleanup;
  using test_support::unique_test_directory;

  namespace
  {
    constexpr float kTolerance = 1e-4f;
    constexpr float kQuarterTurn = 1.5707963f;

    /// Points the process-wide clip cache at a temp root for one test and
    /// puts back whatever the rest of the suite was using. The instance
    /// reaches for the singleton on its query paths, so the root cannot just
    /// be a local object.
    struct ScopedClipRoot
    {
      explicit ScopedClipRoot(const std::filesystem::path &root)
          : previous(AnimationClipCache::instance().assetRoot())
      {
        AnimationClipCache::instance().setAssetRoot(root);
      }

      ~ScopedClipRoot()
      {
        // setAssetRoot() drops every cached entry, so this also clears the
        // clips this test wrote.
        AnimationClipCache::instance().setAssetRoot(previous);
      }

      std::filesystem::path previous;
    };

    ModelAsset make_model()
    {
      ModelAsset asset;
      asset.nodes.push_back({"Root", -1, math::Mat4::identity()});
      asset.nodes.push_back({"Bone", 0, math::Mat4::translate({0.0f, 1.0f, 0.0f})});
      asset.bones.push_back({1, math::Mat4::translate({0.0f, -1.0f, 0.0f})});
      asset.finalize();
      return asset;
    }

    /// A clip that actually drives the one joint of make_model(), so the
    /// posed rotation is visibly different from the rest pose. Two clips made
    /// here with the same duration are identical in content, so these tests
    /// tell states apart by name and by is_transitioning(), not by pose.
    AnimationClipAsset make_clip(const std::string &name, float duration)
    {
      AnimationClipAsset clip;
      clip.name = name;
      clip.duration = duration;
      clip.frameRate = 30.0f;
      clip.looping = true;
      clip.set_rotation_key("Bone", 0.0f, math::Quat{});
      clip.set_rotation_key("Bone", duration, math::Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, kQuarterTurn));
      return clip;
    }

    AnimState make_clip_state(const std::string &name, const std::string &clip, bool looping)
    {
      AnimState state;
      state.name = name;
      state.kind = AnimStateKind::Clip;
      state.clip = clip;
      state.looping = looping;
      return state;
    }

    /// Idle -> Run, guarded by a float parameter, crossfading over 0.2s.
    AnimatorGraph make_locomotion_graph()
    {
      AnimatorGraph graph;
      graph.name = "locomotion";

      AnimParameter speed;
      speed.name = "Speed";
      speed.type = AnimParamType::Float;
      graph.parameters.push_back(speed);

      AnimLayer layer;
      layer.states.push_back(make_clip_state("Idle", "idle", true));
      layer.states.push_back(make_clip_state("Run", "run", true));
      layer.defaultState = 0;

      AnimTransition toRun;
      toRun.fromState = 0;
      toRun.toState = 1;
      toRun.duration = 0.2f;
      toRun.conditions.push_back({"Speed", AnimConditionOp::Greater, 0.5f});
      layer.transitions.push_back(toRun);

      graph.layers.push_back(layer);
      return graph;
    }

    /// Idle -> Swing on a trigger, snapping rather than blending so the test
    /// can observe the state the frame the trigger fires.
    AnimatorGraph make_attack_graph()
    {
      AnimatorGraph graph;
      graph.name = "attack";

      AnimParameter attack;
      attack.name = "Attack";
      attack.type = AnimParamType::Trigger;
      graph.parameters.push_back(attack);

      AnimLayer layer;
      layer.states.push_back(make_clip_state("Idle", "idle", true));
      layer.states.push_back(make_clip_state("Swing", "swing", false));
      layer.defaultState = 0;

      AnimTransition toSwing;
      toSwing.fromState = 0;
      toSwing.toState = 1;
      toSwing.duration = 0.0f;
      toSwing.conditions.push_back({"Attack", AnimConditionOp::IsTrue, 0.0f});
      layer.transitions.push_back(toSwing);

      graph.layers.push_back(layer);
      return graph;
    }

    /// A model whose middle node's local transform is T * S * R. The linear
    /// part is S*R rather than R*S, so decomposeTRS reports success and
    /// silently drops the shear — the shape a COLLADA `<matrix>` node (or
    /// `<scale>` written before `<rotate>`) imports as.
    ModelAsset make_sheared_model()
    {
      ModelAsset asset;
      asset.nodes.push_back({"Root", -1, math::Mat4::identity()});
      asset.nodes.push_back({"Hips", 0,
                             math::Mat4::translate({0.0f, 1.0f, 0.0f}) *
                                 math::Mat4::scaleMatrix({2.0f, 0.5f, 1.0f}) *
                                 math::Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, 0.6f).toMat4()});
      asset.nodes.push_back({"Spine", 1,
                             math::Mat4::translate({0.0f, 2.0f, 0.0f}) *
                                 math::Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, 0.3f).toMat4()});

      // Offset matrices are derived from the bind pose, which is what makes
      // the animator's rest pose and the model's bind pose comparable.
      std::vector<math::Mat4> globals;
      asset.bindPoseNodeGlobals(globals);
      asset.bones.push_back({1, globals[1].inverse()});
      asset.bones.push_back({2, globals[2].inverse()});
      asset.finalize();
      return asset;
    }

    /// A clip that holds "Bone" at one translation for its whole length, so
    /// the pose it produces identifies it regardless of playback time.
    AnimationClipAsset make_constant_clip(const std::string &name, float height)
    {
      AnimationClipAsset clip;
      clip.name = name;
      clip.duration = 1.0f;
      clip.frameRate = 30.0f;
      clip.looping = true;
      clip.set_pose_key("Bone", 0.0f, {0.0f, height, 0.0f}, math::Quat{}, {1.0f, 1.0f, 1.0f});
      clip.set_pose_key("Bone", 1.0f, {0.0f, height, 0.0f}, math::Quat{}, {1.0f, 1.0f, 1.0f});
      return clip;
    }

    /// An additive clip that is exactly its own reference frame, so its delta
    /// is zero and an additive layer playing it must change nothing at all —
    /// whatever pose or reference frame it happens to carry.
    AnimationClipAsset make_self_referencing_additive_clip(const std::string &name, float height, float angle)
    {
      AnimationClipAsset clip = make_constant_clip(name, height);
      clip.additive = true;
      clip.additiveReferenceTime = 0.0f;
      const math::Quat rotation = math::Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, angle);
      clip.set_rotation_key("Bone", 0.0f, rotation);
      clip.set_rotation_key("Bone", 1.0f, rotation);
      return clip;
    }

    /// Base layer running "idle", plus an additive layer crossfading AimA to
    /// AimB on a bool.
    AnimatorGraph make_aim_graph()
    {
      AnimatorGraph graph;
      graph.name = "aim";

      AnimParameter fire;
      fire.name = "Fire";
      fire.type = AnimParamType::Bool;
      graph.parameters.push_back(fire);

      AnimLayer base;
      base.name = "Base";
      base.states.push_back(make_clip_state("Idle", "idle", true));
      base.defaultState = 0;
      graph.layers.push_back(base);

      AnimLayer aim;
      aim.name = "Aim";
      aim.additive = true;
      aim.weight = 1.0f;
      aim.states.push_back(make_clip_state("AimA", "aimA", true));
      aim.states.push_back(make_clip_state("AimB", "aimB", true));
      aim.defaultState = 0;

      AnimTransition toB;
      toB.fromState = 0;
      toB.toState = 1;
      toB.duration = 0.5f;
      toB.conditions.push_back({"Fire", AnimConditionOp::IsTrue, 0.0f});
      aim.transitions.push_back(toB);
      graph.layers.push_back(aim);
      return graph;
    }

    /// A 2D blend tree with `count` entries evenly spread on the unit circle,
    /// naming clips "ringN". Used with a count past the contribution cap, to
    /// check that the one place the weighting still ranks entries does not
    /// reintroduce the pop the gradient band exists to remove.
    AnimatorGraph make_ring_graph(int count)
    {
      AnimatorGraph graph;
      graph.name = "ring";

      AnimParameter moveX;
      moveX.name = "MoveX";
      moveX.type = AnimParamType::Float;
      graph.parameters.push_back(moveX);
      AnimParameter moveY;
      moveY.name = "MoveY";
      moveY.type = AnimParamType::Float;
      graph.parameters.push_back(moveY);

      AnimState state;
      state.name = "Ring";
      state.kind = AnimStateKind::BlendTree2D;
      state.blendParameterX = "MoveX";
      state.blendParameterY = "MoveY";
      for (int i = 0; i < count; ++i)
      {
        const float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(count);
        AnimBlendEntry entry;
        entry.clip = "ring" + std::to_string(i);
        entry.thresholdX = std::sin(angle);
        entry.thresholdY = std::cos(angle);
        state.entries.push_back(entry);
      }

      AnimLayer layer;
      layer.states.push_back(state);
      layer.defaultState = 0;
      graph.layers.push_back(layer);
      return graph;
    }

    /// One 2D blend tree with four entries on the unit circle — the shape the
    /// docs call a strafe set.
    AnimatorGraph make_strafe_graph()
    {
      AnimatorGraph graph;
      graph.name = "strafe";

      AnimParameter moveX;
      moveX.name = "MoveX";
      moveX.type = AnimParamType::Float;
      graph.parameters.push_back(moveX);
      AnimParameter moveY;
      moveY.name = "MoveY";
      moveY.type = AnimParamType::Float;
      graph.parameters.push_back(moveY);

      AnimState state;
      state.name = "Strafe";
      state.kind = AnimStateKind::BlendTree2D;
      state.blendParameterX = "MoveX";
      state.blendParameterY = "MoveY";
      const float positions[4][2] = {{0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, -1.0f}, {-1.0f, 0.0f}};
      const char *clipNames[4] = {"north", "east", "south", "west"};
      for (int i = 0; i < 4; ++i)
      {
        AnimBlendEntry entry;
        entry.clip = clipNames[i];
        entry.thresholdX = positions[i][0];
        entry.thresholdY = positions[i][1];
        state.entries.push_back(entry);
      }

      AnimLayer layer;
      layer.states.push_back(state);
      layer.defaultState = 0;
      graph.layers.push_back(layer);
      return graph;
    }
  }

  TEST(AnimatorInstanceTest, StartsInTheDefaultStateAndTakesTheTransitionOnceItsConditionHolds)
  {
    const std::filesystem::path root = unique_test_directory("hades-animator-transition");
    ScopedDirectoryCleanup cleanup(root);
    std::filesystem::create_directories(root);
    ScopedClipRoot scopedRoot(root);

    AnimationClipCache &clips = AnimationClipCache::instance();
    std::string error;
    ASSERT_TRUE(clips.saveClip("idle", make_clip("idle", 1.0f), &error)) << error;
    ASSERT_TRUE(clips.saveClip("run", make_clip("run", 1.0f), &error)) << error;
    ASSERT_TRUE(clips.saveGraph("locomotion", make_locomotion_graph(), &error)) << error;

    const ModelAsset asset = make_model();
    AnimatorInstance instance;
    instance.set_graph_reference("locomotion");

    instance.update(0.016f, asset, clips);
    EXPECT_EQ(instance.layer_count(), 1U);
    EXPECT_EQ(instance.current_state(0), "Idle");
    EXPECT_EQ(instance.current_clip(0), "idle");
    EXPECT_FALSE(instance.is_transitioning(0));
    EXPECT_EQ(instance.palette().size(), asset.bones.size());

    // The guard is still shut, so the machine stays where it is.
    instance.set_float("Speed", 0.25f);
    instance.update(0.016f, asset, clips);
    EXPECT_EQ(instance.current_state(0), "Idle");

    // Once it opens the layer crossfades into the target state.
    instance.set_float("Speed", 1.0f);
    instance.update(0.016f, asset, clips);
    EXPECT_TRUE(instance.is_transitioning(0));
    EXPECT_EQ(instance.current_state(0), "Run");
    EXPECT_EQ(instance.current_clip(0), "run");

    // 0.32s later the 0.2s blend is long finished and the layer runs the
    // target state on its own.
    for (int frame = 0; frame < 20; ++frame)
    {
      instance.update(0.016f, asset, clips);
    }
    EXPECT_FALSE(instance.is_transitioning(0));
    EXPECT_EQ(instance.current_state(0), "Run");
  }

  TEST(AnimatorInstanceTest, ATriggerIsConsumedByExactlyOneTransition)
  {
    const std::filesystem::path root = unique_test_directory("hades-animator-trigger");
    ScopedDirectoryCleanup cleanup(root);
    std::filesystem::create_directories(root);
    ScopedClipRoot scopedRoot(root);

    AnimationClipCache &clips = AnimationClipCache::instance();
    std::string error;
    ASSERT_TRUE(clips.saveClip("idle", make_clip("idle", 1.0f), &error)) << error;
    ASSERT_TRUE(clips.saveClip("swing", make_clip("swing", 0.5f), &error)) << error;
    ASSERT_TRUE(clips.saveGraph("attack", make_attack_graph(), &error)) << error;

    const ModelAsset asset = make_model();
    AnimatorInstance instance;
    instance.set_graph_reference("attack");

    instance.update(0.016f, asset, clips);
    EXPECT_EQ(instance.current_state(0), "Idle");
    EXPECT_FALSE(instance.get_bool("Attack"));

    instance.set_trigger("Attack");
    EXPECT_TRUE(instance.get_bool("Attack"));

    instance.update(0.016f, asset, clips);
    EXPECT_EQ(instance.current_state(0), "Swing");
    // Taking the transition consumed the latch.
    EXPECT_FALSE(instance.get_bool("Attack"));

    // So coming back to Idle does not fire it a second time.
    ASSERT_TRUE(instance.goto_state("Idle", 0.0f));
    instance.update(0.016f, asset, clips);
    EXPECT_EQ(instance.current_state(0), "Idle");

    // A fresh latch fires again, once.
    instance.set_trigger("Attack");
    instance.update(0.016f, asset, clips);
    EXPECT_EQ(instance.current_state(0), "Swing");
    EXPECT_FALSE(instance.get_bool("Attack"));

    // reset_trigger clears a latch nothing has consumed yet.
    instance.set_trigger("Attack");
    instance.reset_trigger("Attack");
    EXPECT_FALSE(instance.get_bool("Attack"));

    EXPECT_FALSE(instance.goto_state("NoSuchState", 0.0f));
  }

  TEST(AnimatorInstanceTest, PlayClipOnAGraphlessInstancePlaysLoopsAndKeepsNormalizedTimeInRange)
  {
    const std::filesystem::path root = unique_test_directory("hades-animator-clip");
    ScopedDirectoryCleanup cleanup(root);
    std::filesystem::create_directories(root);
    ScopedClipRoot scopedRoot(root);

    AnimationClipCache &clips = AnimationClipCache::instance();
    std::string error;
    ASSERT_TRUE(clips.saveClip("walk", make_clip("walk", 1.0f), &error)) << error;

    const ModelAsset asset = make_model();
    AnimatorInstance instance;
    EXPECT_TRUE(instance.graph_reference().empty());

    instance.play_clip("walk", 0.0f, true);
    EXPECT_EQ(instance.current_clip(0), "walk");
    // Clip mode has no graph, so there is no state to report.
    EXPECT_EQ(instance.current_state(0), "");

    for (int frame = 0; frame < 8; ++frame)
    {
      instance.update(0.3f, asset, clips);
      EXPECT_GE(instance.normalized_time(0), 0.0f) << "frame " << frame;
      EXPECT_LE(instance.normalized_time(0), 1.0f) << "frame " << frame;
    }

    // 2.4s through a 1s looping clip is 0.4s into its third pass.
    EXPECT_NEAR(instance.time_seconds(0), 0.4f, 1e-3f);
    EXPECT_NEAR(instance.normalized_time(0), 0.4f, 1e-3f);

    // The pose is not merely sized, it actually moved, and the movement
    // reached the skinning palette. Checking only that the pose is non-empty
    // would pass for an animator that reported the right time while handing
    // the renderer the bind pose every frame — which is exactly what the
    // editor's preview must never do.
    const Skeleton skeleton = Skeleton::from_model(asset);
    const int joint = skeleton.find("Bone");
    ASSERT_GE(joint, 0);
    const std::size_t index = static_cast<std::size_t>(joint);

    const Pose &pose = instance.pose();
    ASSERT_EQ(pose.size(), skeleton.size());
    // 0.4 of a quarter turn about Z; the rest rotation is the identity.
    EXPECT_GT(std::fabs(pose.rotations[index].z), 0.1f) << "the clip never moved the joint";

    ASSERT_EQ(instance.palette().size(), asset.bones.size());
    EXPECT_GT(std::fabs(instance.palette()[0].m[0][1]), 0.1f)
        << "the posed rotation never reached the bone palette";

    // Seeking closes the event window and lands where it was asked to.
    instance.seek(0.75f, 0);
    EXPECT_NEAR(instance.time_seconds(0), 0.75f, kTolerance);
    instance.restart(0);
    EXPECT_NEAR(instance.time_seconds(0), 0.0f, kTolerance);
  }

  TEST(AnimatorInstanceTest, ANonLoopingClipFinishesAndHoldsItsLastFrame)
  {
    const std::filesystem::path root = unique_test_directory("hades-animator-oneshot");
    ScopedDirectoryCleanup cleanup(root);
    std::filesystem::create_directories(root);
    ScopedClipRoot scopedRoot(root);

    AnimationClipCache &clips = AnimationClipCache::instance();
    std::string error;
    ASSERT_TRUE(clips.saveClip("swing", make_clip("swing", 1.0f), &error)) << error;

    const ModelAsset asset = make_model();
    AnimatorInstance instance;
    instance.play_clip("swing", 0.0f, false);

    for (int frame = 0; frame < 10; ++frame)
    {
      instance.update(0.3f, asset, clips);
    }
    EXPECT_NEAR(instance.time_seconds(0), 1.0f, 1e-3f);
    EXPECT_NEAR(instance.normalized_time(0), 1.0f, 1e-3f);

    // Further updates hold the last frame instead of wrapping.
    instance.update(0.3f, asset, clips);
    EXPECT_NEAR(instance.time_seconds(0), 1.0f, 1e-3f);
    EXPECT_NEAR(instance.normalized_time(0), 1.0f, 1e-3f);
  }

  TEST(AnimatorInstanceTest, AnEventFiresOncePerPassOfTheClip)
  {
    const std::filesystem::path root = unique_test_directory("hades-animator-events");
    ScopedDirectoryCleanup cleanup(root);
    std::filesystem::create_directories(root);
    ScopedClipRoot scopedRoot(root);

    AnimationClipCache &clips = AnimationClipCache::instance();
    AnimationClipAsset clip = make_clip("walk", 1.0f);
    clip.events.push_back({0.5f, "footstep", "left", 1.0f});

    std::string error;
    ASSERT_TRUE(clips.saveClip("walk", clip, &error)) << error;

    const ModelAsset asset = make_model();
    AnimatorInstance instance;
    instance.play_clip("walk", 0.0f, true);

    const auto run_one_pass = [&]()
    {
      int fired = 0;
      // Four frames of 0.25s is exactly one pass of the 1s clip.
      for (int frame = 0; frame < 4; ++frame)
      {
        instance.update(0.25f, asset, clips);
        for (const AnimationEventFired &event : instance.drain_events())
        {
          if (event.name == "footstep")
          {
            ++fired;
            EXPECT_EQ(event.stringValue, "left");
            EXPECT_NEAR(event.floatValue, 1.0f, kTolerance);
            EXPECT_NEAR(event.time, 0.5f, kTolerance);
            EXPECT_EQ(event.clip, "walk");
          }
        }
      }
      return fired;
    };

    EXPECT_EQ(run_one_pass(), 1);
    // The next pass fires it again — once, not twice, and not never.
    EXPECT_EQ(run_one_pass(), 1);

    // A frame that advances nothing fires nothing. Park the play head just
    // short of the marker first: asserting this from t=0 would pass even for
    // an update() that did advance, because nothing sits near 0 to fire.
    instance.seek(0.49f, 0);
    instance.update(0.0f, asset, clips);
    EXPECT_NEAR(instance.time_seconds(0), 0.49f, kTolerance);
    EXPECT_TRUE(instance.pending_events().empty());

    // ...and the very next frame that does cross it fires it, so the empty
    // window above is the zero delta and not a swallowed event.
    instance.update(0.02f, asset, clips);
    ASSERT_EQ(instance.pending_events().size(), 1U);
    EXPECT_EQ(instance.pending_events()[0].name, "footstep");

    // event_fired answers the polling form, and answers it the same way for
    // every caller in the frame: a footstep that drives both a sound script
    // and a Blueprint node must not be swallowed by whichever polls first.
    EXPECT_TRUE(instance.event_fired("footstep"));
    EXPECT_TRUE(instance.event_fired("footstep"));
    EXPECT_FALSE(instance.event_fired("no-such-event"));

    // It is scoped to the frame: clearing the buffer (which AnimatorSystem
    // does once per frame) is what stops it reporting a stale firing.
    instance.clear_events();
    EXPECT_FALSE(instance.event_fired("footstep"));
  }

  TEST(AnimatorInstanceTest, StopHoldsThePoseAndResetForgetsPlaybackButKeepsTheGraph)
  {
    const std::filesystem::path root = unique_test_directory("hades-animator-stop");
    ScopedDirectoryCleanup cleanup(root);
    std::filesystem::create_directories(root);
    ScopedClipRoot scopedRoot(root);

    AnimationClipCache &clips = AnimationClipCache::instance();
    std::string error;
    ASSERT_TRUE(clips.saveClip("idle", make_clip("idle", 1.0f), &error)) << error;
    ASSERT_TRUE(clips.saveClip("run", make_clip("run", 1.0f), &error)) << error;
    ASSERT_TRUE(clips.saveGraph("locomotion", make_locomotion_graph(), &error)) << error;

    const ModelAsset asset = make_model();
    AnimatorInstance instance;
    instance.set_graph_reference("locomotion");
    instance.update(0.25f, asset, clips);
    const float held = instance.time_seconds(0);
    EXPECT_GT(held, 0.0f);

    instance.stop(-1);
    EXPECT_FALSE(instance.playing());
    instance.update(0.25f, asset, clips);
    // A stopped animator still poses itself, it just does not advance.
    EXPECT_NEAR(instance.time_seconds(0), held, kTolerance);
    EXPECT_FALSE(instance.pose().empty());

    instance.set_playing(true);
    instance.update(0.25f, asset, clips);
    EXPECT_GT(instance.time_seconds(0), held);

    instance.reset();
    EXPECT_EQ(instance.graph_reference(), "locomotion");
    instance.update(0.0f, asset, clips);
    EXPECT_NEAR(instance.time_seconds(0), 0.0f, kTolerance);
    EXPECT_EQ(instance.current_state(0), "Idle");
  }
  TEST(AnimatorInstanceTest, ANegativeBlendFallsBackToTheAuthoredDefaultBlend)
  {
    const std::filesystem::path root = unique_test_directory("hades-animator-default-blend");
    ScopedDirectoryCleanup cleanup(root);
    std::filesystem::create_directories(root);
    ScopedClipRoot scopedRoot(root);

    AnimationClipCache &clips = AnimationClipCache::instance();
    std::string error;
    ASSERT_TRUE(clips.saveClip("idle", make_clip("idle", 1.0f), &error)) << error;
    ASSERT_TRUE(clips.saveClip("run", make_clip("run", 1.0f), &error)) << error;

    const ModelAsset asset = make_model();

    // Length of the crossfade play_clip actually ran, in seconds.
    const auto measure = [&](float requested, float authoredDefault) {
      AnimatorInstance instance;
      instance.set_default_blend(authoredDefault);
      instance.play_clip("idle", 0.0f, true, 0);
      // One frame first: the initial ensure_layers() clears any pending
      // transition, so a crossfade started before it would not be observable.
      instance.update(0.005f, asset, clips);

      instance.play_clip("run", requested, true, 0);
      float elapsed = 0.0f;
      for (int frame = 0; frame < 500 && instance.is_transitioning(0); ++frame)
      {
        instance.update(0.005f, asset, clips);
        elapsed += 0.005f;
      }
      return elapsed;
    };

    AnimatorInstance fresh;
    // Sane before AnimatorSystem has pushed anything: a script's onStart runs
    // before the system has ever seen the entity.
    EXPECT_NEAR(fresh.default_blend(), 0.15f, kTolerance);

    // The whole point: no blend given means the authored value, not 0.15.
    EXPECT_NEAR(measure(-1.0f, 0.4f), 0.4f, 0.006f);
    // An explicit blend still wins, and 0 still snaps — which is why the
    // sentinel has to be negative rather than zero.
    EXPECT_NEAR(measure(0.15f, 0.4f), 0.15f, 0.006f);
    EXPECT_NEAR(measure(0.0f, 0.4f), 0.0f, kTolerance);

    // A negative default cannot itself become a sentinel.
    AnimatorInstance clamped;
    clamped.set_default_blend(-2.0f);
    EXPECT_NEAR(clamped.default_blend(), 0.0f, kTolerance);
  }

  TEST(AnimatorInstanceTest, TheScriptFacadeDefersToTheComponentsDefaultBlendWhenNoBlendIsGiven)
  {
    // Resolving the sentinel inside play_clip only matters if something ever
    // sends it. `Animation::play(entity, "clip")` — the call docs/animation.md
    // and every script sample use — is the caller the authored "Default Blend"
    // exists for, so its own default has to BE the sentinel. Leave it at a
    // literal 0.15 and the component value stays as dead as it was before.
    const std::filesystem::path root = unique_test_directory("hades-animator-script-blend");
    ScopedDirectoryCleanup cleanup(root);
    std::filesystem::create_directories(root);
    ScopedClipRoot scopedRoot(root);

    AnimationClipCache &clips = AnimationClipCache::instance();
    std::string error;
    ASSERT_TRUE(clips.saveClip("idle", make_clip("idle", 1.0f), &error)) << error;
    ASSERT_TRUE(clips.saveClip("run", make_clip("run", 1.0f), &error)) << error;

    EXPECT_LT(Animation::kComponentBlend, 0.0f) << "0 is a legal authored blend, so it cannot be the sentinel";

    const ModelAsset asset = make_model();
    constexpr Entity::EntityId entity = 4242;

    // Length of the crossfade a script call actually ran, in seconds.
    const auto measure = [&](bool passBlendExplicitly, float explicitBlend, float authoredDefault) {
      AnimationRuntime::instance().remove(entity);
      AnimatorInstance &instance = AnimationRuntime::instance().instanceFor(entity);
      // What AnimatorSystem::run pushes from AnimatorComponent every frame.
      instance.set_default_blend(authoredDefault);
      Animation::play(entity, "idle", 0.0f);
      instance.update(0.005f, asset, clips);

      if (passBlendExplicitly)
      {
        Animation::play(entity, "run", explicitBlend);
      }
      else
      {
        Animation::play(entity, "run");
      }

      float elapsed = 0.0f;
      for (int frame = 0; frame < 500 && instance.is_transitioning(0); ++frame)
      {
        instance.update(0.005f, asset, clips);
        elapsed += 0.005f;
      }
      return elapsed;
    };

    EXPECT_NEAR(measure(false, 0.0f, 0.4f), 0.4f, 0.006f);
    // Authoring a different value moves it, so this is not 0.4 by luck.
    EXPECT_NEAR(measure(false, 0.0f, 0.8f), 0.8f, 0.006f);
    // An explicit blend still wins outright, and 0 still snaps.
    EXPECT_NEAR(measure(true, 0.15f, 0.8f), 0.15f, 0.006f);
    EXPECT_NEAR(measure(true, 0.0f, 0.8f), 0.0f, kTolerance);

    AnimationRuntime::instance().remove(entity);
  }

  TEST(AnimatorInstanceTest, ASheardNodeStillPosesAtTheModelsBindPose)
  {
    const std::filesystem::path root = unique_test_directory("hades-animator-shear");
    ScopedDirectoryCleanup cleanup(root);
    std::filesystem::create_directories(root);
    ScopedClipRoot scopedRoot(root);

    const ModelAsset asset = make_sheared_model();
    const Skeleton skeleton = Skeleton::from_model(asset);
    ASSERT_EQ(skeleton.size(), asset.nodes.size());
    EXPECT_FALSE(skeleton.joint(0).hasCorrection);
    // Only the node TRS could not express carries a residual.
    EXPECT_TRUE(skeleton.joint(1).hasCorrection);
    EXPECT_FALSE(skeleton.joint(2).hasCorrection);

    // The rest pose has to reproduce the node hierarchy the offset matrices
    // were derived against, or the mesh deforms the moment the animator
    // takes over from the model's own bind pose.
    std::vector<math::Mat4> bindGlobals;
    asset.bindPoseNodeGlobals(bindGlobals);
    std::vector<math::Mat4> restGlobals;
    skeleton.local_to_global(skeleton.rest_pose(), restGlobals);
    ASSERT_EQ(restGlobals.size(), bindGlobals.size());
    for (std::size_t joint = 0; joint < restGlobals.size(); ++joint)
    {
      for (int column = 0; column < 4; ++column)
      {
        for (int row = 0; row < 4; ++row)
        {
          EXPECT_NEAR(restGlobals[joint].m[column][row], bindGlobals[joint].m[column][row], kTolerance)
              << "joint " << joint << " (" << asset.nodes[joint].name << ") column " << column << " row " << row;
        }
      }
    }

    // And the same through a real playing instance: a clip that keys every
    // joint at its own rest transform must leave the palette at the bind pose.
    AnimationClipCache &clips = AnimationClipCache::instance();
    std::string error;
    ASSERT_TRUE(clips.saveClip("rest", AnimationClipAsset::from_rest_pose(skeleton, "rest"), &error)) << error;

    AnimatorInstance instance;
    instance.play_clip("rest", 0.0f, true, 0);
    instance.update(0.016f, asset, clips);
    ASSERT_EQ(instance.palette().size(), asset.bindPose().size());
    for (std::size_t bone = 0; bone < instance.palette().size(); ++bone)
    {
      for (int column = 0; column < 4; ++column)
      {
        for (int row = 0; row < 4; ++row)
        {
          EXPECT_NEAR(instance.palette()[bone].m[column][row], asset.bindPose()[bone].m[column][row], kTolerance)
              << "bone " << bone << " column " << column << " row " << row;
        }
      }
    }
  }

  TEST(AnimatorInstanceTest, ATinyScaleNodeIsNotMistakenForANonTrsOne)
  {
    // The rest correction is the residual of inverting the rebuilt TRS, and a
    // cofactor inverse gives up and returns IDENTITY once |det| drops under
    // 1e-12. det is sx*sy*sz, so a node carrying its unit conversion as a
    // uniform 1e-4 (or smaller) scale would hand back its OWN local transform
    // as the "residual" and have it applied a second time on top of every
    // pose -- a node that decomposes perfectly, wrecked by the repair meant
    // for nodes that do not.
    const float scales[] = {1e-3f, 1e-4f, 1e-5f, 1e-6f};
    for (const float scale : scales)
    {
      ModelAsset asset;
      asset.nodes.push_back({"Root", -1, math::Mat4::identity()});
      asset.nodes.push_back({"Hips", 0,
                             math::Mat4::translate({0.0f, 1.0f, 0.0f}) *
                                 math::Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, 0.6f).toMat4() *
                                 math::Mat4::scaleMatrix({scale, scale, scale})});
      asset.nodes.push_back({"Spine", 1, math::Mat4::translate({0.0f, 2.0f, 0.0f})});
      std::vector<math::Mat4> bindGlobals;
      asset.bindPoseNodeGlobals(bindGlobals);
      asset.bones.push_back({1, bindGlobals[1].inverse()});
      asset.bones.push_back({2, bindGlobals[2].inverse()});
      asset.finalize();

      const Skeleton skeleton = Skeleton::from_model(asset);
      // Nothing here is sheared: every node is exactly T * R * S.
      EXPECT_FALSE(skeleton.joint(1).hasCorrection) << "uniform scale " << scale;

      std::vector<math::Mat4> restGlobals;
      skeleton.local_to_global(skeleton.rest_pose(), restGlobals);
      ASSERT_EQ(restGlobals.size(), bindGlobals.size());
      for (std::size_t joint = 0; joint < restGlobals.size(); ++joint)
      {
        for (int column = 0; column < 4; ++column)
        {
          for (int row = 0; row < 4; ++row)
          {
            EXPECT_NEAR(restGlobals[joint].m[column][row], bindGlobals[joint].m[column][row], kTolerance)
                << "uniform scale " << scale << " joint " << joint << " column " << column << " row " << row;
          }
        }
      }
    }
  }

  TEST(AnimatorInstanceTest, AnAdditiveLayerCrossfadeMeasuresEachHalfAgainstItsOwnReference)
  {
    const std::filesystem::path root = unique_test_directory("hades-animator-additive");
    ScopedDirectoryCleanup cleanup(root);
    std::filesystem::create_directories(root);
    ScopedClipRoot scopedRoot(root);

    AnimationClipCache &clips = AnimationClipCache::instance();
    std::string error;
    // The base layer holds "Bone" exactly where the model does.
    ASSERT_TRUE(clips.saveClip("idle", make_constant_clip("idle", 1.0f), &error)) << error;
    // Two additive clips, each equal to its own reference frame and wildly
    // different from the other's, so both deltas are zero and the layer must
    // contribute nothing at any point of the crossfade.
    ASSERT_TRUE(clips.saveClip("aimA", make_self_referencing_additive_clip("aimA", 1.0f, 0.0f), &error)) << error;
    ASSERT_TRUE(clips.saveClip("aimB", make_self_referencing_additive_clip("aimB", 9.0f, 1.2f), &error)) << error;
    ASSERT_TRUE(clips.saveGraph("aim", make_aim_graph(), &error)) << error;

    const ModelAsset asset = make_model();
    AnimatorInstance instance;
    instance.set_graph_reference("aim");

    for (int frame = 0; frame < 4; ++frame)
    {
      instance.update(0.05f, asset, clips);
    }
    ASSERT_EQ(instance.layer_count(), 2U);
    EXPECT_NEAR(instance.pose().translations[1].y, 1.0f, kTolerance);

    instance.set_bool("Fire", true);
    for (int frame = 0; frame < 13; ++frame)
    {
      instance.update(0.05f, asset, clips);
      // Measuring the blended pose against only the incoming half's reference
      // pops to the full difference between the two references — here 8 units
      // and 1.2 rad — and eases out of it over the whole blend.
      EXPECT_NEAR(instance.pose().translations[1].y, 1.0f, kTolerance) << "frame " << frame;
      EXPECT_NEAR(instance.pose().rotations[1].z, 0.0f, kTolerance) << "frame " << frame;
    }
  }

  TEST(AnimatorInstanceTest, A2dBlendTreeIsContinuousAndClampsOutsideTheAuthoredSet)
  {
    const std::filesystem::path root = unique_test_directory("hades-animator-blend2d");
    ScopedDirectoryCleanup cleanup(root);
    std::filesystem::create_directories(root);
    ScopedClipRoot scopedRoot(root);

    AnimationClipCache &clips = AnimationClipCache::instance();
    std::string error;
    // Distinct, constant heights so the posed translation reads out the blend
    // weights directly: a blend is linear in them, so a step here is a step
    // in the pose.
    ASSERT_TRUE(clips.saveClip("north", make_constant_clip("north", 1.0f), &error)) << error;
    ASSERT_TRUE(clips.saveClip("east", make_constant_clip("east", 2.0f), &error)) << error;
    ASSERT_TRUE(clips.saveClip("south", make_constant_clip("south", 4.0f), &error)) << error;
    ASSERT_TRUE(clips.saveClip("west", make_constant_clip("west", 8.0f), &error)) << error;
    ASSERT_TRUE(clips.saveGraph("strafe", make_strafe_graph(), &error)) << error;

    const ModelAsset asset = make_model();
    AnimatorInstance instance;
    instance.set_graph_reference("strafe");

    const auto pose_at = [&](float x, float y) {
      instance.set_float("MoveX", x);
      instance.set_float("MoveY", y);
      instance.update(0.0f, asset, clips);
      return instance.pose().translations[1].y;
    };

    // Walking the parameter around the circle must not step: ranking the
    // nearest few entries swaps them on every bisector where two are
    // equidistant, and the entry that drops out is still carrying weight.
    const int steps = 720;
    float worst = 0.0f;
    float previous = pose_at(0.0f, 1.0f);
    for (int step = 1; step <= steps; ++step)
    {
      const float angle = 6.2831853f * static_cast<float>(step) / static_cast<float>(steps);
      const float value = pose_at(std::sin(angle), std::cos(angle));
      worst = std::fmax(worst, std::fabs(value - previous));
      previous = value;
    }
    EXPECT_LT(worst, 0.15f) << "largest single-step change around the circle";

    // Exactly on an entry, that clip plays alone.
    EXPECT_NEAR(pose_at(0.0f, 1.0f), 1.0f, kTolerance);
    // On the bisector, exactly the two entries either side of it — a third
    // clip must not leak in.
    EXPECT_NEAR(pose_at(0.70711f, 0.70711f), 1.5f, kTolerance);
    // Outside the authored set the boundary clip holds, the way a 1D tree
    // clamps to its end entry, rather than drifting toward the mean.
    EXPECT_NEAR(pose_at(0.0f, 1.2f), 1.0f, kTolerance);
    EXPECT_NEAR(pose_at(0.0f, 5.0f), 1.0f, kTolerance);
    EXPECT_NEAR(pose_at(0.0f, 1000.0f), 1.0f, kTolerance);
  }

  TEST(AnimatorInstanceTest, A2dBlendTreeWithMoreEntriesThanTheContributionCapIsStillContinuous)
  {
    // Weighting every entry removes the ranking that made the old scheme pop —
    // except at one place: the fixed-size contribution set. Near the middle of
    // a large ring EVERY entry has a non-zero band, so which ones fit is
    // decided by rank again, and an entry dropped while still carrying weight
    // is exactly the original defect. Swept where that bites hardest.
    const std::filesystem::path root = unique_test_directory("hades-animator-blend2d-big");
    ScopedDirectoryCleanup cleanup(root);
    std::filesystem::create_directories(root);
    ScopedClipRoot scopedRoot(root);

    AnimationClipCache &clips = AnimationClipCache::instance();
    std::string error;
    constexpr int kEntries = 24;
    for (int i = 0; i < kEntries; ++i)
    {
      // Only one entry carries a value: a symmetric assignment cancels the
      // off-axis contributions and would hide the very error being measured.
      const std::string name = "ring" + std::to_string(i);
      ASSERT_TRUE(clips.saveClip(name, make_constant_clip(name, i == 0 ? 10.0f : 0.0f), &error)) << error;
    }
    ASSERT_TRUE(clips.saveGraph("ring", make_ring_graph(kEntries), &error)) << error;

    const ModelAsset asset = make_model();
    AnimatorInstance instance;
    instance.set_graph_reference("ring");

    const auto pose_at = [&](float x, float y) {
      instance.set_float("MoveX", x);
      instance.set_float("MoveY", y);
      instance.update(0.0f, asset, clips);
      return instance.pose().translations[1].y;
    };

    const int steps = 2000;
    for (const float radius : {0.02f, 0.5f, 1.0f})
    {
      float worst = 0.0f;
      float previous = pose_at(0.0f, radius);
      for (int step = 1; step <= steps; ++step)
      {
        const float angle = 6.2831853f * static_cast<float>(step) / static_cast<float>(steps);
        const float value = pose_at(radius * std::sin(angle), radius * std::cos(angle));
        worst = std::fmax(worst, std::fabs(value - previous));
        previous = value;
      }
      // The honest bound is "sampling step times local slope". A dropped
      // contributor shows up an order of magnitude above that.
      EXPECT_LT(worst, 0.35f) << "largest single-step change at radius " << radius;
    }

    // The properties that hold for a small tree still hold for a big one.
    EXPECT_NEAR(pose_at(0.0f, 1.0f), 10.0f, kTolerance);
    EXPECT_NEAR(pose_at(0.0f, 40.0f), 10.0f, kTolerance);
  }
}
