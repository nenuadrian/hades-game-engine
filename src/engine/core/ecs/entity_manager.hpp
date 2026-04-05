#ifndef ENTITY_MANAGER_H
#define ENTITY_MANAGER_H

#include "constants.h"
#include "entity.hpp"
#include <bitset>
#include <queue>
#include <vector>
#include <unordered_map>

namespace hades
{
  class EntityManager
  {
  private:
    std::vector<Entity::EntityId> activeEntities;
    std::queue<Entity::EntityId> availableEntities;
    std::unordered_map<Entity::EntityId, std::bitset<MAX_COMPONENTS>> entityComponentSignatures;

  public:
    Entity::EntityId createEntity();
    void destroyEntity(Entity::EntityId entity);
    void setComponentSignature(Entity::EntityId entity, std::bitset<MAX_COMPONENTS> signature);
    const std::bitset<MAX_COMPONENTS> &getComponentSignature(Entity::EntityId entity) const;
    const std::vector<Entity::EntityId> &getAllEntities() const;
    const std::vector<Entity::EntityId> &getActiveEntities() const;
    void setComponentBit(Entity::EntityId entity, uint32_t bit, bool value);
  };
}
#endif
