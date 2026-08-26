#include <gtest/gtest.h>

#include <string>

#include "blueprint_test_support.hpp"

#include "../engine/blueprint/blueprint_compiler.hpp"
#include "../engine/blueprint/blueprint_vm.hpp"
#include "../engine/components/name_component.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"

namespace hades
{
  using blueprint_test_support::GraphBuilder;
  using blueprint_test_support::RecordingHost;

  namespace
  {
    /// Everything a VM test needs: an ECS pair, a recording host, and a VM.
    struct VmFixture
    {
      EntityManager entityManager;
      ComponentManager componentManager{&entityManager};
      RecordingHost host;
      BlueprintVM vm{componentManager, entityManager, host};

      Entity::EntityId create_entity(const char *name = "Subject")
      {
        const Entity::EntityId entity = entityManager.createEntity();
        componentManager.addComponent(entity, NameComponent{name});
        componentManager.addComponent(entity, PositionComponent3D(0.0f, 0.0f, 0.0f));
        return entity;
      }
    };

    /// Compile and assert success in one step so tests fail with the compiler's
    /// own message rather than a null dereference.
    CompiledBlueprint compile_ok(const Blueprint &blueprint)
    {
      CompiledBlueprint compiled = compile_blueprint(blueprint);
      EXPECT_TRUE(compiled.succeeded) << compiled.error_summary();
      return compiled;
    }

    BlueprintNodeId add_print(GraphBuilder &builder, const char *text)
    {
      const BlueprintNodeId node = builder.add("debug.print");
      builder.literal(node, "text", BlueprintValue::from_string(text));
      return node;
    }
  }

