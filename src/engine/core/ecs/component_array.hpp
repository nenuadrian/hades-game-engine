#ifndef COMPONENT_ARRAY_H
#define COMPONENT_ARRAY_H

#include "entity.hpp"
#include <unordered_map>
#include <vector>

namespace hades
{
  class IComponentArray
  {
  public:
    virtual ~IComponentArray() = default;
    virtual bool has(Entity::EntityId entity) = 0;
    virtual void removeIfPresent(Entity::EntityId entity) = 0;
  };

  template <typename T>
  class ComponentArray : public IComponentArray
  {
  private:
    std::vector<T> components;
    std::unordered_map<Entity::EntityId, size_t> entityToIndex;
    std::unordered_map<size_t, Entity::EntityId> indexToEntity;

  public:
    void insert(Entity::EntityId entity, const T &component)
    {
      if (entityToIndex.find(entity) != entityToIndex.end())
      {
        components[entityToIndex[entity]] = component;
        return;
      }

      entityToIndex[entity] = components.size();
      indexToEntity[components.size()] = entity;
      components.push_back(component);
    }

    void remove(Entity::EntityId entity)
    {
      const auto entityIt = entityToIndex.find(entity);
      if (entityIt == entityToIndex.end())
      {
        // Removing a component the entity never had is a no-op. Without this
        // guard the swap-remove below would clobber the element at index 0 and
        // underflow the size on an empty array.
        return;
      }

      size_t index = entityIt->second;
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

    void removeIfPresent(Entity::EntityId entity) override
    {
      if (entityToIndex.find(entity) != entityToIndex.end())
      {
        remove(entity);
      }
    }

    /// Throws std::out_of_range when the entity has no component of this type.
    /// Callers are expected to gate on has() (or a signature query) first;
    /// reading through operator[] here would insert a phantom index entry and
    /// alias another entity's storage.
    T &get(Entity::EntityId entity)
    {
      return components[entityToIndex.at(entity)];
    }

    bool has(Entity::EntityId entity) override
    {
      return entityToIndex.find(entity) != entityToIndex.end();
    }
  };
}

#endif
