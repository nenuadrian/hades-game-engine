#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../engine/animation/animator_graph.hpp"

namespace hades
{
  namespace
  {
    constexpr float kTolerance = 1e-4f;

    bool has_problem(const std::vector<std::string> &problems, const std::string &needle)
    {
      for (const std::string &problem : problems)
      {
        if (problem.find(needle) != std::string::npos)
        {
          return true;
        }
      }
      return false;
    }

    AnimParameter make_parameter(const std::string &name, AnimParamType type)
    {
      AnimParameter parameter;
      parameter.name = name;
      parameter.type = type;
      return parameter;
    }

    AnimState make_clip_state(const std::string &name, const std::string &clip)
    {
      AnimState state;
      state.name = name;
      state.kind = AnimStateKind::Clip;
      state.clip = clip;
      return state;
    }

    /// A graph shaped like the one the editor writes: one float parameter, a
    /// clip state, a 1D blend tree and a guarded transition between them.
    AnimatorGraph make_locomotion_graph()
    {
      AnimatorGraph graph;
      graph.name = "locomotion";
      graph.description = "idle into a walk/run tree";
      graph.sourceModel = "characters/hero.fbx";

      AnimParameter speed = make_parameter("Speed", AnimParamType::Float);
      speed.floatValue = 0.25f;
      graph.parameters.push_back(speed);

      AnimParameter grounded = make_parameter("Grounded", AnimParamType::Bool);
      grounded.boolValue = true;
      graph.parameters.push_back(grounded);

      AnimState idle = make_clip_state("Idle", "idle");
      idle.speed = 0.5f;
      idle.looping = true;
      idle.x = 120.0f;
      idle.y = -40.0f;

      AnimState move;
      move.name = "Move";
      move.kind = AnimStateKind::BlendTree1D;
      move.blendParameterX = "Speed";
      move.speed = 1.5f;
      move.looping = false;
      move.entries.push_back({"walk", 0.0f, 0.0f, 1.0f});
      move.entries.push_back({"run", 1.0f, 0.0f, 1.25f});

      AnimLayer layer;
      layer.name = "Base";
      layer.weight = 0.75f;
      layer.additive = true;
      layer.maskBones = {"Spine", "Head"};
      layer.maskIncludesDescendants = false;
      layer.states = {idle, move};
      layer.defaultState = 1;

      AnimTransition toMove;
      toMove.fromState = 0;
      toMove.toState = 1;
      toMove.duration = 0.35f;
      toMove.hasExitTime = true;
      toMove.exitTime = 0.8f;
      toMove.canInterrupt = true;
      toMove.priority = 3;
      toMove.conditions.push_back({"Speed", AnimConditionOp::GreaterOrEqual, 0.1f});
      toMove.conditions.push_back({"Grounded", AnimConditionOp::IsTrue, 0.0f});
      layer.transitions.push_back(toMove);

      graph.layers.push_back(layer);
      return graph;
    }
  }

  TEST(AnimatorGraphTest, EnumNamesRoundTripThroughTheirPersistedSpellings)
  {
    for (int i = 0; i < 4; ++i)
    {
      const AnimParamType value = static_cast<AnimParamType>(i);
      AnimParamType parsed = AnimParamType::Trigger;
      ASSERT_TRUE(anim_param_type_from_name(anim_param_type_name(value), parsed));
      EXPECT_EQ(parsed, value);
    }

    for (int i = 0; i < 8; ++i)
    {
      const AnimConditionOp value = static_cast<AnimConditionOp>(i);
      AnimConditionOp parsed = AnimConditionOp::IsFalse;
      ASSERT_TRUE(anim_condition_op_from_name(anim_condition_op_name(value), parsed));
      EXPECT_EQ(parsed, value);
    }

    for (int i = 0; i < 3; ++i)
    {
      const AnimStateKind value = static_cast<AnimStateKind>(i);
      AnimStateKind parsed = AnimStateKind::BlendTree2D;
      ASSERT_TRUE(anim_state_kind_from_name(anim_state_kind_name(value), parsed));
      EXPECT_EQ(parsed, value);
    }

    // An unknown spelling fails and leaves the output alone, so a stale file
    // cannot silently reinterpret a state as another kind.
    AnimStateKind untouched = AnimStateKind::BlendTree1D;
    EXPECT_FALSE(anim_state_kind_from_name("nonsense", untouched));
    EXPECT_EQ(untouched, AnimStateKind::BlendTree1D);
  }

