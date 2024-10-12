#ifndef COMPONENT_ARRAY_H
#define COMPONENT_ARRAY_H

#include "entity.hpp"
#include <unordered_map>
#include <vector>

template <typename T>
class ComponentArray
{
private:
  std::vector<T> components;
  std::unordered_map<Entity::Id, size_t> entityToIndex;
  std::unordered_map<size_t, Entity::Id> indexToEntity;

public:
  void insert(Entity::Id entity, T component)
  {
    entityToIndex[entity] = components.size();
    indexToEntity[components.size()] = entity;
    components.push_back(component);
  }

  void remove(Entity::Id entity)
  {
    size_t index = entityToIndex[entity];
    size_t lastIndex = components.size() - 1;

    // Move the last element to the removed position
    components[index] = components[lastIndex];
    Entity::Id lastEntity = indexToEntity[lastIndex];

    // Update maps
    entityToIndex[lastEntity] = index;
    indexToEntity[index] = lastEntity;

    entityToIndex.erase(entity);
    indexToEntity.erase(lastIndex);
    components.pop_back();
  }

  T &get(Entity::Id entity)
  {
    return components[entityToIndex[entity]];
  }

  bool has(Entity::Id entity)
  {
    return entityToIndex.find(entity) != entityToIndex.end();
  }
};

#endif
