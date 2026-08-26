// Coverage for the script <-> Blueprint bridge, in both directions:
//
//   script -> Blueprint   hades::Blueprints::sendEvent / setVariable / ...
//   Blueprint -> script   the `script.*` node category
//
// Most of it runs against a stub `BlueprintHost` rather than a live
// ScriptRuntime: the host *is* the seam the script nodes go through, and
// driving a real ScriptRuntime means invoking the C++ compiler. One test at
// the end does exactly that, to prove the facade resolves across the dylib
// boundary.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "blueprint_test_support.hpp"
#include "test_support.hpp"

#include "../engine/blueprint/blueprint_asset.hpp"
#include "../engine/blueprint/blueprint_engine_host.hpp"
#include "../engine/blueprint/blueprint_runtime.hpp"
#include "../engine/blueprint/script_blueprint.hpp"
#include "../engine/blueprint/script_blueprint_bridge.hpp"
#include "../engine/components/blueprint_component.hpp"
#include "../engine/components/name_component.hpp"
#include "../engine/components/script_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/runtime/script_runtime.hpp"

namespace hades
{
  using blueprint_test_support::GraphBuilder;
  using blueprint_test_support::RecordingHost;
  using test_support::ScopedDirectoryCleanup;
  using test_support::unique_test_directory;

  namespace
  {
    /// A RecordingHost that also answers the `script.*` nodes, standing in for
    /// a ScriptRuntime full of C++ scripts.
    class ScriptingHost : public RecordingHost
    {
    public:
      struct Message
      {
        Entity::EntityId entity = Entity::INVALID;
        std::string name;
        ScriptValue value;
      };

      ScriptValue send_script_message(
          Entity::EntityId entity,
          const std::string &name,
          const ScriptValue &value) override
      {
        messages.push_back({entity, name, value});
        return reply;
      }

      std::vector<Message> messages;
      /// What `onMessage` pretends to return. Empty means "not handled".
      ScriptValue reply;
    };

    struct BridgeFixture
    {
      explicit BridgeFixture(const char *prefix)
          : root(unique_test_directory(prefix)), cleanup(root)
      {
        std::filesystem::create_directories(root);
        runtime.set_host(&host);
        runtime.set_random_seed(7u);
      }

      std::string write_blueprint(const std::string &name, const Blueprint &blueprint)
      {
        const std::string relative = "Blueprints/" + name + kBlueprintFileExtension;
        std::string error;
        EXPECT_TRUE(save_blueprint(root / relative, blueprint, &error)) << error;
        return relative;
      }

      Entity::EntityId spawn(const std::string &name, const std::string &assetPath)
      {
        const Entity::EntityId entity = entityManager.createEntity();
        componentManager.addComponent(entity, NameComponent{name});

        BlueprintComponent component;
        BlueprintAttachment attachment;
        attachment.assetPath = assetPath;
        component.attachments.push_back(std::move(attachment));
        componentManager.addComponent(entity, component);
        return entity;
      }

      bool start()
      {
        std::string error;
        const bool ok = runtime.start(componentManager, entityManager, root, std::nullopt, &error);
        EXPECT_TRUE(ok) << error;
        return ok;
      }

      void tick(float deltaTime = 1.0f / 60.0f)
      {
        runtime.update(deltaTime, componentManager, entityManager);
      }

      std::filesystem::path root;
      ScopedDirectoryCleanup cleanup;
      EntityManager entityManager;
      ComponentManager componentManager{&entityManager};
      ScriptingHost host;
      BlueprintRuntime runtime;
    };

    void set_params(Blueprint &blueprint, BlueprintNodeId node, const nlohmann::json &params)
    {
      blueprint.eventGraph.find_node(node)->config["params"] = params;
    }

    BlueprintVariable make_variable(const char *name, ValueType type, BlueprintValue defaultValue)
    {
      BlueprintVariable variable;
      variable.name = name;
      variable.type = type;
      variable.defaultValue = std::move(defaultValue);
      return variable;
    }

