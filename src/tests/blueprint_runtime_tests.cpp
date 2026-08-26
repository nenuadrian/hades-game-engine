#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "blueprint_test_support.hpp"
#include "test_support.hpp"

#include "../engine/blueprint/blueprint_asset.hpp"
#include "../engine/blueprint/blueprint_runtime.hpp"
#include "../engine/components/blueprint_component.hpp"
#include "../engine/components/name_component.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/components/transform_hierarchy_component.hpp"
#include "../engine/components/world_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_factory.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/scene_serializer.hpp"
#include "editor/workspace_file_operations.hpp"

namespace hades
{
  using blueprint_test_support::GraphBuilder;
  using blueprint_test_support::RecordingHost;
  using test_support::ScopedDirectoryCleanup;
  using test_support::unique_test_directory;

  namespace
  {
    /// A workspace on disk holding one Blueprint asset, plus the ECS pair the
    /// runtime will drive.
    struct RuntimeFixture
    {
      explicit RuntimeFixture(const char *prefix)
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

      Entity::EntityId attach(
          Entity::EntityId entity,
          const std::string &assetPath,
          std::map<std::string, std::string> overrides = {})
      {
        BlueprintComponent component;
        BlueprintAttachment attachment;
        attachment.assetPath = assetPath;
        attachment.variableOverrides = std::move(overrides);
        component.attachments.push_back(std::move(attachment));
        componentManager.addComponent(entity, component);
        return entity;
      }

      std::filesystem::path root;
      ScopedDirectoryCleanup cleanup;
      EntityManager entityManager;
      ComponentManager componentManager{&entityManager};
      RecordingHost host;
      BlueprintRuntime runtime;
    };

    /// Moves the owning entity +1 on X every tick and prints its name once at
    /// BeginPlay.
    Blueprint make_mover_blueprint()
    {
      Blueprint blueprint;
      blueprint.name = "Mover";

      BlueprintVariable speed;
      speed.name = "Speed";
      speed.type = ValueType::Float;
      speed.defaultValue = BlueprintValue::from_float(1.0f);
      speed.exposed = true;
      blueprint.variables.push_back(speed);

      GraphBuilder builder(blueprint, blueprint.eventGraph);

      const auto begin = builder.add("event.begin_play");
      const auto name = builder.add("entity.get_name");
      const auto print = builder.add("debug.print");
      builder.exec(begin, "exec", print);
      builder.data(name, "name", print, "text");

      const auto tick = builder.add("event.tick");
      const auto offset = builder.add("transform.add_offset");
      const auto get = builder.add("variable.get", "variable", "Speed");
      const auto make = builder.add("vector.make");
      builder.exec(tick, "exec", offset);
      builder.data(get, "value", make, "x");
      builder.data(make, "vector", offset, "delta");

      return blueprint;
    }
  }

  TEST(BlueprintRuntimeTest, StartsInstancesFiresBeginPlayAndTicksThem)
  {
    RuntimeFixture fixture("hades-blueprint-runtime");
    const std::string asset = fixture.write_blueprint("Mover", make_mover_blueprint());

    const Entity::EntityId entity = fixture.entityManager.createEntity();
    fixture.componentManager.addComponent(entity, NameComponent{"Walker"});
    fixture.componentManager.addComponent(entity, PositionComponent3D(0.0f, 0.0f, 0.0f));
    fixture.attach(entity, asset);

    std::string error;
    ASSERT_TRUE(fixture.runtime.start(
        fixture.componentManager, fixture.entityManager, fixture.root, std::nullopt, &error))
        << error;

    EXPECT_TRUE(fixture.runtime.is_running());
    EXPECT_EQ(fixture.runtime.instance_count(), 1);
    EXPECT_EQ(fixture.host.joined(), "Walker");

    for (int i = 0; i < 3; ++i)
    {
      fixture.runtime.update(0.5f, fixture.componentManager, fixture.entityManager);
    }

    const auto &position = fixture.componentManager.getComponent<PositionComponent3D>(entity);
    EXPECT_FLOAT_EQ(position.x, 3.0f);
    EXPECT_FALSE(fixture.runtime.faulted());

    fixture.runtime.stop();
    EXPECT_FALSE(fixture.runtime.is_running());
    EXPECT_EQ(fixture.runtime.instance_count(), 0);
  }

