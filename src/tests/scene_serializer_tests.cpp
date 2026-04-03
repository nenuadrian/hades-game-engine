#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

#include "test_support.hpp"

#include "../engine/components/name_component.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/components/primitive_component.hpp"
#include "../engine/components/transform_hierarchy_component.hpp"
#include "../engine/components/world_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_factory.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/scene_serializer.hpp"
#include "../engine/core/ecs/world_utils.hpp"

namespace hades
{
  using test_support::ScopedDirectoryCleanup;
  using test_support::unique_test_directory;

  TEST(SceneSerializerTest, SaveAllWorldsAndLoadAllWorldsRoundTripHierarchy)
  {
    const std::filesystem::path testRoot = unique_test_directory("hades-scene-roundtrip");
    ScopedDirectoryCleanup cleanup(testRoot);

    const std::filesystem::path workspaceRoot = testRoot / "Workspace";
    std::filesystem::create_directories(workspaceRoot);

    EntityManager entityManager;
    ComponentManager componentManager;

    const auto world = EntityFactory::createWorld(entityManager, componentManager, "Playable World", true);
    const auto cube = EntityFactory::createCube(entityManager, componentManager, world);
    const auto text = EntityFactory::createText(entityManager, componentManager, world);

    componentManager.getComponent<NameComponent>(cube).value = "Hero Cube";
    componentManager.getComponent<NameComponent>(text).value = "Scene Text";

    auto &cubePosition = componentManager.getComponent<PositionComponent3D>(cube);
    cubePosition.x = 1.25f;
    cubePosition.y = -3.5f;
    cubePosition.z = 8.0f;

    std::string errorMessage;
    ASSERT_TRUE(save_all_worlds(workspaceRoot, entityManager, componentManager, &errorMessage)) << errorMessage;

    const auto savedWorlds = list_saved_worlds(workspaceRoot);
    ASSERT_EQ(savedWorlds.size(), 1U);
    EXPECT_EQ(savedWorlds.front(), "Playable World");

    EntityManager loadedEntityManager;
    ComponentManager loadedComponentManager;
    const auto loadedWorlds = load_all_worlds(workspaceRoot, loadedEntityManager, loadedComponentManager, &errorMessage);

    EXPECT_TRUE(errorMessage.empty()) << errorMessage;
    ASSERT_EQ(loadedWorlds.size(), 1U);

    const auto loadedWorld = loadedWorlds.front();
    ASSERT_TRUE(loadedComponentManager.hasComponent<WorldComponent>(loadedWorld));
    EXPECT_EQ(loadedComponentManager.getComponent<NameComponent>(loadedWorld).value, "Playable World");

    const auto loadedEntities = loadedEntityManager.getAllEntities();
    ASSERT_EQ(loadedEntities.size(), 3U);

    std::optional<Entity::EntityId> loadedCube;
    std::optional<Entity::EntityId> loadedText;
    for (Entity::EntityId entity : loadedEntities)
    {
      if (!loadedComponentManager.hasComponent<NameComponent>(entity))
      {
        continue;
      }

      const auto &name = loadedComponentManager.getComponent<NameComponent>(entity).value;
      if (name == "Hero Cube")
      {
        loadedCube = entity;
      }
      else if (name == "Scene Text")
      {
        loadedText = entity;
      }
    }

    ASSERT_TRUE(loadedCube.has_value());
    ASSERT_TRUE(loadedText.has_value());
    EXPECT_TRUE(loadedComponentManager.hasComponent<PrimitiveComponent>(*loadedCube));
    EXPECT_EQ(world_for_entity(*loadedCube, loadedComponentManager), loadedWorld);

    const auto &loadedCubePosition = loadedComponentManager.getComponent<PositionComponent3D>(*loadedCube);
    EXPECT_FLOAT_EQ(loadedCubePosition.x, 1.25f);
    EXPECT_FLOAT_EQ(loadedCubePosition.y, -3.5f);
    EXPECT_FLOAT_EQ(loadedCubePosition.z, 8.0f);

    const auto &worldHierarchy = loadedComponentManager.getComponent<TransformHierarchyComponent>(loadedWorld);
    EXPECT_NE(std::find(worldHierarchy.children.begin(), worldHierarchy.children.end(), *loadedCube), worldHierarchy.children.end());
    EXPECT_NE(std::find(worldHierarchy.children.begin(), worldHierarchy.children.end(), *loadedText), worldHierarchy.children.end());
  }

  TEST(SceneSerializerTest, SaveAllWorldsRemovesStaleWorldFiles)
  {
    const std::filesystem::path testRoot = unique_test_directory("hades-scene-stale");
    ScopedDirectoryCleanup cleanup(testRoot);

    const std::filesystem::path workspaceRoot = testRoot / "Workspace";
    std::filesystem::create_directories(workspaceRoot);

    EntityManager entityManager;
    ComponentManager componentManager;
    EntityFactory::createWorld(entityManager, componentManager, "Temporary World", true);

    std::string errorMessage;
    ASSERT_TRUE(save_all_worlds(workspaceRoot, entityManager, componentManager, &errorMessage)) << errorMessage;
    ASSERT_EQ(list_saved_worlds(workspaceRoot).size(), 1U);

    EntityManager emptyEntityManager;
    ComponentManager emptyComponentManager;
    ASSERT_TRUE(save_all_worlds(workspaceRoot, emptyEntityManager, emptyComponentManager, &errorMessage)) << errorMessage;
    EXPECT_TRUE(list_saved_worlds(workspaceRoot).empty());
  }
}
