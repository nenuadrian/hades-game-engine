#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "test_support.hpp"

#include "../engine/blueprint/blueprint_asset.hpp"
#include "../engine/blueprint/blueprint_compiler.hpp"
#include "../engine/blueprint/blueprint_graph.hpp"
#include "../engine/blueprint/blueprint_node_registry.hpp"
#include "../engine/blueprint/blueprint_value.hpp"

namespace hades
{
  using test_support::ScopedDirectoryCleanup;
  using test_support::unique_test_directory;

  namespace
  {
    BlueprintNode make_node(Blueprint &blueprint, const char *type, float x = 0.0f, float y = 0.0f)
    {
      BlueprintNode node;
      node.id = blueprint.allocate_node_id();
      node.type = type;
      node.x = x;
      node.y = y;
      return node;
    }
  }

  TEST(BlueprintValueTest, CoercionFollowsTheDeclaredTypeLattice)
  {
    EXPECT_EQ(BlueprintValue::from_int(3).coerced_to(ValueType::Float).as_float(), 3.0f);
    EXPECT_EQ(BlueprintValue::from_float(3.9f).coerced_to(ValueType::Int).as_int(), 3);
    EXPECT_TRUE(BlueprintValue::from_int(1).coerced_to(ValueType::Bool).as_bool());
    EXPECT_FALSE(BlueprintValue::from_int(0).coerced_to(ValueType::Bool).as_bool());
    EXPECT_EQ(BlueprintValue::from_bool(true).coerced_to(ValueType::String).as_string(), "true");
    EXPECT_EQ(BlueprintValue::from_float(1.5f).as_string(), "1.5");
    EXPECT_EQ(BlueprintValue::from_float(2.0f).as_string(), "2");

    // Scalars splat into vectors so a float wired into a vector pin is usable.
    const math::Vec3 splat = BlueprintValue::from_float(2.0f).coerced_to(ValueType::Vector).as_vector();
    EXPECT_FLOAT_EQ(splat.x, 2.0f);
    EXPECT_FLOAT_EQ(splat.z, 2.0f);
  }

  TEST(BlueprintValueTest, ConvertibilityAndLossinessTables)
  {
    EXPECT_TRUE(value_type_convertible(ValueType::Int, ValueType::Float));
    EXPECT_FALSE(value_type_conversion_is_lossy(ValueType::Int, ValueType::Float));

    EXPECT_TRUE(value_type_convertible(ValueType::Float, ValueType::Int));
    EXPECT_TRUE(value_type_conversion_is_lossy(ValueType::Float, ValueType::Int));

    EXPECT_TRUE(value_type_convertible(ValueType::Vector, ValueType::String));
    EXPECT_FALSE(value_type_convertible(ValueType::String, ValueType::Vector));

    // Exec never mixes with data.
    EXPECT_FALSE(value_type_convertible(ValueType::Exec, ValueType::Bool));
    EXPECT_FALSE(value_type_convertible(ValueType::Float, ValueType::Exec));
  }

  TEST(BlueprintValueTest, StorageStringsKeepFullPrecision)
  {
    // The display formatter rounds to three decimals; storage must not, or an
    // inspector override of 0.0001 would be persisted as "0".
    const auto small = BlueprintValue::from_float(0.0001f);
    EXPECT_EQ(small.to_display_string(), "0");
    EXPECT_FLOAT_EQ(BlueprintValue::parse(small.to_storage_string(), ValueType::Float).as_float(), 0.0001f);

    const auto precise = BlueprintValue::from_vector(math::Vec3(0.0001f, 1234.5678f, -0.00025f));
    const auto restored = BlueprintValue::parse(precise.to_storage_string(), ValueType::Vector).as_vector();
    EXPECT_FLOAT_EQ(restored.x, 0.0001f);
    EXPECT_FLOAT_EQ(restored.y, 1234.5678f);
    EXPECT_FLOAT_EQ(restored.z, -0.00025f);
  }

