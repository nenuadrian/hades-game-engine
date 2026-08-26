#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "../engine/components/animation_component.hpp"
#include "../engine/components/animator_component.hpp"
#include "../engine/components/blueprint_component.hpp"
#include "../engine/components/camera_component.hpp"
#include "../engine/components/collider_component.hpp"
#include "../engine/components/light_component.hpp"
#include "../engine/components/mesh_renderer_component.hpp"
#include "../engine/components/model_component.hpp"
#include "../engine/components/name_component.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/components/rigid_body_component.hpp"
#include "../engine/components/rotation_component_3d.hpp"
#include "../engine/components/scale_component_3d.hpp"
#include "../engine/components/script_component.hpp"
#include "../engine/components/text_component.hpp"
#include "../engine/components/transform_hierarchy_component.hpp"
#include "../engine/components/ui_canvas_component.hpp"
#include "../engine/components/world_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/component_registry.hpp"
#include "../engine/core/ecs/entity_factory.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/scene_serializer.hpp"
#include "../engine/core/ecs/world_utils.hpp"

using json = nlohmann::json;

namespace hades
{
  namespace
  {
    /// Component json keys touched by build_populated_scene(). Kept as data so
    /// the coverage canary below can compare it against the live registry.
    const std::set<std::string> &fixture_component_keys()
    {
      static const std::set<std::string> keys{
          "name",
          "world",
          "hierarchy",
          "position3d",
          "rotation3d",
          "scale3d",
          "camera",
          "primitive",
          "text",
          "audioListener",
          "audioSource",
          "script",
          "light",
          "rigidBody",
          "collider",
          "meshRenderer",
          "model",
          "animation",
          "animator",
          "blueprint",
          "uiCanvas",
      };
      return keys;
    }

