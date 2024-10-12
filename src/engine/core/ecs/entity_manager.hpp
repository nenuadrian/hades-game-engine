#include "constants.h"
#include "entity.hpp"
#include <bitset>
#include <queue>
#include <vector>
#include <unordered_map>

class EntityManager
{
private:
  std::vector<Entity::Id> activeEntities;
  std::queue<Entity::Id> availableEntities;
  std::unordered_map<Entity::Id, std::bitset<MAX_COMPONENTS>> entityComponentSignatures;

public:
  Entity::Id createEntity()
  {
    Entity::Id id;
    if (!availableEntities.empty())
    {
      id = availableEntities.front();
      availableEntities.pop();
    }
    else
    {
      id = activeEntities.size();
      activeEntities.push_back(id);
    }
    return id;
  }

  void destroyEntity(Entity::Id entity)
  {
    availableEntities.push(entity);
    entityComponentSignatures.erase(entity);
  }

  void setComponentSignature(Entity::Id entity, std::bitset<MAX_COMPONENTS> signature)
  {
    entityComponentSignatures[entity] = signature;
  }

  const std::bitset<MAX_COMPONENTS> &getComponentSignature(Entity::Id entity) const
  {
    return entityComponentSignatures.at(entity);
  }
};