    /// Two custom events. `Damaged(amount: float)` stores its argument in the
    /// `Damage` variable; `Ping` (no payload) sets `Pinged` true.
    Blueprint make_receiver_blueprint()
    {
      Blueprint blueprint;
      blueprint.name = "Receiver";
      blueprint.variables.push_back(
          make_variable("Damage", ValueType::Float, BlueprintValue::from_float(0.0f)));
      blueprint.variables.push_back(
          make_variable("Pinged", ValueType::Bool, BlueprintValue::from_bool(false)));

      GraphBuilder builder(blueprint, blueprint.eventGraph);

      const auto damaged = builder.add("event.custom", "name", "Damaged");
      set_params(blueprint, damaged, nlohmann::json::array({{{"name", "amount"}, {"type", "float"}}}));
      const auto storeDamage = builder.add("variable.set", "variable", "Damage");
      builder.exec(damaged, "exec", storeDamage);
      builder.data(damaged, "amount", storeDamage, "value");

      const auto ping = builder.add("event.custom", "name", "Ping");
      const auto storePinged = builder.add("variable.set", "variable", "Pinged");
      builder.literal(storePinged, "value", BlueprintValue::from_bool(true));
      builder.exec(ping, "exec", storePinged);

      return blueprint;
    }
  }

  // ---------------------------------------------------------------------------
  // script -> Blueprint
  // ---------------------------------------------------------------------------

  TEST(BlueprintScriptBridgeTest, FacadeIsInertWithoutARunningRuntime)
  {
    ASSERT_FALSE(Blueprints::isRunning());

    // None of these have anywhere to go; none of them may crash or claim
    // success either.
    Blueprints::sendEvent(1, "Damaged", {25.0f});
    Blueprints::broadcastEvent("Ping");

    EXPECT_FALSE(Blueprints::has(1));
    EXPECT_EQ(Blueprints::count(1), 0);
    EXPECT_FALSE(Blueprints::hasVariable(1, "Damage"));
    EXPECT_FLOAT_EQ(Blueprints::getFloat(1, "Damage"), 0.0f);
    EXPECT_FALSE(Blueprints::setFloat(1, "Damage", 5.0f));
    EXPECT_TRUE(Blueprints::getVariable(1, "Damage").empty());

    clear_pending_blueprint_events();
  }

  TEST(BlueprintScriptBridgeTest, SendEventDeliversItsPayloadOnTheNextUpdate)
  {
    BridgeFixture fixture("hades-bridge-send");
    const std::string asset = fixture.write_blueprint("Receiver", make_receiver_blueprint());
    const Entity::EntityId entity = fixture.spawn("Target", asset);

    ASSERT_TRUE(fixture.start());
    EXPECT_TRUE(Blueprints::isRunning());
    EXPECT_TRUE(Blueprints::has(entity));
    EXPECT_EQ(Blueprints::count(entity), 1);

    Blueprints::sendEvent(entity, "Damaged", {25.0f});

    // Queued, not immediate: nothing has run yet.
    EXPECT_FLOAT_EQ(Blueprints::getFloat(entity, "Damage"), 0.0f);

    fixture.tick();
    EXPECT_FLOAT_EQ(Blueprints::getFloat(entity, "Damage"), 25.0f);
    EXPECT_FALSE(fixture.runtime.faulted());
  }

  TEST(BlueprintScriptBridgeTest, PayloadValuesCoerceToTheDeclaredParameterType)
  {
    BridgeFixture fixture("hades-bridge-coerce");
    const std::string asset = fixture.write_blueprint("Receiver", make_receiver_blueprint());
    const Entity::EntityId entity = fixture.spawn("Target", asset);
    ASSERT_TRUE(fixture.start());

    // The parameter is declared float; the script sent a string.
    Blueprints::sendEvent(entity, "Damaged", {"12.5"});
    fixture.tick();
    EXPECT_FLOAT_EQ(Blueprints::getFloat(entity, "Damage"), 12.5f);
  }

  TEST(BlueprintScriptBridgeTest, EventsWithNoPayloadStillFire)
  {
    BridgeFixture fixture("hades-bridge-nopayload");
    const std::string asset = fixture.write_blueprint("Receiver", make_receiver_blueprint());
    const Entity::EntityId entity = fixture.spawn("Target", asset);
    ASSERT_TRUE(fixture.start());

    EXPECT_FALSE(Blueprints::getBool(entity, "Pinged"));
    Blueprints::sendEvent(entity, "Ping");
    fixture.tick();
    EXPECT_TRUE(Blueprints::getBool(entity, "Pinged"));
  }