  TEST(BlueprintValueTest, NumericAccessorsAgreeOnStringsAndSaturateOnOverflow)
  {
    const auto text = BlueprintValue::from_string("2.5");
    EXPECT_FLOAT_EQ(text.as_float(), 2.5f);
    EXPECT_EQ(text.as_int(), 2);
    EXPECT_FLOAT_EQ(text.as_vector().y, 2.5f);

    EXPECT_FLOAT_EQ(BlueprintValue::from_string("nonsense").as_float(), 0.0f);

    // Casting a float outside int32 range is undefined; the accessor saturates.
    EXPECT_EQ(BlueprintValue::from_float(1.0e30f).as_int(), 2147483647);
    EXPECT_EQ(BlueprintValue::from_float(-1.0e30f).as_int(), -2147483648);
  }

  TEST(BlueprintValueTest, StorageStringsRoundTrip)
  {
    const auto vector = BlueprintValue::from_vector(math::Vec3(1.0f, -2.5f, 0.25f));
    const auto parsed = BlueprintValue::parse(vector.to_storage_string(), ValueType::Vector);
    EXPECT_FLOAT_EQ(parsed.as_vector().x, 1.0f);
    EXPECT_FLOAT_EQ(parsed.as_vector().y, -2.5f);
    EXPECT_FLOAT_EQ(parsed.as_vector().z, 0.25f);

    // The display form is accepted too, so pasting from a tooltip works.
    const auto fromDisplay = BlueprintValue::parse("(3, 4, 5)", ValueType::Vector);
    EXPECT_FLOAT_EQ(fromDisplay.as_vector().y, 4.0f);

    EXPECT_TRUE(BlueprintValue::parse("true", ValueType::Bool).as_bool());
    EXPECT_EQ(BlueprintValue::parse("-7", ValueType::Int).as_int(), -7);
    EXPECT_EQ(BlueprintValue::parse("not a number", ValueType::Int).as_int(), 0);
  }

  TEST(BlueprintGraphTest, JsonRoundTripPreservesNodesLinksVariablesAndFunctions)
  {
    Blueprint blueprint;
    blueprint.name = "Round Trip";
    blueprint.description = "fixture";

    BlueprintVariable speed;
    speed.name = "Speed";
    speed.type = ValueType::Float;
    speed.defaultValue = BlueprintValue::from_float(7.5f);
    speed.exposed = true;
    blueprint.variables.push_back(speed);

    BlueprintFunction function;
    function.name = "Double";
    function.allowRecursion = false;
    BlueprintVariable input;
    input.name = "In";
    input.type = ValueType::Float;
    function.inputs.push_back(input);
    BlueprintVariable output;
    output.name = "Out";
    output.type = ValueType::Float;
    function.outputs.push_back(output);
    blueprint.functions.push_back(function);

    auto begin = make_node(blueprint, "event.begin_play", -100.0f, 20.0f);
    auto print = make_node(blueprint, "debug.print", 150.0f, 20.0f);
    print.pinDefaults["text"] = BlueprintValue::from_string("hello");
    print.comment = "greeting";
    print.config["level"] = "warning";

    BlueprintLink link;
    link.kind = BlueprintLinkKind::Exec;
    link.from = {begin.id, "exec"};
    link.to = {print.id, "exec"};

    blueprint.eventGraph.nodes.push_back(begin);
    blueprint.eventGraph.nodes.push_back(print);
    blueprint.eventGraph.links.push_back(link);

    Blueprint restored;
    std::string error;
    ASSERT_TRUE(Blueprint::from_json(blueprint.to_json(), restored, &error)) << error;

    EXPECT_EQ(restored.name, "Round Trip");
    EXPECT_EQ(restored.description, "fixture");
    ASSERT_EQ(restored.variables.size(), 1U);
    EXPECT_EQ(restored.variables[0].name, "Speed");
    EXPECT_EQ(restored.variables[0].type, ValueType::Float);
    EXPECT_FLOAT_EQ(restored.variables[0].defaultValue.as_float(), 7.5f);

    ASSERT_EQ(restored.functions.size(), 1U);
    EXPECT_EQ(restored.functions[0].name, "Double");
    ASSERT_EQ(restored.functions[0].inputs.size(), 1U);
    ASSERT_EQ(restored.functions[0].outputs.size(), 1U);

    ASSERT_EQ(restored.eventGraph.nodes.size(), 2U);
    ASSERT_EQ(restored.eventGraph.links.size(), 1U);
    EXPECT_EQ(restored.eventGraph.links[0].from.pin, "exec");
    EXPECT_EQ(restored.eventGraph.nodes[1].comment, "greeting");
    EXPECT_EQ(restored.eventGraph.nodes[1].config.value("level", std::string()), "warning");
    EXPECT_EQ(restored.eventGraph.nodes[1].pinDefaults.at("text").as_string(), "hello");
    EXPECT_FLOAT_EQ(restored.eventGraph.nodes[1].x, 150.0f);
  }