    /// A world holding one entity per interesting component combination, every
    /// field set away from its default so a dropped field is visible.
    Entity::EntityId build_populated_scene(
        EntityManager &entityManager,
        ComponentManager &componentManager)
    {
      register_builtin_components();

      const auto world = EntityFactory::createWorld(entityManager, componentManager, "SnapshotWorld", true);

      const auto camera = EntityFactory::createCamera(entityManager, componentManager, world);
      componentManager.getComponent<NameComponent>(camera).value = "Camera";
      componentManager.getComponent<PositionComponent3D>(camera) = PositionComponent3D{1.5f, -2.25f, 3.75f};
      componentManager.getComponent<CameraComponent>(camera).fovY = 71.5f;
      componentManager.getComponent<CameraComponent>(camera).nearClip = 0.25f;
      componentManager.getComponent<CameraComponent>(camera).farClip = 512.0f;

      const auto text = EntityFactory::createText(entityManager, componentManager, camera);
      componentManager.getComponent<NameComponent>(text).value = "Text";
      auto &textComponent = componentManager.getComponent<TextComponent>(text);
      textComponent.content = "round trip";
      textComponent.fontSize = 2.5f;
      textComponent.wrapWidth = 9.5f;
      textComponent.lineSpacing = 1.75f;
      textComponent.yawDegrees = 15.0f;
      textComponent.pitchDegrees = -30.0f;
      textComponent.rollDegrees = 45.0f;

      const auto physicsCube = EntityFactory::createPhysicsCube(entityManager, componentManager, world);
      componentManager.getComponent<NameComponent>(physicsCube).value = "PhysicsCube";
      componentManager.addComponent(physicsCube, RotationComponent3D{0.1f, 0.2f, 0.3f, 0.927f});
      componentManager.addComponent(physicsCube, ScaleComponent3D{2.0f, 3.0f, 4.0f});

      const auto spotLight = EntityFactory::createSpotLight(entityManager, componentManager, world);
      componentManager.getComponent<NameComponent>(spotLight).value = "SpotLight";
      auto &light = componentManager.getComponent<LightComponent>(spotLight);
      light.colorR = 0.2f;
      light.colorG = 0.4f;
      light.colorB = 0.6f;
      light.intensity = 3.5f;
      light.range = 22.5f;
      light.innerConeAngle = 12.0f;
      light.outerConeAngle = 48.0f;
      light.castShadows = true;
      light.enabled = false;

      const auto scripted = EntityFactory::createCube(entityManager, componentManager, world);
      componentManager.getComponent<NameComponent>(scripted).value = "Scripted";
      ScriptComponent script;
      ScriptAttachment attachment;
      attachment.scriptPath = "scripts/player.py";
      attachment.className = "Player";
      attachment.enabled = false;
      attachment.publicFieldValues["speed"] = "12.5";
      attachment.publicFieldValues["label"] = "hero";
      script.attachments.push_back(attachment);
      componentManager.addComponent(scripted, script);
      componentManager.addComponent(scripted, MeshRendererComponent{});

      const auto blueprinted = EntityFactory::createCube(entityManager, componentManager, world);
      componentManager.getComponent<NameComponent>(blueprinted).value = "Blueprinted";
      BlueprintComponent blueprintComponent;
      BlueprintAttachment blueprintAttachment;
      blueprintAttachment.assetPath = "Blueprints/Patrol.hbp";
      blueprintAttachment.enabled = false;
      blueprintAttachment.variableOverrides["Speed"] = "3.25";
      blueprintAttachment.variableOverrides["Waypoint"] = "1,2,3";
      blueprintComponent.attachments.push_back(blueprintAttachment);
      componentManager.addComponent(blueprinted, blueprintComponent);

      const auto model = EntityFactory::createCube(entityManager, componentManager, world);
      componentManager.getComponent<NameComponent>(model).value = "Model";
      componentManager.removeComponent<PrimitiveComponent>(model);
      ModelComponent modelComponent;
      modelComponent.assetPath = "models/hero.glb";
      componentManager.addComponent(model, modelComponent);
      componentManager.addComponent(model, AnimationComponent{2, false, false, 0.5f, 1.25f});

      AnimatorComponent animatorComponent;
      animatorComponent.graphPath = "locomotion";
      animatorComponent.defaultClip = "idle";
      animatorComponent.playOnStart = false;
      animatorComponent.looping = false;
      animatorComponent.speed = 1.75f;
      animatorComponent.defaultBlendSeconds = 0.4f;
      animatorComponent.parameters.push_back(AnimatorParamOverride{"speed", "float", 3.5f, 0, false});
      animatorComponent.parameters.push_back(AnimatorParamOverride{"grounded", "bool", 0.0f, 0, true});
      componentManager.addComponent(model, animatorComponent);

      const auto audio = EntityFactory::createAudioEmitter(entityManager, componentManager, model);
      componentManager.getComponent<NameComponent>(audio).value = "AudioEmitter";

      UICanvasComponent uiCanvas;
      uiCanvas.space = UICanvasSpace::World;
      uiCanvas.visible = false;
      uiCanvas.referenceWidth = 640.0f;
      uiCanvas.referenceHeight = 360.0f;
      uiCanvas.worldWidth = 3.5f;
      uiCanvas.offsetX = 0.25f;
      uiCanvas.offsetY = 2.5f;
      uiCanvas.offsetZ = -0.75f;
      uiCanvas.billboard = false;
      uiCanvas.maxDistance = 40.0f;
      uiCanvas.fadeDistance = 5.0f;
      uiCanvas.sortOrder = 3;
      UIWidget bar;
      bar.id = "healthBar";
      bar.type = "bar";
      bar.visible = false;
      bar.anchorX = 0.25f;
      bar.anchorY = 0.75f;
      bar.offsetX = 4.0f;
      bar.offsetY = -6.0f;
      bar.width = 220.0f;
      bar.height = 18.0f;
      bar.colorR = 0.11f;
      bar.colorG = 0.12f;
      bar.colorB = 0.13f;
      bar.colorA = 0.9f;
      bar.fillColorR = 0.85f;
      bar.fillColorG = 0.2f;
      bar.fillColorB = 0.25f;
      bar.fillColorA = 0.95f;
      bar.text = "unused-but-round-tripped";
      bar.textSize = 22.0f;
      bar.value = 0.42f;
      bar.bindVariable = "health";
      bar.onClickEvent = "BarClicked";
      UIWidget label;
      label.id = "nameLabel";
      label.type = "text";
      label.text = "GOBLIN";
      label.textSize = 24.0f;
      label.anchorY = 0.0f;
      bar.children.push_back(label);
      uiCanvas.widgets.push_back(bar);
      componentManager.addComponent(model, uiCanvas);

      return world;
    }