  TEST(BlueprintScriptBridgeTest, SendEventTargetsOneEntityAndBroadcastReachesAll)
  {
    BridgeFixture fixture("hades-bridge-target");
    const std::string asset = fixture.write_blueprint("Receiver", make_receiver_blueprint());
    const Entity::EntityId first = fixture.spawn("First", asset);
    const Entity::EntityId second = fixture.spawn("Second", asset);
    ASSERT_TRUE(fixture.start());

    Blueprints::sendEvent(first, "Damaged", {3.0f});
    fixture.tick();
    EXPECT_FLOAT_EQ(Blueprints::getFloat(first, "Damage"), 3.0f);
    EXPECT_FLOAT_EQ(Blueprints::getFloat(second, "Damage"), 0.0f);

    Blueprints::broadcastEvent("Damaged", {9.0f});
    fixture.tick();
    EXPECT_FLOAT_EQ(Blueprints::getFloat(first, "Damage"), 9.0f);
    EXPECT_FLOAT_EQ(Blueprints::getFloat(second, "Damage"), 9.0f);
  }

  TEST(BlueprintScriptBridgeTest, UnknownEventNamesAreIgnored)
  {
    BridgeFixture fixture("hades-bridge-unknown-event");
    const std::string asset = fixture.write_blueprint("Receiver", make_receiver_blueprint());
    const Entity::EntityId entity = fixture.spawn("Target", asset);
    ASSERT_TRUE(fixture.start());

    Blueprints::sendEvent(entity, "NoSuchEvent", {1.0f});
    fixture.tick();

    EXPECT_FALSE(fixture.runtime.faulted());
    EXPECT_FLOAT_EQ(Blueprints::getFloat(entity, "Damage"), 0.0f);
  }

  TEST(BlueprintScriptBridgeTest, VariablesReadAndWriteThroughTheFacade)
  {
    BridgeFixture fixture("hades-bridge-variables");
    const std::string asset = fixture.write_blueprint("Receiver", make_receiver_blueprint());
    const Entity::EntityId entity = fixture.spawn("Target", asset);
    ASSERT_TRUE(fixture.start());

    EXPECT_TRUE(Blueprints::hasVariable(entity, "Damage"));
    EXPECT_FALSE(Blueprints::hasVariable(entity, "Nonexistent"));

    EXPECT_TRUE(Blueprints::setFloat(entity, "Damage", 4.5f));
    EXPECT_FLOAT_EQ(Blueprints::getFloat(entity, "Damage"), 4.5f);

    // Writes coerce to the declared type: Damage is a float, so an int lands
    // as one and reads back as such.
    EXPECT_TRUE(Blueprints::setInt(entity, "Damage", 7));
    EXPECT_EQ(Blueprints::getVariable(entity, "Damage").type(), ScriptValueType::Float);
    EXPECT_FLOAT_EQ(Blueprints::getFloat(entity, "Damage"), 7.0f);

    EXPECT_FALSE(Blueprints::setFloat(entity, "Nonexistent", 1.0f));
    EXPECT_TRUE(Blueprints::getVariable(entity, "Nonexistent").empty());
  }

  TEST(BlueprintScriptBridgeTest, StopDropsQueuedEventsButStartKeepsThem)
  {
    BridgeFixture fixture("hades-bridge-lifecycle");
    const std::string asset = fixture.write_blueprint("Receiver", make_receiver_blueprint());
    const Entity::EntityId entity = fixture.spawn("Target", asset);

    // Queued before start, the way a script's onStart does: scripts start
    // before Blueprints, so this must survive into the new session.
    Blueprints::sendEvent(entity, "Damaged", {11.0f});
    ASSERT_TRUE(fixture.start());
    fixture.tick();
    EXPECT_FLOAT_EQ(Blueprints::getFloat(entity, "Damage"), 11.0f);

    // Queued and then abandoned: stop() is the end of the conversation.
    Blueprints::sendEvent(entity, "Damaged", {99.0f});
    fixture.runtime.stop();
    EXPECT_FALSE(Blueprints::isRunning());

    ASSERT_TRUE(fixture.start());
    fixture.tick();
    EXPECT_FLOAT_EQ(Blueprints::getFloat(entity, "Damage"), 0.0f);
  }

  // ---------------------------------------------------------------------------
  // Blueprint -> script
  // ---------------------------------------------------------------------------

  namespace
  {
    /// BeginPlay -> Send Script Message("Hurt", 12) on the owning entity.
    Blueprint make_sender_blueprint()
    {
      Blueprint blueprint;
      blueprint.name = "Sender";

      GraphBuilder builder(blueprint, blueprint.eventGraph);
      const auto begin = builder.add("event.begin_play");
      const auto send = builder.add("script.send", "valueType", "float");
      builder.literal(send, "name", BlueprintValue::from_string("Hurt"));
      builder.literal(send, "value", BlueprintValue::from_float(12.0f));
      builder.exec(begin, "exec", send);

      return blueprint;
    }