  TEST(BlueprintVmTest, BeginPlayRunsTheChainOnce)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);
    const auto begin = builder.add("event.begin_play");
    const auto print = add_print(builder, "hello");
    builder.exec(begin, "exec", print);

    const CompiledBlueprint compiled = compile_ok(blueprint);

    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());

    EXPECT_TRUE(fixture.vm.dispatch(instance, "begin_play"));
    EXPECT_EQ(fixture.host.joined(), "hello");

    // No Tick handler: dispatch reports that nothing ran.
    EXPECT_FALSE(fixture.vm.dispatch(instance, "tick", {BlueprintValue::from_float(0.016f)}));
  }

  TEST(BlueprintVmTest, BranchTakesTheSelectedPin)
  {
    const auto run = [](bool condition)
    {
      Blueprint blueprint;
      GraphBuilder builder(blueprint, blueprint.eventGraph);
      const auto begin = builder.add("event.begin_play");
      const auto branch = builder.add("flow.branch");
      const auto yes = add_print(builder, "yes");
      const auto no = add_print(builder, "no");

      builder.exec(begin, "exec", branch);
      builder.exec(branch, "true", yes);
      builder.exec(branch, "false", no);
      builder.literal(branch, "condition", BlueprintValue::from_bool(condition));

      const CompiledBlueprint compiled = compile_ok(blueprint);
      VmFixture fixture;
      auto instance = make_blueprint_instance(compiled, fixture.create_entity());
      fixture.vm.dispatch(instance, "begin_play");
      return fixture.host.joined();
    };

    EXPECT_EQ(run(true), "yes");
    EXPECT_EQ(run(false), "no");
  }

  TEST(BlueprintVmTest, SequenceRunsEveryPinInOrder)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto sequence = builder.add("flow.sequence", "outputs", 3);
    builder.exec(begin, "exec", sequence);

    // Each branch is two nodes deep so an out-of-order continuation would show.
    for (int i = 0; i < 3; ++i)
    {
      const std::string first = "a" + std::to_string(i);
      const std::string second = "b" + std::to_string(i);
      const auto firstNode = add_print(builder, first.c_str());
      const auto secondNode = add_print(builder, second.c_str());
      builder.exec(sequence, "then" + std::to_string(i), firstNode);
      builder.exec(firstNode, "then", secondNode);
    }

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());
    fixture.vm.dispatch(instance, "begin_play");

    EXPECT_EQ(fixture.host.joined(), "a0|b0|a1|b1|a2|b2");
  }

  TEST(BlueprintVmTest, ForLoopIteratesInclusivelyThenTakesCompleted)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto loop = builder.add("flow.for_loop");
    const auto body = builder.add("debug.print");
    const auto done = add_print(builder, "done");

    builder.exec(begin, "exec", loop);
    builder.exec(loop, "loopBody", body);
    builder.exec(loop, "completed", done);
    builder.data(loop, "index", body, "text");
    builder.literal(loop, "first", BlueprintValue::from_int(1));
    builder.literal(loop, "last", BlueprintValue::from_int(3));

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());
    fixture.vm.dispatch(instance, "begin_play");

    EXPECT_EQ(fixture.host.joined(), "1|2|3|done");
  }

  TEST(BlueprintVmTest, NestedForLoopsKeepSeparateCounters)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto outer = builder.add("flow.for_loop");
    const auto inner = builder.add("flow.for_loop");
    const auto body = builder.add("debug.print");
    const auto append = builder.add("string.concat");

    builder.exec(begin, "exec", outer);
    builder.exec(outer, "loopBody", inner);
    builder.exec(inner, "loopBody", body);
    builder.literal(outer, "first", BlueprintValue::from_int(0));
    builder.literal(outer, "last", BlueprintValue::from_int(1));
    builder.literal(inner, "first", BlueprintValue::from_int(0));
    builder.literal(inner, "last", BlueprintValue::from_int(1));

    builder.data(outer, "index", append, "a");
    builder.data(inner, "index", append, "b");
    builder.data(append, "result", body, "text");

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());
    fixture.vm.dispatch(instance, "begin_play");

    EXPECT_EQ(fixture.host.joined(), "00|01|10|11");
  }

  TEST(BlueprintVmTest, WhileLoopReEvaluatesItsConditionEveryIteration)
  {
    Blueprint blueprint;

    BlueprintVariable counter;
    counter.name = "Counter";
    counter.type = ValueType::Int;
    counter.defaultValue = BlueprintValue::from_int(0);
    blueprint.variables.push_back(counter);

    GraphBuilder builder(blueprint, blueprint.eventGraph);
    const auto begin = builder.add("event.begin_play");
    const auto loop = builder.add("flow.while_loop");
    const auto get = builder.add("variable.get", "variable", "Counter");
    const auto less = builder.add("logic.less");
    const auto add = builder.add("math.add_int");
    const auto set = builder.add("variable.set", "variable", "Counter");
    const auto print = builder.add("debug.print");

    builder.exec(begin, "exec", loop);
    builder.exec(loop, "loopBody", set);
    builder.exec(set, "then", print);

    builder.data(get, "value", less, "a");
    builder.literal(less, "b", BlueprintValue::from_float(3.0f));
    builder.data(less, "result", loop, "condition");

    builder.data(get, "value", add, "a");
    builder.literal(add, "b", BlueprintValue::from_int(1));
    builder.data(add, "result", set, "value");
    builder.data(set, "value", print, "text");

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());
    fixture.vm.dispatch(instance, "begin_play");

    EXPECT_EQ(fixture.host.joined(), "1|2|3");
    EXPECT_EQ(instance.variables[0].as_int(), 3);
  }

  TEST(BlueprintVmTest, UnboundedLoopsAbortInsteadOfHanging)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto loop = builder.add("flow.while_loop");
    const auto body = builder.add("flow.stop");

    builder.exec(begin, "exec", loop);
    builder.exec(loop, "loopBody", body);
    builder.literal(loop, "condition", BlueprintValue::from_bool(true));

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());

    EXPECT_FALSE(fixture.vm.dispatch(instance, "begin_play"));
    EXPECT_TRUE(instance.faulted);
    EXPECT_NE(instance.error.find("budget exhausted"), std::string::npos);
    ASSERT_FALSE(fixture.host.errors.empty());
  }

  TEST(BlueprintVmTest, DoOnceGateAndFlipFlopKeepStateBetweenDispatches)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto tick = builder.add("event.tick");
    const auto doOnce = builder.add("flow.do_once");
    const auto flipFlop = builder.add("flow.flip_flop");
    const auto once = add_print(builder, "once");
    const auto a = add_print(builder, "A");
    const auto b = add_print(builder, "B");

    builder.exec(tick, "exec", doOnce);
    builder.exec(doOnce, "completed", once);
    builder.exec(once, "then", flipFlop);
    builder.exec(flipFlop, "a", a);
    builder.exec(flipFlop, "b", b);

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());

    for (int i = 0; i < 3; ++i)
    {
      fixture.vm.dispatch(instance, "tick", {BlueprintValue::from_float(0.1f)});
    }

    // Do Once lets exactly one pass through, so Flip Flop only ever sees one.
    EXPECT_EQ(fixture.host.joined(), "once|A");
  }

  TEST(BlueprintVmTest, GateBlocksUntilOpened)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto tick = builder.add("event.tick");
    const auto gate = builder.add("flow.gate");
    const auto through = add_print(builder, "through");

    builder.exec(tick, "exec", gate, "enter");
    builder.exec(gate, "exit", through);
    builder.literal(gate, "startOpen", BlueprintValue::from_bool(false));

    // A custom event opens the gate.
    const auto open = builder.add("event.custom", "name", "Open");
    builder.exec(open, "exec", gate, "open");

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());

    fixture.vm.dispatch(instance, "tick", {BlueprintValue::from_float(0.1f)});
    EXPECT_TRUE(fixture.host.lines.empty());

    fixture.vm.dispatch(instance, "custom:Open");
    fixture.vm.dispatch(instance, "tick", {BlueprintValue::from_float(0.1f)});
    EXPECT_EQ(fixture.host.joined(), "through");
  }

  TEST(BlueprintVmTest, DelaySuspendsAndResumesAfterTheRequestedTime)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto before = add_print(builder, "before");
    const auto delay = builder.add("flow.delay");
    const auto after = add_print(builder, "after");

    builder.exec(begin, "exec", before);
    builder.exec(before, "then", delay);
    builder.exec(delay, "completed", after);
    builder.literal(delay, "duration", BlueprintValue::from_float(0.5f));

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());

    fixture.vm.dispatch(instance, "begin_play");
    EXPECT_EQ(fixture.host.joined(), "before");
    ASSERT_EQ(instance.latentActions.size(), 1U);

    fixture.vm.advance_latent_actions(instance, 0.2f);
    EXPECT_EQ(fixture.host.joined(), "before");
    EXPECT_EQ(instance.latentActions.size(), 1U);

    fixture.vm.advance_latent_actions(instance, 0.4f);
    EXPECT_EQ(fixture.host.joined(), "before|after");
    EXPECT_TRUE(instance.latentActions.empty());
  }

  TEST(BlueprintVmTest, DelayInsideALoopResumesTheRemainingIterations)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto loop = builder.add("flow.for_loop");
    const auto body = builder.add("debug.print");
    const auto delay = builder.add("flow.delay");
    const auto done = add_print(builder, "done");

    builder.exec(begin, "exec", loop);
    builder.exec(loop, "loopBody", body);
    builder.exec(body, "then", delay);
    builder.exec(loop, "completed", done);
    builder.data(loop, "index", body, "text");
    builder.literal(loop, "first", BlueprintValue::from_int(0));
    builder.literal(loop, "last", BlueprintValue::from_int(2));
    builder.literal(delay, "duration", BlueprintValue::from_float(1.0f));

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());

    // The loop's continuation has to survive the suspension, otherwise the
    // remaining iterations are lost.
    fixture.vm.dispatch(instance, "begin_play");
    EXPECT_EQ(fixture.host.joined(), "0");

    fixture.vm.advance_latent_actions(instance, 1.0f);
    EXPECT_EQ(fixture.host.joined(), "0|1");

    fixture.vm.advance_latent_actions(instance, 1.0f);
    EXPECT_EQ(fixture.host.joined(), "0|1|2");

    fixture.vm.advance_latent_actions(instance, 1.0f);
    EXPECT_EQ(fixture.host.joined(), "0|1|2|done");
  }

  TEST(BlueprintVmTest, PureNodesReEvaluateForEveryConsumer)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto loop = builder.add("flow.for_loop");
    const auto multiply = builder.add("math.multiply_int");
    const auto print = builder.add("debug.print");

    builder.exec(begin, "exec", loop);
    builder.exec(loop, "loopBody", print);
    builder.literal(loop, "first", BlueprintValue::from_int(1));
    builder.literal(loop, "last", BlueprintValue::from_int(3));

    // The pure Multiply must be recomputed each iteration from the loop index.
    builder.data(loop, "index", multiply, "a");
    builder.literal(multiply, "b", BlueprintValue::from_int(10));
    builder.data(multiply, "result", print, "text");

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());
    fixture.vm.dispatch(instance, "begin_play");

    EXPECT_EQ(fixture.host.joined(), "10|20|30");
  }

  TEST(BlueprintVmTest, VariablesPersistAcrossTicksAndHonourInstanceOverrides)
  {
    Blueprint blueprint;

    BlueprintVariable score;
    score.name = "Score";
    score.type = ValueType::Int;
    score.defaultValue = BlueprintValue::from_int(0);
    blueprint.variables.push_back(score);

    GraphBuilder builder(blueprint, blueprint.eventGraph);
    const auto tick = builder.add("event.tick");
    const auto get = builder.add("variable.get", "variable", "Score");
    const auto add = builder.add("math.add_int");
    const auto set = builder.add("variable.set", "variable", "Score");

    builder.exec(tick, "exec", set);
    builder.data(get, "value", add, "a");
    builder.literal(add, "b", BlueprintValue::from_int(5));
    builder.data(add, "result", set, "value");

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());

    EXPECT_EQ(instance.variables[0].as_int(), 0);
    for (int i = 0; i < 4; ++i)
    {
      fixture.vm.dispatch(instance, "tick", {BlueprintValue::from_float(0.1f)});
    }
    EXPECT_EQ(instance.variables[0].as_int(), 20);

    // Starting from an overridden value keeps the same increments.
    auto overridden = make_blueprint_instance(compiled, fixture.create_entity());
    overridden.variables[0] = BlueprintValue::parse("100", ValueType::Int);
    fixture.vm.dispatch(overridden, "tick", {BlueprintValue::from_float(0.1f)});
    EXPECT_EQ(overridden.variables[0].as_int(), 105);
  }

  TEST(BlueprintVmTest, UserFunctionsPassArgumentsAndReturnValues)
  {
    Blueprint blueprint;

    BlueprintFunction function;
    function.name = "Double";
    BlueprintVariable input;
    input.name = "Value";
    input.type = ValueType::Int;
    function.inputs.push_back(input);
    BlueprintVariable output;
    output.name = "Result";
    output.type = ValueType::Int;
    function.outputs.push_back(output);
    blueprint.functions.push_back(function);

    {
      GraphBuilder body(blueprint, blueprint.functions[0].graph);
      const auto entry = body.add("function.entry");
      const auto multiply = body.add("math.multiply_int");
      const auto result = body.add("function.result");
      body.exec(entry, "exec", result);
      body.data(entry, "Value", multiply, "a");
      body.literal(multiply, "b", BlueprintValue::from_int(2));
      body.data(multiply, "result", result, "Result");
    }

    GraphBuilder builder(blueprint, blueprint.eventGraph);
    const auto begin = builder.add("event.begin_play");
    const auto call = builder.add("function.call", "function", "Double");
    const auto print = builder.add("debug.print");

    builder.exec(begin, "exec", call);
    builder.exec(call, "then", print);
    builder.literal(call, "Value", BlueprintValue::from_int(21));
    builder.data(call, "Result", print, "text");

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());
    fixture.vm.dispatch(instance, "begin_play");

    EXPECT_EQ(fixture.host.joined(), "42");
  }

  TEST(BlueprintVmTest, RecursiveFunctionsUnwindAndAreDepthLimited)
  {
    // Factorial(n) = n <= 1 ? 1 : n * Factorial(n - 1)
    Blueprint blueprint;

    BlueprintFunction function;
    function.name = "Factorial";
    function.allowRecursion = true;
    BlueprintVariable input;
    input.name = "N";
    input.type = ValueType::Int;
    function.inputs.push_back(input);
    BlueprintVariable output;
    output.name = "Result";
    output.type = ValueType::Int;
    function.outputs.push_back(output);
    blueprint.functions.push_back(function);

    {
      GraphBuilder body(blueprint, blueprint.functions[0].graph);
      const auto entry = body.add("function.entry");
      const auto branch = body.add("flow.branch");
      const auto lessEqual = body.add("logic.less_equal");
      const auto baseCase = body.add("function.result");
      const auto minusOne = body.add("math.subtract_int");
      const auto recurse = body.add("function.call", "function", "Factorial");
      const auto multiply = body.add("math.multiply_int");
      const auto recursiveCase = body.add("function.result");

      body.exec(entry, "exec", branch);
      body.data(entry, "N", lessEqual, "a");
      body.literal(lessEqual, "b", BlueprintValue::from_float(1.0f));
      body.data(lessEqual, "result", branch, "condition");

      body.exec(branch, "true", baseCase);
      body.literal(baseCase, "Result", BlueprintValue::from_int(1));

      body.exec(branch, "false", recurse);
      body.data(entry, "N", minusOne, "a");
      body.literal(minusOne, "b", BlueprintValue::from_int(1));
      body.data(minusOne, "result", recurse, "N");

      body.exec(recurse, "then", recursiveCase);
      body.data(entry, "N", multiply, "a");
      body.data(recurse, "Result", multiply, "b");
      body.data(multiply, "result", recursiveCase, "Result");
    }

    GraphBuilder builder(blueprint, blueprint.eventGraph);
    const auto begin = builder.add("event.begin_play");
    const auto call = builder.add("function.call", "function", "Factorial");
    const auto print = builder.add("debug.print");
    builder.exec(begin, "exec", call);
    builder.exec(call, "then", print);
    builder.literal(call, "N", BlueprintValue::from_int(6));
    builder.data(call, "Result", print, "text");

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());
    fixture.vm.dispatch(instance, "begin_play");

    EXPECT_EQ(fixture.host.joined(), "720");

    // Runaway recursion is caught rather than blowing the C++ stack.
    auto runaway = make_blueprint_instance(compiled, fixture.create_entity());
    fixture.host.lines.clear();
    auto &callNode = *compiled.blueprint.eventGraph.find_node(call);
    (void)callNode;

    Blueprint deep = blueprint;
    deep.eventGraph.find_node(call)->pinDefaults["N"] = BlueprintValue::from_int(500);
    const CompiledBlueprint deepCompiled = compile_ok(deep);
    auto deepInstance = make_blueprint_instance(deepCompiled, fixture.create_entity());
    fixture.vm.dispatch(deepInstance, "begin_play");
    EXPECT_TRUE(deepInstance.faulted);
    EXPECT_NE(deepInstance.error.find("call depth"), std::string::npos);
  }

  TEST(BlueprintVmTest, CustomEventsCanBeCalledFromTheGraph)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto call = builder.add("flow.call_event", "name", "Greet");
    const auto afterCall = add_print(builder, "after");

    const auto custom = builder.add("event.custom", "name", "Greet");
    const auto greet = add_print(builder, "greet");

    builder.exec(begin, "exec", call);
    builder.exec(call, "then", afterCall);
    builder.exec(custom, "exec", greet);

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());
    fixture.vm.dispatch(instance, "begin_play");

    // The called event runs to completion before the caller continues.
    EXPECT_EQ(fixture.host.joined(), "greet|after");
  }

  TEST(BlueprintVmTest, TransformNodesReadAndWriteTheEcs)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto tick = builder.add("event.tick");
    const auto offset = builder.add("transform.add_offset");
    const auto make = builder.add("vector.make");
    const auto scale = builder.add("math.multiply");

    builder.exec(tick, "exec", offset);
    builder.data(tick, "deltaSeconds", scale, "a");
    builder.literal(scale, "b", BlueprintValue::from_float(10.0f));
    builder.data(scale, "result", make, "x");
    builder.data(make, "vector", offset, "delta");

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    const Entity::EntityId entity = fixture.create_entity();
    auto instance = make_blueprint_instance(compiled, entity);

    for (int i = 0; i < 4; ++i)
    {
      fixture.vm.set_delta_time(0.25f);
      fixture.vm.dispatch(instance, "tick", {BlueprintValue::from_float(0.25f)});
    }

    const auto &position = fixture.componentManager.getComponent<PositionComponent3D>(entity);
    EXPECT_FLOAT_EQ(position.x, 10.0f);
    EXPECT_FLOAT_EQ(position.y, 0.0f);
  }

  TEST(BlueprintVmTest, EntityPinsDefaultToSelfAndFindByNameLocatesOthers)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto find = builder.add("entity.find_by_name");
    const auto setPosition = builder.add("transform.set_position");
    const auto selfName = builder.add("entity.get_name");
    const auto print = builder.add("debug.print");

    builder.exec(begin, "exec", setPosition);
    builder.exec(setPosition, "then", print);
    builder.literal(find, "name", BlueprintValue::from_string("Target"));
    builder.data(find, "entity", setPosition, "target");
    builder.literal(setPosition, "position", BlueprintValue::from_vector(math::Vec3(4.0f, 5.0f, 6.0f)));
    builder.data(selfName, "name", print, "text");

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    const Entity::EntityId owner = fixture.create_entity("Owner");
    const Entity::EntityId target = fixture.create_entity("Target");

    auto instance = make_blueprint_instance(compiled, owner);
    fixture.vm.dispatch(instance, "begin_play");

    const auto &targetPosition = fixture.componentManager.getComponent<PositionComponent3D>(target);
    EXPECT_FLOAT_EQ(targetPosition.y, 5.0f);
    // The owner was untouched: the wired target pin overrode the implicit self.
    EXPECT_FLOAT_EQ(fixture.componentManager.getComponent<PositionComponent3D>(owner).y, 0.0f);
    // Get Name with an unwired target pin fell back to self.
    EXPECT_EQ(fixture.host.joined(), "Owner");
  }

  TEST(BlueprintVmTest, HostServicesReceiveGraphSideEffects)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto impulse = builder.add("physics.add_impulse");
    const auto observe = builder.add("debug.observe");
    const auto load = builder.add("world.load");

    builder.exec(begin, "exec", impulse);
    builder.exec(impulse, "then", observe);
    builder.exec(observe, "then", load);
    builder.literal(impulse, "impulse", BlueprintValue::from_vector(math::Vec3(0.0f, 12.0f, 0.0f)));
    builder.literal(observe, "key", BlueprintValue::from_string("score"));
    builder.literal(observe, "value", BlueprintValue::from_int(7));
    builder.literal(load, "worldName", BlueprintValue::from_string("Level2"));

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    const Entity::EntityId entity = fixture.create_entity();
    auto instance = make_blueprint_instance(compiled, entity);
    fixture.vm.dispatch(instance, "begin_play");

    ASSERT_EQ(fixture.host.impulses.size(), 1U);
    EXPECT_EQ(fixture.host.impulses[0].first, entity);
    EXPECT_FLOAT_EQ(fixture.host.impulses[0].second.y, 12.0f);

    ASSERT_EQ(fixture.host.observations.size(), 1U);
    EXPECT_EQ(fixture.host.observations[0].first, "score");
    EXPECT_EQ(fixture.host.observations[0].second, "7");

    ASSERT_EQ(fixture.host.loadedWorlds.size(), 1U);
    EXPECT_EQ(fixture.host.loadedWorlds[0], "Level2");
  }

  TEST(BlueprintVmTest, EventPayloadsLandOnTheEventNodesOutputPins)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto keyDown = builder.add("event.key_down");
    const auto print = builder.add("debug.print");
    builder.exec(keyDown, "exec", print);
    builder.data(keyDown, "keyCode", print, "text");

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());

    fixture.vm.dispatch(instance, "key_down", {BlueprintValue::from_int(32)});
    fixture.vm.dispatch(instance, "key_down", {BlueprintValue::from_int(27)});

    EXPECT_EQ(fixture.host.joined(), "32|27");
  }

  TEST(BlueprintVmTest, RandomNodesAreReproducibleForAFixedSeed)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto random = builder.add("math.random_int");
    const auto print = builder.add("debug.print");
    builder.exec(begin, "exec", print);
    builder.literal(random, "min", BlueprintValue::from_int(0));
    builder.literal(random, "max", BlueprintValue::from_int(1000));
    builder.data(random, "result", print, "text");

    const CompiledBlueprint compiled = compile_ok(blueprint);

    const auto run = [&compiled]()
    {
      VmFixture fixture;
      fixture.vm.set_random_seed(1234u);
      auto instance = make_blueprint_instance(compiled, fixture.create_entity());
      fixture.vm.dispatch(instance, "begin_play");
      return fixture.host.joined();
    };

    EXPECT_EQ(run(), run());
  }

  TEST(BlueprintVmTest, InstanceResetRestoresVariableDefaultsAndClearsLatents)
  {
    Blueprint blueprint;

    BlueprintVariable health;
    health.name = "Health";
    health.type = ValueType::Float;
    health.defaultValue = BlueprintValue::from_float(100.0f);
    blueprint.variables.push_back(health);

    GraphBuilder builder(blueprint, blueprint.eventGraph);
    const auto begin = builder.add("event.begin_play");
    const auto set = builder.add("variable.set", "variable", "Health");
    const auto delay = builder.add("flow.delay");
    builder.exec(begin, "exec", set);
    builder.exec(set, "then", delay);
    builder.literal(set, "value", BlueprintValue::from_float(5.0f));
    builder.literal(delay, "duration", BlueprintValue::from_float(99.0f));

    // A Delay whose Completed pin is wired is the only kind that suspends.
    const auto after = add_print(builder, "after");
    builder.exec(delay, "completed", after);

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());

    fixture.vm.dispatch(instance, "begin_play");
    EXPECT_FLOAT_EQ(instance.variables[0].as_float(), 5.0f);
    EXPECT_EQ(instance.latentActions.size(), 1U);

    instance.reset();
    EXPECT_FLOAT_EQ(instance.variables[0].as_float(), 100.0f);
    EXPECT_TRUE(instance.latentActions.empty());
    EXPECT_FALSE(instance.started);
  }

  TEST(BlueprintVmTest, DelayInsideASequenceBranchResumesTheRemainingPins)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto sequence = builder.add("flow.sequence", "outputs", 3);
    const auto first = add_print(builder, "first");
    const auto delay = builder.add("flow.delay");
    const auto second = add_print(builder, "second");
    const auto third = add_print(builder, "third");

    builder.exec(begin, "exec", sequence);
    builder.exec(sequence, "then0", first);
    builder.exec(first, "then", delay);
    builder.exec(sequence, "then1", second);
    builder.exec(sequence, "then2", third);
    builder.literal(delay, "duration", BlueprintValue::from_float(1.0f));

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());

    // The first branch suspends, so the later pins must wait rather than run
    // ahead of it or be dropped.
    fixture.vm.dispatch(instance, "begin_play");
    EXPECT_EQ(fixture.host.joined(), "first");

    fixture.vm.advance_latent_actions(instance, 1.0f);
    EXPECT_EQ(fixture.host.joined(), "first|second|third");
    EXPECT_TRUE(instance.latentActions.empty());
  }

  TEST(BlueprintVmTest, ChainedDelaysFireOnSeparateFrames)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto firstDelay = builder.add("flow.delay");
    const auto middle = add_print(builder, "one");
    const auto secondDelay = builder.add("flow.delay");
    const auto last = add_print(builder, "two");

    builder.exec(begin, "exec", firstDelay);
    builder.exec(firstDelay, "completed", middle);
    builder.exec(middle, "then", secondDelay);
    builder.exec(secondDelay, "completed", last);
    builder.literal(firstDelay, "duration", BlueprintValue::from_float(0.5f));
    builder.literal(secondDelay, "duration", BlueprintValue::from_float(0.5f));

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());

    fixture.vm.dispatch(instance, "begin_play");
    EXPECT_TRUE(fixture.host.lines.empty());

    // Resuming must not immediately consume the delay the resumed chain queued.
    fixture.vm.advance_latent_actions(instance, 0.5f);
    EXPECT_EQ(fixture.host.joined(), "one");
    EXPECT_EQ(instance.latentActions.size(), 1U);

    fixture.vm.advance_latent_actions(instance, 0.5f);
    EXPECT_EQ(fixture.host.joined(), "one|two");
  }

  TEST(BlueprintVmTest, SwappingTheHostMidRunKeepsInstancesAlive)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);
    const auto tick = builder.add("event.tick");
    const auto print = add_print(builder, "tick");
    builder.exec(tick, "exec", print);

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity());

    fixture.vm.dispatch(instance, "tick", {BlueprintValue::from_float(0.1f)});
    EXPECT_EQ(fixture.host.lines.size(), 1U);

    RecordingHost replacement;
    fixture.vm.set_host(replacement);
    fixture.vm.dispatch(instance, "tick", {BlueprintValue::from_float(0.1f)});

    EXPECT_EQ(fixture.host.lines.size(), 1U);
    EXPECT_EQ(replacement.joined(), "tick");
  }

  TEST(BlueprintVmTest, IsValidReportsFalseForANoneHandle)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto find = builder.add("entity.find_by_name");
    const auto isValid = builder.add("entity.is_valid");
    const auto print = builder.add("debug.print");

    builder.exec(begin, "exec", print);
    builder.literal(find, "name", BlueprintValue::from_string("NoSuchEntity"));
    builder.data(find, "entity", isValid, "target");
    builder.data(isValid, "valid", print, "text");

    const CompiledBlueprint compiled = compile_ok(blueprint);
    VmFixture fixture;
    auto instance = make_blueprint_instance(compiled, fixture.create_entity("Owner"));
    fixture.vm.dispatch(instance, "begin_play");

    // A wired-but-None handle must not silently resolve to the owning entity.
    EXPECT_EQ(fixture.host.joined(), "false");
  }

  TEST(BlueprintVmTest, IntegerDivisionSurvivesItsUndefinedCases)
  {
    const auto run = [](const char *type, std::int32_t a, std::int32_t b)
    {
      Blueprint blueprint;
      GraphBuilder builder(blueprint, blueprint.eventGraph);
      const auto begin = builder.add("event.begin_play");
      const auto op = builder.add(type);
      const auto print = builder.add("debug.print");
      builder.exec(begin, "exec", print);
      builder.literal(op, "a", BlueprintValue::from_int(a));
      builder.literal(op, "b", BlueprintValue::from_int(b));
      builder.data(op, "result", print, "text");

      CompiledBlueprint compiled = compile_blueprint(blueprint);
      EXPECT_TRUE(compiled.succeeded) << compiled.error_summary();

      VmFixture fixture;
      auto instance = make_blueprint_instance(compiled, fixture.create_entity());
      fixture.vm.dispatch(instance, "begin_play");
      return fixture.host.joined();
    };

    constexpr std::int32_t kMin = -2147483647 - 1;
    EXPECT_EQ(run("math.divide_int", 7, 2), "3");
    EXPECT_EQ(run("math.divide_int", 7, 0), "0");
    EXPECT_EQ(run("math.divide_int", kMin, -1), "0");
    EXPECT_EQ(run("math.modulo_int", 7, 3), "1");
    EXPECT_EQ(run("math.modulo_int", 7, 0), "0");
    EXPECT_EQ(run("math.modulo_int", kMin, -1), "0");
  }
}
