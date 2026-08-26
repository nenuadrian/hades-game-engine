#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "blueprint_test_support.hpp"

#include "../engine/blueprint/blueprint_compiler.hpp"

namespace hades
{
  using blueprint_test_support::GraphBuilder;

  namespace
  {
    bool has_error_containing(const CompiledBlueprint &compiled, const std::string &needle)
    {
      return std::any_of(
          compiled.messages.begin(),
          compiled.messages.end(),
          [&](const BlueprintCompileMessage &message)
          {
            return message.is_error() && message.text.find(needle) != std::string::npos;
          });
    }

    bool has_warning_containing(const CompiledBlueprint &compiled, const std::string &needle)
    {
      return std::any_of(
          compiled.messages.begin(),
          compiled.messages.end(),
          [&](const BlueprintCompileMessage &message)
          {
            return !message.is_error() && message.text.find(needle) != std::string::npos;
          });
    }
  }

  TEST(BlueprintCompilerTest, CompilesASimpleEventChain)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto print = builder.add("debug.print");
    builder.exec(begin, "exec", print);
    builder.literal(print, "text", BlueprintValue::from_string("ready"));

    const CompiledBlueprint compiled = compile_blueprint(blueprint);
    ASSERT_TRUE(compiled.succeeded) << compiled.error_summary();
    EXPECT_EQ(compiled.error_count(), 0);

    const int entry = compiled.eventGraph.find_event("begin_play");
    ASSERT_GE(entry, 0);