  TEST(BlueprintGraphTest, NormalizeDropsDanglingLinksAndRepairsIds)
  {
    Blueprint blueprint;
    auto begin = make_node(blueprint, "event.begin_play");
    auto print = make_node(blueprint, "debug.print");
    const BlueprintNodeId printId = print.id;

    blueprint.eventGraph.nodes.push_back(begin);
    blueprint.eventGraph.nodes.push_back(print);

    // Good link, dangling link, self link, and an exact duplicate.
    blueprint.eventGraph.links.push_back({BlueprintLinkKind::Exec, {begin.id, "exec"}, {printId, "exec"}});
    blueprint.eventGraph.links.push_back({BlueprintLinkKind::Exec, {begin.id, "exec"}, {9999, "exec"}});
    blueprint.eventGraph.links.push_back({BlueprintLinkKind::Exec, {begin.id, "exec"}, {begin.id, "exec"}});
    blueprint.eventGraph.links.push_back({BlueprintLinkKind::Exec, {begin.id, "exec"}, {printId, "exec"}});

    // Two nodes sharing an id must be split apart.
    blueprint.eventGraph.nodes.push_back(make_node(blueprint, "debug.print"));
    blueprint.eventGraph.nodes.back().id = begin.id;

    EXPECT_GT(blueprint.normalize(), 0);
    ASSERT_EQ(blueprint.eventGraph.links.size(), 1U);
    EXPECT_EQ(blueprint.eventGraph.links[0].to.node, printId);

    EXPECT_NE(blueprint.eventGraph.nodes[0].id, blueprint.eventGraph.nodes[2].id);
  }

  TEST(BlueprintGraphTest, LinkQueriesDistinguishInputsFromOutputs)
  {
    BlueprintGraph graph;

    BlueprintNode a;
    a.id = 1;
    a.type = "math.add";
    BlueprintNode b;
    b.id = 2;
    b.type = "math.add";
    graph.nodes.push_back(a);
    graph.nodes.push_back(b);

    graph.links.push_back({BlueprintLinkKind::Data, {1, "result"}, {2, "a"}});

    EXPECT_NE(graph.incoming_data_link({2, "a"}), nullptr);
    EXPECT_EQ(graph.incoming_data_link({1, "result"}), nullptr);
    EXPECT_TRUE(graph.pin_has_link({1, "result"}, BlueprintLinkKind::Data, false));
    EXPECT_FALSE(graph.pin_has_link({1, "result"}, BlueprintLinkKind::Data, true));

    graph.remove_links_into({2, "a"}, BlueprintLinkKind::Data);
    EXPECT_TRUE(graph.links.empty());
  }

  TEST(BlueprintAssetTest, SaveAndLoadRoundTripsThroughDisk)
  {
    const auto testRoot = unique_test_directory("hades-blueprint-asset");
    ScopedDirectoryCleanup cleanup(testRoot);
    std::filesystem::create_directories(testRoot);

    const Blueprint blueprint = make_starter_blueprint("Greeter");
    const auto file = blueprint_assets_directory(testRoot) / "Greeter.hbp";

    std::string error;
    ASSERT_TRUE(save_blueprint(file, blueprint, &error)) << error;
    ASSERT_TRUE(std::filesystem::exists(file));

    Blueprint loaded;
    ASSERT_TRUE(load_blueprint(file, loaded, &error)) << error;
    EXPECT_EQ(loaded.name, "Greeter");
    ASSERT_EQ(loaded.eventGraph.nodes.size(), 2U);
    ASSERT_EQ(loaded.eventGraph.links.size(), 1U);

    const auto assets = list_blueprint_assets(testRoot);
    ASSERT_EQ(assets.size(), 1U);
    EXPECT_EQ(assets.front(), "Blueprints/Greeter.hbp");
  }