    /// BeginPlay -> Call Script Function("GetHealth"), storing the reply in
    /// `Reply` and whether anyone answered in `Answered`.
    Blueprint make_caller_blueprint()
    {
      Blueprint blueprint;
      blueprint.name = "Caller";
      blueprint.variables.push_back(
          make_variable("Reply", ValueType::Float, BlueprintValue::from_float(0.0f)));
      blueprint.variables.push_back(
          make_variable("Answered", ValueType::Bool, BlueprintValue::from_bool(false)));

      GraphBuilder builder(blueprint, blueprint.eventGraph);
      const auto begin = builder.add("event.begin_play");
      const auto call = builder.add("script.call");
      blueprint.eventGraph.find_node(call)->config["valueType"] = "string";
      blueprint.eventGraph.find_node(call)->config["resultType"] = "float";
      builder.literal(call, "name", BlueprintValue::from_string("GetHealth"));
      builder.literal(call, "value", BlueprintValue::from_string("player"));

      const auto storeReply = builder.add("variable.set", "variable", "Reply");
      const auto storeAnswered = builder.add("variable.set", "variable", "Answered");

      builder.exec(begin, "exec", call);
      builder.exec(call, "then", storeReply);
      builder.exec(storeReply, "then", storeAnswered);
      builder.data(call, "result", storeReply, "value");
      builder.data(call, "handled", storeAnswered, "value");

      return blueprint;
    }
  }

  TEST(BlueprintScriptBridgeTest, SendScriptMessageReachesTheHost)
  {
    BridgeFixture fixture("hades-bridge-script-send");
    const std::string asset = fixture.write_blueprint("Sender", make_sender_blueprint());
    const Entity::EntityId entity = fixture.spawn("Sender", asset);
    ASSERT_TRUE(fixture.start());

    ASSERT_EQ(fixture.host.messages.size(), 1u);
    EXPECT_EQ(fixture.host.messages[0].entity, entity);
    EXPECT_EQ(fixture.host.messages[0].name, "Hurt");
    EXPECT_FLOAT_EQ(fixture.host.messages[0].value.asFloat(), 12.0f);
    EXPECT_EQ(fixture.host.messages[0].value.type(), ScriptValueType::Float);
  }

  TEST(BlueprintScriptBridgeTest, CallScriptFunctionReadsTheReplyBack)
  {
    BridgeFixture fixture("hades-bridge-script-call");
    fixture.host.reply = ScriptValue::fromFloat(87.5f);

    const std::string asset = fixture.write_blueprint("Caller", make_caller_blueprint());
    const Entity::EntityId entity = fixture.spawn("Caller", asset);
    ASSERT_TRUE(fixture.start());

    ASSERT_EQ(fixture.host.messages.size(), 1u);
    EXPECT_EQ(fixture.host.messages[0].name, "GetHealth");
    EXPECT_EQ(fixture.host.messages[0].value.asString(), "player");

    EXPECT_FLOAT_EQ(Blueprints::getFloat(entity, "Reply"), 87.5f);
    EXPECT_TRUE(Blueprints::getBool(entity, "Answered"));
  }

  TEST(BlueprintScriptBridgeTest, CallScriptFunctionReportsUnhandledMessages)
  {
    BridgeFixture fixture("hades-bridge-script-unhandled");
    // Default-constructed reply: no script answered.
    const std::string asset = fixture.write_blueprint("Caller", make_caller_blueprint());
    const Entity::EntityId entity = fixture.spawn("Caller", asset);
    ASSERT_TRUE(fixture.start());

    EXPECT_FALSE(Blueprints::getBool(entity, "Answered"));
    EXPECT_FLOAT_EQ(Blueprints::getFloat(entity, "Reply"), 0.0f);
    EXPECT_FALSE(fixture.runtime.faulted());
  }

  TEST(BlueprintScriptBridgeTest, ScriptNodesAreInertOnAHostThatDoesNotImplementThem)
  {
    // The base BlueprintHost drops script messages, which is what a graph
    // under unit test — or a headless run with no ScriptRuntime — gets.
    BridgeFixture fixture("hades-bridge-script-inert");
    RecordingHost plainHost;
    fixture.runtime.set_host(&plainHost);

    const std::string asset = fixture.write_blueprint("Caller", make_caller_blueprint());
    const Entity::EntityId entity = fixture.spawn("Caller", asset);
    ASSERT_TRUE(fixture.start());

    EXPECT_FALSE(Blueprints::getBool(entity, "Answered"));
    EXPECT_FALSE(fixture.runtime.faulted());
  }