  TEST(BlueprintRuntimeTest, PerEntityOverridesShadowTheAssetDefaults)
  {
    RuntimeFixture fixture("hades-blueprint-overrides");
    const std::string asset = fixture.write_blueprint("Mover", make_mover_blueprint());

    const Entity::EntityId slow = fixture.entityManager.createEntity();
    fixture.componentManager.addComponent(slow, NameComponent{"Slow"});
    fixture.componentManager.addComponent(slow, PositionComponent3D(0.0f, 0.0f, 0.0f));
    fixture.attach(slow, asset);

    const Entity::EntityId fast = fixture.entityManager.createEntity();
    fixture.componentManager.addComponent(fast, NameComponent{"Fast"});
    fixture.componentManager.addComponent(fast, PositionComponent3D(0.0f, 0.0f, 0.0f));
    fixture.attach(fast, asset, {{"Speed", "10"}});

    std::string error;
    ASSERT_TRUE(fixture.runtime.start(
        fixture.componentManager, fixture.entityManager, fixture.root, std::nullopt, &error))
        << error;
    EXPECT_EQ(fixture.runtime.instance_count(), 2);

    fixture.runtime.update(1.0f, fixture.componentManager, fixture.entityManager);

    EXPECT_FLOAT_EQ(fixture.componentManager.getComponent<PositionComponent3D>(slow).x, 1.0f);
    EXPECT_FLOAT_EQ(fixture.componentManager.getComponent<PositionComponent3D>(fast).x, 10.0f);
  }