  TEST(BlueprintAssetTest, LoadRejectsMalformedAndFutureVersionDocuments)
  {
    const auto testRoot = unique_test_directory("hades-blueprint-bad-asset");
    ScopedDirectoryCleanup cleanup(testRoot);
    std::filesystem::create_directories(testRoot);

    const auto broken = testRoot / "broken.hbp";
    {
      std::ofstream stream(broken);
      stream << "{ this is not json";
    }

    Blueprint blueprint;
    std::string error;
    EXPECT_FALSE(load_blueprint(broken, blueprint, &error));
    EXPECT_FALSE(error.empty());

    const auto future = testRoot / "future.hbp";
    {
      std::ofstream stream(future);
      stream << R"({"version": 9999, "name": "Future"})";
    }

    error.clear();
    EXPECT_FALSE(load_blueprint(future, blueprint, &error));
    EXPECT_NE(error.find("newer"), std::string::npos);
  }

  TEST(BlueprintNodeRegistryTest, BuiltInLibraryIsRegisteredAndSignaturesResolve)
  {
    register_builtin_blueprint_nodes();
    auto &registry = BlueprintNodeRegistry::instance();

    ASSERT_NE(registry.find("flow.branch"), nullptr);
    ASSERT_NE(registry.find("event.tick"), nullptr);
    EXPECT_EQ(registry.find("this.does.not.exist"), nullptr);

    // Every registered type must carry a category and an implementation.
    for (const auto *type : registry.all())
    {
      EXPECT_FALSE(type->category.empty()) << type->name;
      EXPECT_NE(type->fn, nullptr) << type->name;
      EXPECT_FALSE(type->displayName.empty()) << type->name;
    }

    Blueprint blueprint;
    BlueprintVariable variable;
    variable.name = "Health";
    variable.type = ValueType::Int;
    blueprint.variables.push_back(variable);

    BlueprintNode getter;
    getter.id = 1;
    getter.type = "variable.get";
    getter.config["variable"] = "Health";

    BlueprintSignatureContext context;
    context.blueprint = &blueprint;

    BlueprintNodeSignature signature;
    ASSERT_TRUE(resolve_blueprint_node_signature(context, getter, signature));
    ASSERT_EQ(signature.dataOutputs.size(), 1U);
    EXPECT_EQ(signature.dataOutputs[0].type, ValueType::Int);
    EXPECT_EQ(signature.title, "Get Health");

    // Sequence grows its exec pins from config.
    BlueprintNode sequence;
    sequence.id = 2;
    sequence.type = "flow.sequence";
    sequence.config["outputs"] = 4;
    ASSERT_TRUE(resolve_blueprint_node_signature(context, sequence, signature));
    EXPECT_EQ(signature.execOutputs.size(), 4U);
    EXPECT_EQ(signature.execOutputs[3], "then3");
  }

#ifdef HADES_TEST_PROJECT_DIR
  /// The sample project ships Blueprint assets that the editor opens on a
  /// fresh checkout. If a node type is renamed or a pin disappears, this is
  /// where it gets caught rather than in a hand-run of the editor.
  TEST(BlueprintAssetTest, ShippedSampleProjectBlueprintsStillCompile)
  {
    const std::filesystem::path project(HADES_TEST_PROJECT_DIR);
    ASSERT_TRUE(std::filesystem::is_directory(project)) << project;

    const auto assets = list_blueprint_assets(project);
    ASSERT_FALSE(assets.empty()) << "the sample project should ship at least one .hbp";

    for (const auto &relative : assets)
    {
      Blueprint blueprint;
      std::string error;
      ASSERT_TRUE(load_blueprint(project / relative, blueprint, &error)) << relative << ": " << error;

      const CompiledBlueprint compiled = compile_blueprint(blueprint);
      EXPECT_TRUE(compiled.succeeded) << relative << ":\n" << compiled.error_summary();

      for (const auto &message : compiled.messages)
      {
        EXPECT_TRUE(message.is_error()) << relative << " warning: " << message.text;
      }
    }
  }
#endif
}