  TEST(AnimatorGraphTest, JsonRoundTripPreservesParametersLayersStatesAndTransitions)
  {
    const AnimatorGraph graph = make_locomotion_graph();

    AnimatorGraph restored;
    std::string error;
    ASSERT_TRUE(AnimatorGraph::from_json(graph.to_json(), restored, &error)) << error;

    EXPECT_EQ(restored.name, "locomotion");
    EXPECT_EQ(restored.description, "idle into a walk/run tree");
    EXPECT_EQ(restored.sourceModel, "characters/hero.fbx");

    ASSERT_EQ(restored.parameters.size(), 2U);
    EXPECT_EQ(restored.parameters[0].name, "Speed");
    EXPECT_EQ(restored.parameters[0].type, AnimParamType::Float);
    EXPECT_NEAR(restored.parameters[0].floatValue, 0.25f, kTolerance);
    EXPECT_EQ(restored.parameters[1].type, AnimParamType::Bool);
    EXPECT_TRUE(restored.parameters[1].boolValue);
    ASSERT_NE(restored.find_parameter("Grounded"), nullptr);
    EXPECT_EQ(restored.parameter_index("Grounded"), 1);
    EXPECT_EQ(restored.parameter_index("Missing"), -1);

    ASSERT_EQ(restored.layers.size(), 1U);
    const AnimLayer &layer = restored.layers[0];
    EXPECT_EQ(layer.name, "Base");
    EXPECT_NEAR(layer.weight, 0.75f, kTolerance);
    EXPECT_TRUE(layer.additive);
    EXPECT_FALSE(layer.maskIncludesDescendants);
    ASSERT_EQ(layer.maskBones.size(), 2U);
    EXPECT_EQ(layer.maskBones[0], "Spine");
    EXPECT_EQ(layer.maskBones[1], "Head");
    EXPECT_EQ(layer.defaultState, 1);

    ASSERT_EQ(layer.states.size(), 2U);
    EXPECT_EQ(layer.states[0].name, "Idle");
    EXPECT_EQ(layer.states[0].kind, AnimStateKind::Clip);
    EXPECT_EQ(layer.states[0].clip, "idle");
    EXPECT_NEAR(layer.states[0].speed, 0.5f, kTolerance);
    EXPECT_TRUE(layer.states[0].looping);
    EXPECT_NEAR(layer.states[0].x, 120.0f, kTolerance);
    EXPECT_NEAR(layer.states[0].y, -40.0f, kTolerance);

    EXPECT_EQ(layer.states[1].kind, AnimStateKind::BlendTree1D);
    EXPECT_EQ(layer.states[1].blendParameterX, "Speed");
    EXPECT_FALSE(layer.states[1].looping);
    ASSERT_EQ(layer.states[1].entries.size(), 2U);
    EXPECT_EQ(layer.states[1].entries[1].clip, "run");
    EXPECT_NEAR(layer.states[1].entries[1].thresholdX, 1.0f, kTolerance);
    EXPECT_NEAR(layer.states[1].entries[1].speed, 1.25f, kTolerance);
    EXPECT_EQ(layer.find_state("Move"), 1);
    EXPECT_EQ(layer.find_state("Nowhere"), -1);

    ASSERT_EQ(layer.transitions.size(), 1U);
    const AnimTransition &transition = layer.transitions[0];
    EXPECT_EQ(transition.fromState, 0);
    EXPECT_EQ(transition.toState, 1);
    EXPECT_NEAR(transition.duration, 0.35f, kTolerance);
    EXPECT_TRUE(transition.hasExitTime);
    EXPECT_NEAR(transition.exitTime, 0.8f, kTolerance);
    EXPECT_TRUE(transition.canInterrupt);
    EXPECT_EQ(transition.priority, 3);
    ASSERT_EQ(transition.conditions.size(), 2U);
    EXPECT_EQ(transition.conditions[0].parameter, "Speed");
    EXPECT_EQ(transition.conditions[0].op, AnimConditionOp::GreaterOrEqual);
    EXPECT_NEAR(transition.conditions[0].threshold, 0.1f, kTolerance);
    EXPECT_EQ(transition.conditions[1].op, AnimConditionOp::IsTrue);

    AnimatorGraph rejected;
    std::string rejectError;
    EXPECT_FALSE(AnimatorGraph::from_json(nlohmann::json::array(), rejected, &rejectError));
    EXPECT_FALSE(rejectError.empty());
  }

  TEST(AnimatorGraphTest, LoadingDropsEdgesThatPointNowhereAndClampsTheDefaultState)
  {
    nlohmann::json document = make_locomotion_graph().to_json();
    document["layers"][0]["transitions"][0]["toState"] = 9;
    document["layers"][0]["defaultState"] = 12;

    AnimatorGraph restored;
    std::string error;
    // Loading is tolerant on purpose: a stale file still opens, minus the
    // edges that point nowhere. Reporting the rest is validate()'s job.
    ASSERT_TRUE(AnimatorGraph::from_json(document, restored, &error)) << error;
    ASSERT_EQ(restored.layers.size(), 1U);
    EXPECT_TRUE(restored.layers[0].transitions.empty());
    EXPECT_EQ(restored.layers[0].defaultState, 1);
  }

