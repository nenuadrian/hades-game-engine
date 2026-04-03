#include <gtest/gtest.h>

#include <algorithm>

#include "../engine/core/ecs/entity_manager.hpp"

namespace hades
{
  TEST(EntityManagerTest, DestroyEntityRemovesItFromActiveList)
  {
    EntityManager entityManager;

    const auto first = entityManager.createEntity();
    const auto second = entityManager.createEntity();
    const auto third = entityManager.createEntity();

    entityManager.destroyEntity(second);

    const auto remainingEntities = entityManager.getAllEntities();
    EXPECT_EQ(remainingEntities.size(), 2U);
    EXPECT_NE(std::find(remainingEntities.begin(), remainingEntities.end(), first), remainingEntities.end());
    EXPECT_EQ(std::find(remainingEntities.begin(), remainingEntities.end(), second), remainingEntities.end());
    EXPECT_NE(std::find(remainingEntities.begin(), remainingEntities.end(), third), remainingEntities.end());
  }
}
