#include "entity_manager.hpp"

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

    return id;
  }

  void EntityManager::destroyEntity(Entity::EntityId entity)
  {
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
}
