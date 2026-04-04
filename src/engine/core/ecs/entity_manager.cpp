#include "entity_manager.hpp"

#include <algorithm>

namespace hades
{
  Entity::EntityId EntityManager::createEntity()
  {
    Entity::EntityId id;
    if (!availableEntities.empty())
    {
      id = availableEntities.front();
      availableEntities.pop();
    }
    else
    {
      id = activeEntities.size();
    }
    activeEntities.push_back(id);
    entityComponentSignatures[id] = {};

    return id;
  }

  void EntityManager::destroyEntity(Entity::EntityId entity)
  {
    const auto it = std::find(activeEntities.begin(), activeEntities.end(), entity);
    if (it == activeEntities.end())
    {
      return;
    }

    activeEntities.erase(it);
    availableEntities.push(entity);
    entityComponentSignatures.erase(entity);
  }

  void EntityManager::setComponentSignature(Entity::EntityId entity, std::bitset<MAX_COMPONENTS> signature)
  {
    entityComponentSignatures[entity] = signature;
  }

  const std::bitset<MAX_COMPONENTS> &EntityManager::getComponentSignature(Entity::EntityId entity) const
  {
    return entityComponentSignatures.at(entity);
  }

  std::vector<Entity::EntityId> EntityManager::getAllEntities()
  {
    return activeEntities;
  }

  const std::vector<Entity::EntityId> &EntityManager::getActiveEntities() const
  {
    return activeEntities;
  }

  void EntityManager::setComponentBit(Entity::EntityId entity, uint32_t bit, bool value)
  {
    auto it = entityComponentSignatures.find(entity);
    if (it != entityComponentSignatures.end())
    {
      it->second.set(bit, value);
    }
  }
}