  TEST(BlueprintScriptBridgeTest, BroadcastScriptMessageTargetsEveryEntity)
  {
    BridgeFixture fixture("hades-bridge-script-broadcast");

    Blueprint blueprint;
    blueprint.name = "Broadcaster";
    GraphBuilder builder(blueprint, blueprint.eventGraph);
    const auto begin = builder.add("event.begin_play");
    const auto broadcast = builder.add("script.broadcast", "valueType", "int");
    builder.literal(broadcast, "name", BlueprintValue::from_string("Reset"));
    builder.literal(broadcast, "value", BlueprintValue::from_int(3));
    builder.exec(begin, "exec", broadcast);

    const std::string asset = fixture.write_blueprint("Broadcaster", blueprint);
    fixture.spawn("Anyone", asset);
    ASSERT_TRUE(fixture.start());

    ASSERT_EQ(fixture.host.messages.size(), 1u);
    EXPECT_EQ(fixture.host.messages[0].entity, Entity::INVALID);
    EXPECT_EQ(fixture.host.messages[0].name, "Reset");
    EXPECT_EQ(fixture.host.messages[0].value.asInt(), 3);
  }

  // ---------------------------------------------------------------------------
  // Custom event parameters, in-graph
  // ---------------------------------------------------------------------------

  TEST(BlueprintScriptBridgeTest, CallEventPassesArgumentsToTheCustomEvent)
  {
    BridgeFixture fixture("hades-bridge-call-event");

    Blueprint blueprint = make_receiver_blueprint();
    GraphBuilder builder(blueprint, blueprint.eventGraph);
    const auto begin = builder.add("event.begin_play");
    const auto call = builder.add("flow.call_event", "name", "Damaged");
    builder.literal(call, "amount", BlueprintValue::from_float(42.0f));
    builder.exec(begin, "exec", call);

    const std::string asset = fixture.write_blueprint("Receiver", blueprint);
    const Entity::EntityId entity = fixture.spawn("Target", asset);
    ASSERT_TRUE(fixture.start());

    EXPECT_FLOAT_EQ(Blueprints::getFloat(entity, "Damage"), 42.0f);
  }


  // ---------------------------------------------------------------------------
  // End to end, through a real compiled script
  // ---------------------------------------------------------------------------
  //
  // The only test that proves the bridge survives the dylib boundary: the
  // script is compiled and dlopen'd for real, so `hades::Blueprints` and
  // `ScriptValue` have to resolve out of the host binary at load time. It is
  // skipped rather than failed when the toolchain is unavailable, matching how
  // ScriptRuntimeTest treats compilation.

