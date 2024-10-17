#ifndef COMPONENT_ARRAY_H
#define COMPONENT_ARRAY_H

#include "entity.hpp"
#include <unordered_map>
#include <vector>

namespace hades
{
  template <typename T>
  class ComponentArray
  {
  private:
    std::vector<T> components;
    std::unordered_map<Entity::EntityId, size_t> entityToIndex;
    std::unordered_map<size_t, Entity::EntityId> indexToEntity;

  public:
    void insert(Entity::EntityId entity, T component)
    {
      entityToIndex[entity] = components.size();
      indexToEntity[components.size()] = entity;
      components.push_back(component);
    }

    void remove(Entity::EntityId entity)
    {
      size_t index = entityToIndex[entity];
      size_t lastIndex = components.size() - 1;

      // Move the last element to the removed position
      components[index] = components[lastIndex];
      Entity::EntityId lastEntity = indexToEntity[lastIndex];

      // Update maps
      entityToIndex[lastEntity] = index;
      indexToEntity[index] = lastEntity;

      entityToIndex.erase(entity);
      indexToEntity.erase(lastIndex);
      components.pop_back();
    }

    T &get(Entity::EntityId entity)
    {
      return components[entityToIndex[entity]];
    }

    bool has(Entity::EntityId entity)
    {
      return entityToIndex.find(entity) != entityToIndex.end();
    }
  };
}

#endif