  TEST(BlueprintRuntimeTest, DisabledAttachmentsAndMissingAssetsAreHandled)
  {
    RuntimeFixture fixture("hades-blueprint-disabled");
    const std::string asset = fixture.write_blueprint("Mover", make_mover_blueprint());

    const Entity::EntityId entity = fixture.entityManager.createEntity();
    fixture.componentManager.addComponent(entity, NameComponent{"Idle"});
    fixture.componentManager.addComponent(entity, PositionComponent3D(0.0f, 0.0f, 0.0f));

    BlueprintComponent component;
    BlueprintAttachment disabled;
    disabled.assetPath = asset;
    disabled.enabled = false;
    component.attachments.push_back(disabled);
    fixture.componentManager.addComponent(entity, component);

    std::string error;
    ASSERT_TRUE(fixture.runtime.start(
        fixture.componentManager, fixture.entityManager, fixture.root, std::nullopt, &error))
        << error;
    EXPECT_EQ(fixture.runtime.instance_count(), 0);
    fixture.runtime.stop();

    // A path that does not resolve is a loud failure, not a silent skip.
    fixture.componentManager.getComponent<BlueprintComponent>(entity).attachments[0].enabled = true;
    fixture.componentManager.getComponent<BlueprintComponent>(entity).attachments[0].assetPath =
        "Blueprints/Missing.hbp";

    error.clear();
    EXPECT_FALSE(fixture.runtime.start(
        fixture.componentManager, fixture.entityManager, fixture.root, std::nullopt, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(fixture.runtime.is_running());
  }

  TEST(BlueprintRuntimeTest, GraphsThatFailToCompileStopPlayModeWithTheirErrors)
  {
    RuntimeFixture fixture("hades-blueprint-broken");

    Blueprint broken;
    broken.name = "Broken";
    GraphBuilder builder(broken, broken.eventGraph);
    const auto begin = builder.add("event.begin_play");
    const auto getter = builder.add("variable.get", "variable", "DoesNotExist");
    const auto print = builder.add("debug.print");
    builder.exec(begin, "exec", print);
    builder.data(getter, "value", print, "text");

    const std::string asset = fixture.write_blueprint("Broken", broken);

    const Entity::EntityId entity = fixture.entityManager.createEntity();
    fixture.componentManager.addComponent(entity, NameComponent{"Subject"});
    fixture.attach(entity, asset);

    std::string error;
    EXPECT_FALSE(fixture.runtime.start(
        fixture.componentManager, fixture.entityManager, fixture.root, std::nullopt, &error));
    EXPECT_NE(error.find("unknown variable"), std::string::npos);
  }

  TEST(BlueprintRuntimeTest, InputAndCollisionEventsReachTheRightInstances)
  {
    RuntimeFixture fixture("hades-blueprint-events");

    Blueprint blueprint;
    blueprint.name = "Listener";
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto keyDown = builder.add("event.key_down");
    const auto keyPrint = builder.add("debug.print");
    builder.exec(keyDown, "exec", keyPrint);
    builder.data(keyDown, "keyCode", keyPrint, "text");

    const auto collision = builder.add("event.collision_begin");
    const auto otherName = builder.add("entity.get_name");
    const auto collisionPrint = builder.add("debug.print");
    builder.exec(collision, "exec", collisionPrint);
    builder.data(collision, "other", otherName, "target");
    builder.data(otherName, "name", collisionPrint, "text");

    const std::string asset = fixture.write_blueprint("Listener", blueprint);

    const Entity::EntityId listener = fixture.entityManager.createEntity();
    fixture.componentManager.addComponent(listener, NameComponent{"Listener"});
    fixture.attach(listener, asset);

    const Entity::EntityId other = fixture.entityManager.createEntity();
    fixture.componentManager.addComponent(other, NameComponent{"Wall"});

    std::string error;
    ASSERT_TRUE(fixture.runtime.start(
        fixture.componentManager, fixture.entityManager, fixture.root, std::nullopt, &error))
        << error;

    fixture.runtime.on_key_down(32);
    EXPECT_EQ(fixture.host.joined(), "32");

    // The payload is the *other* entity, whichever side of the pair we are.
    fixture.runtime.on_collision_begin(other, listener);
    EXPECT_EQ(fixture.host.joined(), "32|Wall");
  }

  TEST(BlueprintRuntimeTest, WorldFilteringLimitsInstancesToTheActiveWorld)
  {
    RuntimeFixture fixture("hades-blueprint-worlds");
    const std::string asset = fixture.write_blueprint("Mover", make_mover_blueprint());

    const auto worldA = EntityFactory::createWorld(fixture.entityManager, fixture.componentManager, "A", true);
    const auto worldB = EntityFactory::createWorld(fixture.entityManager, fixture.componentManager, "B", false);

    const auto inA = EntityFactory::createCube(fixture.entityManager, fixture.componentManager, worldA);
    const auto inB = EntityFactory::createCube(fixture.entityManager, fixture.componentManager, worldB);
    fixture.attach(inA, asset);
    fixture.attach(inB, asset);

    std::string error;
    ASSERT_TRUE(fixture.runtime.start(
        fixture.componentManager, fixture.entityManager, fixture.root, worldA, &error))
        << error;

    EXPECT_EQ(fixture.runtime.instance_count(), 1);
    const auto views = fixture.runtime.instances();
    ASSERT_EQ(views.size(), 1U);
    EXPECT_EQ(views.front().entity, inA);

    fixture.runtime.update(1.0f, fixture.componentManager, fixture.entityManager);
    EXPECT_FLOAT_EQ(fixture.componentManager.getComponent<PositionComponent3D>(inA).x, 1.0f);
    EXPECT_FLOAT_EQ(fixture.componentManager.getComponent<PositionComponent3D>(inB).x, 0.0f);
  }

  TEST(BlueprintRuntimeTest, DelayedChainsResumeAcrossFrames)
  {
    RuntimeFixture fixture("hades-blueprint-latent");

    Blueprint blueprint;
    blueprint.name = "Waiter";
    GraphBuilder builder(blueprint, blueprint.eventGraph);

    const auto begin = builder.add("event.begin_play");
    const auto delay = builder.add("flow.delay");
    const auto print = builder.add("debug.print");
    builder.exec(begin, "exec", delay);
    builder.exec(delay, "completed", print);
    builder.literal(delay, "duration", BlueprintValue::from_float(0.3f));
    builder.literal(print, "text", BlueprintValue::from_string("late"));

    // A Tick handler is required for update() to drive the instance forward.
    const auto tick = builder.add("event.tick");
    const auto noop = builder.add("flow.stop");
    builder.exec(tick, "exec", noop);

    const std::string asset = fixture.write_blueprint("Waiter", blueprint);

    const Entity::EntityId entity = fixture.entityManager.createEntity();
    fixture.componentManager.addComponent(entity, NameComponent{"Waiter"});
    fixture.attach(entity, asset);

    std::string error;
    ASSERT_TRUE(fixture.runtime.start(
        fixture.componentManager, fixture.entityManager, fixture.root, std::nullopt, &error))
        << error;
    EXPECT_TRUE(fixture.host.lines.empty());

    fixture.runtime.update(0.1f, fixture.componentManager, fixture.entityManager);
    EXPECT_TRUE(fixture.host.lines.empty());

    fixture.runtime.update(0.25f, fixture.componentManager, fixture.entityManager);
    EXPECT_EQ(fixture.host.joined(), "late");
  }

  TEST(BlueprintRuntimeTest, ComponentSurvivesASceneRoundTrip)
  {
    const auto testRoot = unique_test_directory("hades-blueprint-scene");
    ScopedDirectoryCleanup cleanup(testRoot);
    std::filesystem::create_directories(testRoot);

    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);

    const auto world = EntityFactory::createWorld(entityManager, componentManager, "Main", true);
    const auto entity = EntityFactory::createCube(entityManager, componentManager, world);

    BlueprintComponent component;
    BlueprintAttachment attachment;
    attachment.assetPath = "Blueprints/Patrol.hbp";
    attachment.enabled = false;
    attachment.variableOverrides["Speed"] = "2.5";
    attachment.variableOverrides["Home"] = "1,2,3";
    component.attachments.push_back(attachment);
    componentManager.addComponent(entity, component);

    std::string error;
    ASSERT_TRUE(save_all_worlds(testRoot, entityManager, componentManager, &error)) << error;

    EntityManager loadedEntities;
    ComponentManager loadedComponents(&loadedEntities);
    const auto worlds = load_all_worlds(testRoot, loadedEntities, loadedComponents, &error);
    ASSERT_EQ(worlds.size(), 1U);

    bool found = false;
    for (Entity::EntityId candidate : loadedEntities.getActiveEntities())
    {
      if (!loadedComponents.hasComponent<BlueprintComponent>(candidate))
      {
        continue;
      }

      const auto &restored = loadedComponents.getComponent<BlueprintComponent>(candidate);
      ASSERT_EQ(restored.attachments.size(), 1U);
      EXPECT_EQ(restored.attachments[0].assetPath, "Blueprints/Patrol.hbp");
      EXPECT_FALSE(restored.attachments[0].enabled);
      EXPECT_EQ(restored.attachments[0].variableOverrides.at("Speed"), "2.5");
      EXPECT_EQ(restored.attachments[0].variableOverrides.at("Home"), "1,2,3");
      found = true;
    }

    EXPECT_TRUE(found) << "the BlueprintComponent did not survive the round trip";
  }

  TEST(BlueprintRuntimeTest, DeletingAnAssetDetachesItFromEveryEntity)
  {
    const auto testRoot = unique_test_directory("hades-blueprint-delete");
    ScopedDirectoryCleanup cleanup(testRoot);
    std::filesystem::create_directories(testRoot);

    const std::string keptRelative = "Blueprints/Kept.hbp";
    const std::string doomedRelative = "Blueprints/Doomed.hbp";

    std::string error;
    ASSERT_TRUE(save_blueprint(testRoot / keptRelative, make_starter_blueprint("Kept"), &error)) << error;
    ASSERT_TRUE(save_blueprint(testRoot / doomedRelative, make_starter_blueprint("Doomed"), &error)) << error;

    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);

    const Entity::EntityId entity = entityManager.createEntity();
    BlueprintComponent component;
    BlueprintAttachment kept;
    kept.assetPath = keptRelative;
    BlueprintAttachment doomed;
    doomed.assetPath = doomedRelative;
    component.attachments.push_back(kept);
    component.attachments.push_back(doomed);
    componentManager.addComponent(entity, component);

    WorkspaceDeleteResult result;
    ASSERT_TRUE(delete_workspace_item(
        testRoot, testRoot / doomedRelative, entityManager, componentManager, &result, &error))
        << error;

    EXPECT_EQ(result.removedBlueprintAssignments, 1U);
    EXPECT_EQ(result.affectedBlueprintComponents, 1U);

    const auto &remaining = componentManager.getComponent<BlueprintComponent>(entity);
    ASSERT_EQ(remaining.attachments.size(), 1U);
    EXPECT_EQ(remaining.attachments[0].assetPath, keptRelative);
  }