    // BeginPlay's single exec pin must point at the Print node.
    const auto &entryNode = compiled.eventGraph.nodes[static_cast<std::size_t>(entry)];
    ASSERT_EQ(entryNode.execTargets.size(), 1U);
    ASSERT_GE(entryNode.execTargets[0].node, 0);
    EXPECT_EQ(
        compiled.eventGraph.nodes[static_cast<std::size_t>(entryNode.execTargets[0].node)].source.type,
        "debug.print");
  }

  TEST(BlueprintCompilerTest, ResolvesLiteralsAndRegistersForDataWires)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto add = builder.add("math.add");
    const auto print = builder.add("debug.print");

    builder.literal(add, "a", BlueprintValue::from_float(2.0f));
    builder.literal(add, "b", BlueprintValue::from_float(3.0f));
    builder.exec(begin, "exec", print);
    builder.data(add, "result", print, "text");

    const CompiledBlueprint compiled = compile_blueprint(blueprint);
    ASSERT_TRUE(compiled.succeeded) << compiled.error_summary();

    // Float -> String on the Print pin is allowed but lossy, so it warns.
    EXPECT_TRUE(has_warning_containing(compiled, "lossy conversion"));

    // The Print node's pure dependency list must contain the Add node.
    const auto printIndex = std::distance(
        compiled.eventGraph.nodes.begin(),
        std::find_if(
            compiled.eventGraph.nodes.begin(),
            compiled.eventGraph.nodes.end(),
            [](const CompiledNode &node)
            { return node.source.type == "debug.print"; }));
    const auto &printNode = compiled.eventGraph.nodes[static_cast<std::size_t>(printIndex)];
    ASSERT_EQ(printNode.pureDeps.size(), 1U);
    EXPECT_EQ(
        compiled.eventGraph.nodes[static_cast<std::size_t>(printNode.pureDeps[0])].source.type,
        "math.add");

    // Two unwired float inputs were baked into the literal pool.
    EXPECT_GE(compiled.eventGraph.literals.size(), 2U);
    (void)print;
  }

  TEST(BlueprintCompilerTest, RejectsUnknownNodeTypes)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);
    builder.add("not.a.real.node");

    const CompiledBlueprint compiled = compile_blueprint(blueprint);
    EXPECT_FALSE(compiled.succeeded);
    EXPECT_TRUE(has_error_containing(compiled, "unknown node type"));
  }

  TEST(BlueprintCompilerTest, RejectsIncompatibleDataConnections)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto name = builder.add("entity.get_name");
    const auto scale = builder.add("vector.scale");
    // String cannot become a Vector.
    builder.data(name, "name", scale, "vector");

    const CompiledBlueprint compiled = compile_blueprint(blueprint);
    EXPECT_FALSE(compiled.succeeded);
    EXPECT_TRUE(has_error_containing(compiled, "cannot connect string to vector"));
  }

  TEST(BlueprintCompilerTest, RejectsFanInOnDataPinsAndFanOutOnExecPins)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto first = builder.add("debug.print");
    const auto second = builder.add("debug.print");
    builder.exec(begin, "exec", first);
    builder.exec(begin, "exec", second);

    const auto a = builder.add("math.add");
    const auto b = builder.add("math.add");
    const auto c = builder.add("math.add");
    builder.data(a, "result", c, "a");
    builder.data(b, "result", c, "a");

    const CompiledBlueprint compiled = compile_blueprint(blueprint);
    EXPECT_FALSE(compiled.succeeded);
    EXPECT_TRUE(has_error_containing(compiled, "outgoing wires; use a Sequence"));
    EXPECT_TRUE(has_error_containing(compiled, "incoming wires; data inputs accept one"));
  }

  TEST(BlueprintCompilerTest, RejectsDuplicateEventHandlers)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);
    builder.add("event.tick");
    builder.add("event.tick");

    const CompiledBlueprint compiled = compile_blueprint(blueprint);
    EXPECT_FALSE(compiled.succeeded);
    EXPECT_TRUE(has_error_containing(compiled, "duplicate event node"));
  }

  TEST(BlueprintCompilerTest, RejectsCyclesInPureDataDependencies)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto first = builder.add("math.add");
    const auto second = builder.add("math.add");
    builder.data(first, "result", second, "a");
    builder.data(second, "result", first, "a");

    const CompiledBlueprint compiled = compile_blueprint(blueprint);
    EXPECT_FALSE(compiled.succeeded);
    EXPECT_TRUE(has_error_containing(compiled, "circular data dependency"));
  }

  TEST(BlueprintCompilerTest, RejectsUnknownVariablesAndUnnamedCustomEvents)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);
    builder.add("variable.get", "variable", "Missing");
    builder.add("event.custom");

    const CompiledBlueprint compiled = compile_blueprint(blueprint);
    EXPECT_FALSE(compiled.succeeded);
    EXPECT_TRUE(has_error_containing(compiled, "unknown variable 'Missing'"));
    EXPECT_TRUE(has_error_containing(compiled, "custom event needs a name"));
  }

  TEST(BlueprintCompilerTest, RejectsLatentNodesAndEventsInsideFunctions)
  {
    Blueprint blueprint;

    BlueprintFunction function;
    function.name = "Helper";
    blueprint.functions.push_back(function);

    GraphBuilder builder(blueprint, blueprint.functions[0].graph);
    const auto entry = builder.add("function.entry");
    const auto delay = builder.add("flow.delay");
    builder.exec(entry, "exec", delay);
    builder.add("event.tick");

    const CompiledBlueprint compiled = compile_blueprint(blueprint);
    EXPECT_FALSE(compiled.succeeded);
    EXPECT_TRUE(has_error_containing(compiled, "cannot be used inside a function"));
    EXPECT_TRUE(has_error_containing(compiled, "event nodes are not allowed inside a function"));
  }

  TEST(BlueprintCompilerTest, RejectsUnintendedRecursionButAllowsItWhenOptedIn)
  {
    const auto build = [](bool allowRecursion)
    {
      Blueprint blueprint;
      BlueprintFunction function;
      function.name = "Countdown";
      function.allowRecursion = allowRecursion;
      blueprint.functions.push_back(function);

      GraphBuilder builder(blueprint, blueprint.functions[0].graph);
      const auto entry = builder.add("function.entry");
      const auto call = builder.add("function.call", "function", "Countdown");
      builder.exec(entry, "exec", call);
      return blueprint;
    };

    const CompiledBlueprint rejected = compile_blueprint(build(false));
    EXPECT_FALSE(rejected.succeeded);
    EXPECT_TRUE(has_error_containing(rejected, "calls itself"));

    const CompiledBlueprint accepted = compile_blueprint(build(true));
    EXPECT_TRUE(accepted.succeeded) << accepted.error_summary();
  }

  TEST(BlueprintCompilerTest, RequiresExactlyOneFunctionEntry)
  {
    Blueprint blueprint;
    BlueprintFunction function;
    function.name = "NoEntry";
    blueprint.functions.push_back(function);

    const CompiledBlueprint missing = compile_blueprint(blueprint);
    EXPECT_FALSE(missing.succeeded);
    EXPECT_TRUE(has_error_containing(missing, "has no entry node"));

    GraphBuilder builder(blueprint, blueprint.functions[0].graph);
    builder.add("function.entry");
    builder.add("function.entry");

    const CompiledBlueprint duplicated = compile_blueprint(blueprint);
    EXPECT_FALSE(duplicated.succeeded);
    EXPECT_TRUE(has_error_containing(duplicated, "more than one entry node"));
  }

  TEST(BlueprintCompilerTest, ResolvesWildcardPinsFromNeighbours)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto select = builder.add("logic.select");
    const auto position = builder.add("transform.set_position");

    builder.exec(begin, "exec", position);
    // Wiring the Select result into a Vector pin must retype all three
    // wildcard pins to Vector.
    builder.data(select, "result", position, "position");

    const CompiledBlueprint compiled = compile_blueprint(blueprint);
    ASSERT_TRUE(compiled.succeeded) << compiled.error_summary();

    const auto it = std::find_if(
        compiled.eventGraph.nodes.begin(),
        compiled.eventGraph.nodes.end(),
        [](const CompiledNode &node)
        { return node.source.type == "logic.select"; });
    ASSERT_NE(it, compiled.eventGraph.nodes.end());

    EXPECT_EQ(it->signature.dataOutputs[0].type, ValueType::Vector);
    EXPECT_EQ(it->signature.dataInputs[1].type, ValueType::Vector);
    EXPECT_EQ(it->signature.dataInputs[2].type, ValueType::Vector);
    // The condition pin stays a bool — it was never a wildcard.
    EXPECT_EQ(it->signature.dataInputs[0].type, ValueType::Bool);
  }

  TEST(BlueprintCompilerTest, WarnsAboutNodesNoEventCanReach)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);
    builder.add("debug.print");

    const CompiledBlueprint compiled = compile_blueprint(blueprint);
    EXPECT_TRUE(compiled.succeeded) << compiled.error_summary();
    EXPECT_TRUE(has_warning_containing(compiled, "is never reached from an event"));
  }

  TEST(BlueprintCompilerTest, RejectsDuplicateVariableAndFunctionNames)
  {
    Blueprint blueprint;

    BlueprintVariable variable;
    variable.name = "Speed";
    blueprint.variables.push_back(variable);
    blueprint.variables.push_back(variable);

    BlueprintFunction function;
    function.name = "Do";
    blueprint.functions.push_back(function);
    blueprint.functions.push_back(function);
    for (auto &entry : blueprint.functions)
    {
      GraphBuilder builder(blueprint, entry.graph);
      builder.add("function.entry");
    }

    const CompiledBlueprint compiled = compile_blueprint(blueprint);
    EXPECT_FALSE(compiled.succeeded);
    EXPECT_TRUE(has_error_containing(compiled, "duplicate variable name 'Speed'"));
    EXPECT_TRUE(has_error_containing(compiled, "duplicate function name 'Do'"));
  }

  TEST(BlueprintCompilerTest, ReportsStaleLinksLeftOnRemovedPins)
  {
    Blueprint blueprint;
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto print = builder.add("debug.print");
    // "notAPin" never existed on Print.
    builder.exec(begin, "exec", print, "notAPin");

    const CompiledBlueprint compiled = compile_blueprint(blueprint);
    EXPECT_FALSE(compiled.succeeded);
    EXPECT_TRUE(has_error_containing(compiled, "no execution input named 'notAPin'"));
  }
}