    /// Component payloads keyed by entity name, with the hierarchy component
    /// dropped because its values are entity IDs that legitimately change.
    std::map<std::string, json> components_by_name(const json &snapshot)
    {
      std::map<std::string, json> byName;
      for (const auto &world : snapshot)
      {
        for (const auto &entity : world.at("entities"))
        {
          json components = entity.at("components");
          const std::string name =
              components.contains("name") ? components["name"].at("value").get<std::string>() : std::string("<unnamed>");
          components.erase("hierarchy");
          byName[name] = components;
        }
      }
      return byName;
    }

    std::string name_of(Entity::EntityId entity, ComponentManager &componentManager)
    {
      return componentManager.hasComponent<NameComponent>(entity)
                 ? componentManager.getComponent<NameComponent>(entity).value
                 : std::string("<unnamed>");
    }

    std::optional<Entity::EntityId> find_by_name(
        const std::string &name,
        EntityManager &entityManager,
        ComponentManager &componentManager)
    {
      for (Entity::EntityId entity : entityManager.getActiveEntities())
      {
        if (name_of(entity, componentManager) == name)
        {
          return entity;
        }
      }
      return std::nullopt;
    }
  }

  TEST(PlayModeSnapshotTest, RoundTripPreservesEveryComponentAndField)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);
    build_populated_scene(entityManager, componentManager);

    const json before = snapshot_all_worlds(entityManager, componentManager);
    restore_all_worlds_from_snapshot(before, entityManager, componentManager);
    const json after = snapshot_all_worlds(entityManager, componentManager);

    // Stopping play mode restores this snapshot over the live scene, so any
    // component or field that does not survive is silent data loss.
    EXPECT_EQ(components_by_name(before), components_by_name(after));
  }

  TEST(PlayModeSnapshotTest, RoundTripCoversEveryComponentTypeInTheFixture)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);
    build_populated_scene(entityManager, componentManager);

    std::set<std::string> serializedKeys;
    for (const auto &[name, components] : components_by_name(snapshot_all_worlds(entityManager, componentManager)))
    {
      (void)name;
      for (const auto &entry : components.items())
      {
        serializedKeys.insert(entry.key());
      }
    }
    serializedKeys.insert("hierarchy"); // stripped by components_by_name.

    EXPECT_EQ(serializedKeys, fixture_component_keys())
        << "the fixture no longer writes what it claims to cover";
  }

  TEST(PlayModeSnapshotTest, RestoreRemapsHierarchyLinksOntoTheNewEntityIds)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);
    build_populated_scene(entityManager, componentManager);

    const json snapshot = snapshot_all_worlds(entityManager, componentManager);
    restore_all_worlds_from_snapshot(snapshot, entityManager, componentManager);

    const auto world = find_by_name("SnapshotWorld", entityManager, componentManager);
    const auto camera = find_by_name("Camera", entityManager, componentManager);
    const auto text = find_by_name("Text", entityManager, componentManager);
    const auto model = find_by_name("Model", entityManager, componentManager);
    const auto audio = find_by_name("AudioEmitter", entityManager, componentManager);
    ASSERT_TRUE(world.has_value() && camera.has_value() && text.has_value());
    ASSERT_TRUE(model.has_value() && audio.has_value());

    // Nesting must survive, including the two-deep branches.
    EXPECT_EQ(componentManager.getComponent<TransformHierarchyComponent>(*camera).parent.value(), *world);
    EXPECT_EQ(componentManager.getComponent<TransformHierarchyComponent>(*text).parent.value(), *camera);
    EXPECT_EQ(componentManager.getComponent<TransformHierarchyComponent>(*audio).parent.value(), *model);

    const auto &cameraChildren = componentManager.getComponent<TransformHierarchyComponent>(*camera).children;
    EXPECT_EQ(cameraChildren, std::vector<Entity::EntityId>{*text});

    // Every restored link must point at a live entity, not a stale ID.
    const auto &active = entityManager.getActiveEntities();
    for (Entity::EntityId entity : active)
    {
      if (!componentManager.hasComponent<TransformHierarchyComponent>(entity))
      {
        continue;
      }
      const auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);
      if (hierarchy.parent.has_value())
      {
        EXPECT_NE(std::find(active.begin(), active.end(), *hierarchy.parent), active.end())
            << "dangling parent on entity " << entity;
      }
      for (Entity::EntityId child : hierarchy.children)
      {
        EXPECT_NE(std::find(active.begin(), active.end(), child), active.end())
            << "dangling child on entity " << entity;
      }
      EXPECT_EQ(world_for_entity(entity, componentManager).value_or(Entity::INVALID), *world);
    }
  }

  TEST(PlayModeSnapshotTest, RestoreReplacesTheLiveSceneRatherThanDuplicatingIt)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);
    build_populated_scene(entityManager, componentManager);

    const std::size_t originalCount = entityManager.getActiveEntities().size();
    const json snapshot = snapshot_all_worlds(entityManager, componentManager);

    // Mutate the live scene the way play mode would, then restore twice: the
    // entity count must not creep upward on repeated stop/start cycles.
    const auto strayCube = EntityFactory::createCube(
        entityManager, componentManager, find_by_name("SnapshotWorld", entityManager, componentManager));
    componentManager.getComponent<NameComponent>(strayCube).value = "SpawnedDuringPlay";

    restore_all_worlds_from_snapshot(snapshot, entityManager, componentManager);
    EXPECT_EQ(entityManager.getActiveEntities().size(), originalCount);
    EXPECT_FALSE(find_by_name("SpawnedDuringPlay", entityManager, componentManager).has_value());

    restore_all_worlds_from_snapshot(snapshot, entityManager, componentManager);
    EXPECT_EQ(entityManager.getActiveEntities().size(), originalCount);
    EXPECT_EQ(find_world_entities(entityManager, componentManager).size(), 1U);
  }

  TEST(PlayModeSnapshotTest, RestoreReportsTheOldToNewIdMapping)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);
    const auto originalWorld = build_populated_scene(entityManager, componentManager);

    const json snapshot = snapshot_all_worlds(entityManager, componentManager);

    std::unordered_map<Entity::EntityId, Entity::EntityId> idMap;
    restore_all_worlds_from_snapshot(snapshot, entityManager, componentManager, &idMap);

    // The editor uses this map to re-point its selection after play mode, so a
    // missing entry silently drops the user's selection.
    ASSERT_TRUE(idMap.count(originalWorld) == 1);
    const auto restoredWorld = idMap.at(originalWorld);
    EXPECT_TRUE(componentManager.hasComponent<WorldComponent>(restoredWorld));
    EXPECT_EQ(name_of(restoredWorld, componentManager), "SnapshotWorld");

    const auto &active = entityManager.getActiveEntities();
    EXPECT_EQ(idMap.size(), active.size());
    for (const auto &[oldId, newId] : idMap)
    {
      (void)oldId;
      EXPECT_NE(std::find(active.begin(), active.end(), newId), active.end());
    }
  }

  TEST(ComponentRegistryTest, RegistrationsHaveUniqueKeysAndCallableHooks)
  {
    register_builtin_components();

    const auto &registrations = ComponentRegistry::instance().all();
    ASSERT_FALSE(registrations.empty());

    std::set<std::string> keys;
    std::set<ComponentId> typeIds;
    for (const auto &registration : registrations)
    {
      EXPECT_FALSE(registration.jsonKey.empty());
      EXPECT_TRUE(static_cast<bool>(registration.serialize)) << registration.jsonKey;
      EXPECT_TRUE(static_cast<bool>(registration.deserialize)) << registration.jsonKey;
      EXPECT_TRUE(keys.insert(registration.jsonKey).second)
          << "duplicate json key: " << registration.jsonKey;
      EXPECT_TRUE(typeIds.insert(registration.typeId).second)
          << "component registered twice: " << registration.jsonKey;
    }
  }

  TEST(ComponentRegistryTest, EveryRegisteredComponentIsExercisedByTheSnapshotFixture)
  {
    register_builtin_components();

    std::set<std::string> registered;
    for (const auto &registration : ComponentRegistry::instance().all())
    {
      registered.insert(registration.jsonKey);
    }

    // Canary: when a component gains a serializer it must also be added to
    // build_populated_scene(), or the round-trip test silently stops covering it.
    EXPECT_EQ(registered, fixture_component_keys());
  }
}
