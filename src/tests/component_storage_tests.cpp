#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/query.hpp"

namespace hades
{
  namespace
  {
    // Test-local component types, so these tests never depend on the shape of
    // a real gameplay component.
    struct Marker
    {
      int value = 0;
    };

    struct OtherMarker
    {
      int value = 0;
    };
  }

  TEST(ComponentArrayTest, GetOnAMissingEntityThrowsInsteadOfAliasingAnotherEntity)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);

    const auto owner = entityManager.createEntity();
    const auto stranger = entityManager.createEntity();
    componentManager.addComponent(owner, Marker{100});

    EXPECT_THROW((void)componentManager.getComponent<Marker>(stranger), std::out_of_range);

    // The failed lookup must not leave a phantom entry behind: before this was
    // guarded, one stray read made hasComponent() report true forever and
    // handed back the owner's storage.
    EXPECT_FALSE(componentManager.hasComponent<Marker>(stranger));
    EXPECT_EQ(componentManager.getComponent<Marker>(owner).value, 100);
  }

  TEST(ComponentArrayTest, GetOnAnEmptyArrayThrows)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);

    const auto entity = entityManager.createEntity();

    EXPECT_THROW((void)componentManager.getComponent<Marker>(entity), std::out_of_range);
    EXPECT_FALSE(componentManager.hasComponent<Marker>(entity));
  }

  TEST(ComponentArrayTest, RemoveOnAMissingEntityLeavesOtherComponentsIntact)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);

    const auto first = entityManager.createEntity();
    const auto second = entityManager.createEntity();
    const auto stranger = entityManager.createEntity();
    componentManager.addComponent(first, Marker{1});
    componentManager.addComponent(second, Marker{2});

    componentManager.removeComponent<Marker>(stranger);

    // The swap-remove used to run unconditionally, overwriting slot 0 with the
    // last element and orphaning whoever lived there.
    EXPECT_TRUE(componentManager.hasComponent<Marker>(first));
    EXPECT_TRUE(componentManager.hasComponent<Marker>(second));
    EXPECT_EQ(componentManager.getComponent<Marker>(first).value, 1);
    EXPECT_EQ(componentManager.getComponent<Marker>(second).value, 2);
    EXPECT_FALSE(componentManager.hasComponent<Marker>(stranger));
  }

  TEST(ComponentArrayTest, RemoveOnAnEmptyArrayDoesNotUnderflow)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);

    const auto entity = entityManager.createEntity();

    // components.size() - 1 on an empty vector wraps to SIZE_MAX, so this used
    // to index far out of bounds.
    EXPECT_NO_THROW(componentManager.removeComponent<Marker>(entity));
    EXPECT_FALSE(componentManager.hasComponent<Marker>(entity));
  }

  TEST(ComponentArrayTest, SwapRemoveKeepsEveryRemainingEntityAddressable)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);

    std::vector<Entity::EntityId> entities;
    for (int index = 0; index < 6; ++index)
    {
      const auto entity = entityManager.createEntity();
      componentManager.addComponent(entity, Marker{index});
      entities.push_back(entity);
    }

    // Remove from the front, the middle and the back to exercise every branch
    // of the swap-remove.
    componentManager.removeComponent<Marker>(entities[0]);
    componentManager.removeComponent<Marker>(entities[3]);
    componentManager.removeComponent<Marker>(entities[5]);

    for (int index : {1, 2, 4})
    {
      const auto entity = entities[static_cast<std::size_t>(index)];
      ASSERT_TRUE(componentManager.hasComponent<Marker>(entity)) << "index " << index;
      EXPECT_EQ(componentManager.getComponent<Marker>(entity).value, index);
    }

    for (int index : {0, 3, 5})
    {
      EXPECT_FALSE(componentManager.hasComponent<Marker>(entities[static_cast<std::size_t>(index)]));
    }
  }

  TEST(ComponentArrayTest, InsertingTwiceOverwritesInPlace)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);

    const auto entity = entityManager.createEntity();
    const auto neighbour = entityManager.createEntity();
    componentManager.addComponent(entity, Marker{1});
    componentManager.addComponent(neighbour, Marker{2});

    componentManager.addComponent(entity, Marker{42});

    EXPECT_EQ(componentManager.getComponent<Marker>(entity).value, 42);
    EXPECT_EQ(componentManager.getComponent<Marker>(neighbour).value, 2);
  }

  TEST(ComponentManagerTest, HasComponentAgreesWithTheSignatureQuery)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);

    const auto tagged = entityManager.createEntity();
    const auto untagged = entityManager.createEntity();
    componentManager.addComponent(tagged, Marker{});

    // hasComponent() reads the component array while query() reads the entity
    // signature bitset. The two must never disagree, or a query hands back an
    // entity whose components cannot be read.
    const auto matches = query<Marker>(entityManager);
    EXPECT_NE(std::find(matches.begin(), matches.end(), tagged), matches.end());
    EXPECT_EQ(std::find(matches.begin(), matches.end(), untagged), matches.end());

    componentManager.removeComponent<Marker>(tagged);

    const auto afterRemoval = query<Marker>(entityManager);
    EXPECT_FALSE(componentManager.hasComponent<Marker>(tagged));
    EXPECT_EQ(std::find(afterRemoval.begin(), afterRemoval.end(), tagged), afterRemoval.end());
  }

  TEST(ComponentManagerTest, RemoveAllComponentsClearsStorageAndSignature)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);

    const auto entity = entityManager.createEntity();
    componentManager.addComponent(entity, Marker{1});
    componentManager.addComponent(entity, OtherMarker{2});

    componentManager.removeAllComponents(entity);

    EXPECT_FALSE(componentManager.hasComponent<Marker>(entity));
    EXPECT_FALSE(componentManager.hasComponent<OtherMarker>(entity));
    EXPECT_TRUE(entityManager.getComponentSignature(entity).none());
    EXPECT_TRUE(query<Marker>(entityManager).empty());
  }

  TEST(EntityLifecycleTest, ARecycledEntityIdInheritsNothingFromItsPredecessor)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);

    const auto original = entityManager.createEntity();
    componentManager.addComponent(original, Marker{7});
    componentManager.addComponent(original, OtherMarker{8});

    componentManager.removeAllComponents(original);
    entityManager.destroyEntity(original);

    const auto recycled = entityManager.createEntity();
    ASSERT_EQ(recycled, original) << "test assumes IDs are reused";

    EXPECT_FALSE(componentManager.hasComponent<Marker>(recycled));
    EXPECT_FALSE(componentManager.hasComponent<OtherMarker>(recycled));
    EXPECT_TRUE(entityManager.getComponentSignature(recycled).none());
  }
}