  TEST(AnimatorGraphTest, ValidateCatchesDanglingTransitionsUnknownParametersAndEmptyBlendTrees)
  {
    std::vector<std::string> problems;

    // A clean graph reports nothing at all.
    EXPECT_TRUE(make_locomotion_graph().validate(problems));
    EXPECT_TRUE(problems.empty());

    {
      AnimatorGraph graph;
      AnimLayer layer;
      layer.states.push_back(make_clip_state("Idle", "idle"));
      AnimTransition dangling;
      dangling.fromState = 0;
      dangling.toState = 4;
      layer.transitions.push_back(dangling);
      graph.layers.push_back(layer);

      EXPECT_FALSE(graph.validate(problems));
      EXPECT_TRUE(has_problem(problems, "targets state 4"));
      EXPECT_TRUE(has_problem(problems, "does not exist"));
    }

    {
      AnimatorGraph graph;
      AnimLayer layer;
      layer.states.push_back(make_clip_state("Idle", "idle"));
      AnimTransition guarded;
      guarded.fromState = 0;
      guarded.toState = 0;
      guarded.conditions.push_back({"NeverDeclared", AnimConditionOp::Greater, 1.0f});
      layer.transitions.push_back(guarded);
      graph.layers.push_back(layer);

      EXPECT_FALSE(graph.validate(problems));
      EXPECT_TRUE(has_problem(problems, "unknown parameter 'NeverDeclared'"));
    }

    {
      AnimatorGraph graph;
      graph.parameters.push_back(make_parameter("Speed", AnimParamType::Float));
      AnimLayer layer;
      AnimState empty;
      empty.name = "Move";
      empty.kind = AnimStateKind::BlendTree1D;
      empty.blendParameterX = "Speed";
      layer.states.push_back(empty);
      graph.layers.push_back(layer);

      EXPECT_FALSE(graph.validate(problems));
      EXPECT_TRUE(has_problem(problems, "blend tree with no entries"));
    }

    {
      // A layer with no states at all cannot honour its default state.
      AnimatorGraph graph;
      graph.layers.push_back(AnimLayer{});
      EXPECT_FALSE(graph.validate(problems));
      EXPECT_TRUE(has_problem(problems, "defaultState"));
    }
  }

  TEST(AnimatorGraphTest, EnsureDefaultLayerMakesAnEmptyGraphPlayable)
  {
    AnimatorGraph graph;
    ASSERT_TRUE(graph.layers.empty());

    graph.ensure_default_layer();

    ASSERT_EQ(graph.layers.size(), 1U);
    ASSERT_EQ(graph.layers[0].states.size(), 1U);
    EXPECT_EQ(graph.layers[0].defaultState, 0);
    EXPECT_EQ(graph.layers[0].states[0].kind, AnimStateKind::Clip);

    std::vector<std::string> problems;
    EXPECT_TRUE(graph.validate(problems)) << (problems.empty() ? std::string() : problems[0]);

    // Idempotent: a second call must not append another state.
    graph.ensure_default_layer();
    EXPECT_EQ(graph.layers.size(), 1U);
    EXPECT_EQ(graph.layers[0].states.size(), 1U);

    // A layer that exists but is empty is filled in too.
    graph.layers.push_back(AnimLayer{});
    graph.ensure_default_layer();
    ASSERT_EQ(graph.layers.size(), 2U);
    EXPECT_EQ(graph.layers[1].states.size(), 1U);
    EXPECT_EQ(graph.layers[1].defaultState, 0);
  }

  TEST(AnimatorGraphTest, ReferencedClipsDeduplicatesAcrossStatesAndBlendTreeEntries)
  {
    AnimatorGraph graph;

    AnimLayer base;
    base.states.push_back(make_clip_state("Idle", "idle"));
    // The same clip under another state name must not be listed twice.
    base.states.push_back(make_clip_state("Breathe", "idle"));

    AnimState move;
    move.name = "Move";
    move.kind = AnimStateKind::BlendTree1D;
    move.blendParameterX = "Speed";
    move.entries.push_back({"walk", 0.0f, 0.0f, 1.0f});
    move.entries.push_back({"run", 1.0f, 0.0f, 1.0f});
    move.entries.push_back({"walk", 2.0f, 0.0f, 1.0f});
    base.states.push_back(move);

    // A state with no clip contributes nothing at all.
    base.states.push_back(make_clip_state("Unset", ""));
    graph.layers.push_back(base);

    AnimLayer upper;
    upper.name = "Upper";
    upper.states.push_back(make_clip_state("Aim", "run"));
    upper.states.push_back(make_clip_state("Wave", "wave"));
    graph.layers.push_back(upper);

    const std::vector<std::string> clips = graph.referenced_clips();
    ASSERT_EQ(clips.size(), 4U);
    // Authoring order is kept so a failure to warm one of them is readable.
    EXPECT_EQ(clips[0], "idle");
    EXPECT_EQ(clips[1], "walk");
    EXPECT_EQ(clips[2], "run");
    EXPECT_EQ(clips[3], "wave");
  }
}
