#ifndef ENTITY_MANAGER_H
#define ENTITY_MANAGER_H

#include "constants.h"
#include "entity.hpp"
#include <bitset>
#include <queue>
#include <vector>
#include <unordered_map>

class EntityManager
{
private:
  std::vector<Entity::EntityId> activeEntities;
  std::queue<Entity::EntityId> availableEntities;
  std::unordered_map<Entity::EntityId, std::bitset<MAX_COMPONENTS>> entityComponentSignatures;

public:
  Entity::EntityId createEntity()
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

  void destroyEntity(Entity::EntityId entity)
  {
    availableEntities.push(entity);
    entityComponentSignatures.erase(entity);
  }

  void setComponentSignature(Entity::EntityId entity, std::bitset<MAX_COMPONENTS> signature)
  {
    entityComponentSignatures[entity] = signature;
  }

  const std::bitset<MAX_COMPONENTS> &getComponentSignature(Entity::EntityId entity) const
  {
    return entityComponentSignatures.at(entity);
  }

  std::vector<Entity::EntityId> getAllEntities()
  {
    return activeEntities;
  }
};

#endif
