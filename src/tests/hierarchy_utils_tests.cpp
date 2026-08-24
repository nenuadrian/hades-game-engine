#include <gtest/gtest.h>

#include <vector>

#include "../engine/components/transform_hierarchy_component.hpp"
#include "../engine/components/world_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_factory.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/hierarchy_utils.hpp"
#include "../engine/core/ecs/world_utils.hpp"

namespace hades
{
  namespace
  {
    std::vector<Entity::EntityId> children_of(
        Entity::EntityId entity,
        ComponentManager &componentManager)
    {
      return componentManager.getComponent<TransformHierarchyComponent>(entity).children;
    }
  }

  TEST(HierarchyUtilsTest, ReparentMovesEntityBetweenParents)
  {
    EntityManager entityManager;
    ComponentManager componentManager;

    const auto world = EntityFactory::createWorld(entityManager, componentManager, "World1", true);
    const auto first = EntityFactory::createCube(entityManager, componentManager, world);
    const auto second = EntityFactory::createCube(entityManager, componentManager, world);

    ASSERT_TRUE(reparent_entity(second, first, componentManager));

    EXPECT_EQ(children_of(world, componentManager), std::vector<Entity::EntityId>{first});
    EXPECT_EQ(children_of(first, componentManager), std::vector<Entity::EntityId>{second});
    EXPECT_EQ(componentManager.getComponent<TransformHierarchyComponent>(second).parent.value(), first);
  }

  TEST(HierarchyUtilsTest, ReparentRefusesSelfAndDescendants)
  {
    EntityManager entityManager;
    ComponentManager componentManager;

    const auto world = EntityFactory::createWorld(entityManager, componentManager, "World1", true);
    const auto parent = EntityFactory::createCube(entityManager, componentManager, world);
    const auto child = EntityFactory::createCube(entityManager, componentManager, parent);
    const auto grandChild = EntityFactory::createCube(entityManager, componentManager, child);

    EXPECT_FALSE(reparent_entity(parent, parent, componentManager));
    EXPECT_FALSE(reparent_entity(parent, child, componentManager));
    EXPECT_FALSE(reparent_entity(parent, grandChild, componentManager));

    // The rejected moves must leave the hierarchy exactly as it was.
    EXPECT_EQ(componentManager.getComponent<TransformHierarchyComponent>(parent).parent.value(), world);
    EXPECT_EQ(children_of(world, componentManager), std::vector<Entity::EntityId>{parent});
    EXPECT_EQ(children_of(parent, componentManager), std::vector<Entity::EntityId>{child});
  }

  TEST(HierarchyUtilsTest, ReparentInsertsAtRequestedIndex)
  {
    EntityManager entityManager;
    ComponentManager componentManager;

    const auto world = EntityFactory::createWorld(entityManager, componentManager, "World1", true);
    const auto host = EntityFactory::createCube(entityManager, componentManager, world);
    const auto first = EntityFactory::createCube(entityManager, componentManager, host);
    const auto second = EntityFactory::createCube(entityManager, componentManager, host);
    const auto moved = EntityFactory::createCube(entityManager, componentManager, world);

    ASSERT_TRUE(reparent_entity(moved, host, componentManager, 1));

    const std::vector<Entity::EntityId> expected{first, moved, second};
    EXPECT_EQ(children_of(host, componentManager), expected);
  }

  TEST(HierarchyUtilsTest, ReorderWithinSameParentAccountsForTheDetachedSlot)
  {
    EntityManager entityManager;
    ComponentManager componentManager;

    const auto world = EntityFactory::createWorld(entityManager, componentManager, "World1", true);
    const auto first = EntityFactory::createCube(entityManager, componentManager, world);
    const auto second = EntityFactory::createCube(entityManager, componentManager, world);
    const auto third = EntityFactory::createCube(entityManager, componentManager, world);

    // Drop "first" onto the gap below "third", i.e. before index 3.
    ASSERT_TRUE(reparent_entity(first, world, componentManager, 3));

    const std::vector<Entity::EntityId> movedDown{second, third, first};
    EXPECT_EQ(children_of(world, componentManager), movedDown);

    // Drop it back onto the gap above "second", i.e. before index 0.
    ASSERT_TRUE(reparent_entity(first, world, componentManager, 0));

    const std::vector<Entity::EntityId> movedUp{first, second, third};
    EXPECT_EQ(children_of(world, componentManager), movedUp);
  }

  TEST(HierarchyUtilsTest, ReparentAppendsWhenIndexIsOutOfRange)
  {
    EntityManager entityManager;
    ComponentManager componentManager;

    const auto world = EntityFactory::createWorld(entityManager, componentManager, "World1", true);
    const auto host = EntityFactory::createCube(entityManager, componentManager, world);
    const auto existing = EntityFactory::createCube(entityManager, componentManager, host);
    const auto moved = EntityFactory::createCube(entityManager, componentManager, world);

    ASSERT_TRUE(reparent_entity(moved, host, componentManager, 99));

    const std::vector<Entity::EntityId> expected{existing, moved};
    EXPECT_EQ(children_of(host, componentManager), expected);
  }

  TEST(HierarchyUtilsTest, ReparentRefreshesTheCachedWorldOfTheWholeSubtree)
  {
    EntityManager entityManager;
    ComponentManager componentManager;

    const auto firstWorld = EntityFactory::createWorld(entityManager, componentManager, "World1", true);
    const auto secondWorld = EntityFactory::createWorld(entityManager, componentManager, "World2", false);
    const auto parent = EntityFactory::createCube(entityManager, componentManager, firstWorld);
    const auto child = EntityFactory::createCube(entityManager, componentManager, parent);

    // Prime the caches so a stale entry would be observable.
    ASSERT_EQ(world_for_entity(child, componentManager).value(), firstWorld);

    ASSERT_TRUE(reparent_entity(parent, secondWorld, componentManager));

    EXPECT_EQ(world_for_entity(parent, componentManager).value(), secondWorld);
    EXPECT_EQ(world_for_entity(child, componentManager).value(), secondWorld);
  }

  TEST(HierarchyUtilsTest, ChildIndexInParentReportsTheSiblingSlot)
  {
    EntityManager entityManager;
    ComponentManager componentManager;

    const auto world = EntityFactory::createWorld(entityManager, componentManager, "World1", true);
    const auto first = EntityFactory::createCube(entityManager, componentManager, world);
    const auto second = EntityFactory::createCube(entityManager, componentManager, world);

    EXPECT_EQ(child_index_in_parent(first, componentManager).value(), 0);
    EXPECT_EQ(child_index_in_parent(second, componentManager).value(), 1);
    EXPECT_FALSE(child_index_in_parent(world, componentManager).has_value());
  }
}