  TEST(BlueprintRuntimeTest, StartFiresBeginPlayOncePerCall)
  {
    // start() is "begin play": it re-instantiates and fires BeginPlay again.
    // Calling it twice therefore applies every BeginPlay side effect twice, so
    // hosts must start the runtime exactly once per play session.
    RuntimeFixture fixture("hades-blueprint-restart");

    Blueprint blueprint;
    blueprint.name = "Nudge";
    GraphBuilder builder(blueprint, blueprint.eventGraph);
    const auto begin = builder.add("event.begin_play");
    const auto offset = builder.add("transform.add_offset");
    builder.exec(begin, "exec", offset);
    builder.literal(offset, "delta", BlueprintValue::from_vector(math::Vec3(0.0f, 10.0f, 0.0f)));

    const std::string asset = fixture.write_blueprint("Nudge", blueprint);

    const Entity::EntityId entity = fixture.entityManager.createEntity();
    fixture.componentManager.addComponent(entity, NameComponent{"Nudged"});
    fixture.componentManager.addComponent(entity, PositionComponent3D(0.0f, 0.0f, 0.0f));
    fixture.attach(entity, asset);

    std::string error;
    ASSERT_TRUE(fixture.runtime.start(
        fixture.componentManager, fixture.entityManager, fixture.root, std::nullopt, &error))
        << error;
    EXPECT_FLOAT_EQ(fixture.componentManager.getComponent<PositionComponent3D>(entity).y, 10.0f);

    ASSERT_TRUE(fixture.runtime.start(
        fixture.componentManager, fixture.entityManager, fixture.root, std::nullopt, &error))
        << error;
    EXPECT_FLOAT_EQ(fixture.componentManager.getComponent<PositionComponent3D>(entity).y, 20.0f);
    EXPECT_EQ(fixture.runtime.instance_count(), 1);
  }