  TEST(BlueprintScriptBridgeTest, RoundTripsThroughACompiledScript)
  {
    BridgeFixture fixture("hades-bridge-e2e");

    const std::filesystem::path scripts = fixture.root / "Scripts";
    std::filesystem::create_directories(scripts);
    {
      std::ofstream out(scripts / "Bridged.cpp");
      out << "#include \"engine/hades.hpp\"\n\n"
          << "class Bridged : public hades::HadesScript\n"
          << "{\n"
          << "public:\n"
          << "  void onStart(hades::ScriptContext &ctx) override\n"
          << "  {\n"
          << "    hades::Blueprints::sendEvent(ctx.entityId, \"Damaged\", {25.0f});\n"
          << "  }\n\n"
          << "  hades::ScriptValue onMessage(\n"
          << "      hades::ScriptContext &ctx, const std::string &name,\n"
          << "      const hades::ScriptValue &value) override\n"
          << "  {\n"
          << "    (void)ctx;\n"
          << "    (void)value;\n"
          << "    if (name == \"GetHealth\")\n"
          << "    {\n"
          << "      return hades::ScriptValue(87.5f);\n"
          << "    }\n"
          << "    return hades::ScriptValue();\n"
          << "  }\n"
          << "};\n\n"
          << "HADES_REGISTER_SCRIPT(Bridged)\n";
    }

    // One Blueprint doing both halves: it answers a script event, and it calls
    // into the script at BeginPlay.
    Blueprint blueprint = make_receiver_blueprint();
    blueprint.variables.push_back(
        make_variable("Reply", ValueType::Float, BlueprintValue::from_float(0.0f)));
    {
      GraphBuilder builder(blueprint, blueprint.eventGraph);
      const auto begin = builder.add("event.begin_play");
      const auto call = builder.add("script.call", "resultType", "float");
      builder.literal(call, "name", BlueprintValue::from_string("GetHealth"));
      const auto store = builder.add("variable.set", "variable", "Reply");
      builder.exec(begin, "exec", call);
      builder.exec(call, "then", store);
      builder.data(call, "result", store, "value");
    }

    const std::string asset = fixture.write_blueprint("Receiver", blueprint);
    const Entity::EntityId entity = fixture.spawn("Actor", asset);

    ScriptComponent scriptComponent;
    scriptComponent.attachments.push_back(ScriptAttachment{"Scripts/Bridged.cpp", "Bridged", true});
    fixture.componentManager.addComponent(entity, scriptComponent);

    ScriptRuntime scriptRuntime;
    std::string scriptError;
    if (!scriptRuntime.start(
            fixture.componentManager, fixture.entityManager, fixture.root, std::nullopt, &scriptError))
    {
      GTEST_SKIP() << "script toolchain unavailable: " << scriptError;
    }

    EngineBlueprintHost host;
    host.bind(&fixture.componentManager, nullptr, nullptr);
    host.set_script_runtime(&scriptRuntime);
    fixture.runtime.set_host(&host);

    ASSERT_TRUE(fixture.start());

    // Blueprint -> script: BeginPlay called onMessage and got an answer.
    EXPECT_FLOAT_EQ(Blueprints::getFloat(entity, "Reply"), 87.5f);

    // script -> Blueprint: the event queued from onStart, which ran before the
    // Blueprint runtime even existed, is delivered on the first update.
    EXPECT_FLOAT_EQ(Blueprints::getFloat(entity, "Damage"), 0.0f);
    fixture.tick();
    EXPECT_FLOAT_EQ(Blueprints::getFloat(entity, "Damage"), 25.0f);

    EXPECT_FALSE(fixture.runtime.faulted());
    fixture.runtime.stop();
    scriptRuntime.stop();
  }

  // ---------------------------------------------------------------------------
  // ScriptValue
  // ---------------------------------------------------------------------------

  TEST(ScriptValueTest, ConstructorsPickTheIntendedAlternative)
  {
    EXPECT_EQ(ScriptValue(true).type(), ScriptValueType::Bool);
    EXPECT_EQ(ScriptValue(3).type(), ScriptValueType::Int);
    EXPECT_EQ(ScriptValue(1.5f).type(), ScriptValueType::Float);
    EXPECT_EQ(ScriptValue(1.5).type(), ScriptValueType::Float);
    // A string literal must not collapse into `bool`.
    EXPECT_EQ(ScriptValue("hello").type(), ScriptValueType::String);
    EXPECT_EQ(ScriptValue(std::string("hello")).type(), ScriptValueType::String);
    EXPECT_EQ(ScriptValue(math::Vec3(1.0f, 2.0f, 3.0f)).type(), ScriptValueType::Vector);
    EXPECT_EQ(ScriptValue::fromEntity(7).type(), ScriptValueType::Entity);
    EXPECT_TRUE(ScriptValue().empty());
  }

  TEST(ScriptValueTest, RoundTripsThroughBlueprintValue)
  {
    const std::vector<ScriptValue> values = {
        ScriptValue(true),
        ScriptValue(42),
        ScriptValue(2.5f),
        ScriptValue("text"),
        ScriptValue(math::Vec3(1.0f, 2.0f, 3.0f)),
        ScriptValue::fromEntity(9),
    };

    for (const auto &value : values)
    {
      const ScriptValue round = to_script_value(to_blueprint_value(value));
      EXPECT_EQ(round.type(), value.type());
      EXPECT_EQ(round, value) << "round-tripping " << script_value_type_name(value.type());
    }

    EXPECT_TRUE(to_script_value(to_blueprint_value(ScriptValue())).empty());
  }

  TEST(ScriptValueTest, AccessorsConvertLeniently)
  {
    EXPECT_EQ(ScriptValue("12abc").asInt(), 12);
    EXPECT_FLOAT_EQ(ScriptValue("2.5").asFloat(), 2.5f);
    EXPECT_EQ(ScriptValue("nonsense").asInt(), 0);
    EXPECT_TRUE(ScriptValue(1).asBool());
    EXPECT_FALSE(ScriptValue("false").asBool());
    EXPECT_EQ(ScriptValue(2.0f).asVector().y, 2.0f);
    EXPECT_EQ(ScriptValue().asEntity(), Entity::INVALID);
  }
}