  TEST(BlueprintRuntimeTest, ABeginPlayFaultLeavesTheRuntimeStopped)
  {
    RuntimeFixture fixture("hades-blueprint-begin-fault");

    // An unbounded loop on BeginPlay trips the VM's execution budget.
    Blueprint blueprint;
    blueprint.name = "Runaway";
    GraphBuilder builder(blueprint, blueprint.eventGraph);
    const auto begin = builder.add("event.begin_play");
    const auto loop = builder.add("flow.while_loop");
    const auto body = builder.add("flow.stop");
    builder.exec(begin, "exec", loop);
    builder.exec(loop, "loopBody", body);
    builder.literal(loop, "condition", BlueprintValue::from_bool(true));

    const std::string asset = fixture.write_blueprint("Runaway", blueprint);

    const Entity::EntityId entity = fixture.entityManager.createEntity();
    fixture.componentManager.addComponent(entity, NameComponent{"Runaway"});
    fixture.attach(entity, asset);

    std::string error;
    EXPECT_FALSE(fixture.runtime.start(
        fixture.componentManager, fixture.entityManager, fixture.root, std::nullopt, &error));
    EXPECT_NE(error.find("budget exhausted"), std::string::npos);

    // A failed start must not leave the runtime claiming to be live.
    EXPECT_FALSE(fixture.runtime.is_running());
    EXPECT_EQ(fixture.runtime.instance_count(), 0);
    EXPECT_TRUE(fixture.runtime.faulted());
    EXPECT_FALSE(fixture.runtime.last_error().empty());
  }

  TEST(BlueprintRuntimeTest, InstancesDoNotResumeOnARecycledEntityId)
  {
    RuntimeFixture fixture("hades-blueprint-recycled");
    const std::string asset = fixture.write_blueprint("Mover", make_mover_blueprint());

    const Entity::EntityId entity = fixture.entityManager.createEntity();
    fixture.componentManager.addComponent(entity, NameComponent{"Doomed"});
    fixture.componentManager.addComponent(entity, PositionComponent3D(0.0f, 0.0f, 0.0f));
    fixture.attach(entity, asset);

    std::string error;
    ASSERT_TRUE(fixture.runtime.start(
        fixture.componentManager, fixture.entityManager, fixture.root, std::nullopt, &error))
        << error;

    // Destroy the owner the way the Destroy Entity node does.
    fixture.componentManager.removeAllComponents(entity);
    fixture.entityManager.destroyEntity(entity);
    fixture.runtime.update(1.0f, fixture.componentManager, fixture.entityManager);

    // EntityManager recycles ids, so a fresh entity can land on the same slot.
    const Entity::EntityId recycled = fixture.entityManager.createEntity();
    ASSERT_EQ(recycled, entity) << "test assumes the id is recycled";
    fixture.componentManager.addComponent(recycled, NameComponent{"Newcomer"});
    fixture.componentManager.addComponent(recycled, PositionComponent3D(0.0f, 0.0f, 0.0f));
    BlueprintComponent component;
    component.attachments.push_back(BlueprintAttachment{});
    fixture.componentManager.addComponent(recycled, component);

    fixture.runtime.update(1.0f, fixture.componentManager, fixture.entityManager);

    // The retired instance must not adopt the newcomer and start moving it.
    EXPECT_FLOAT_EQ(fixture.componentManager.getComponent<PositionComponent3D>(recycled).x, 0.0f);
  }
}
