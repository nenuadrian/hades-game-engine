#ifndef COMPONENT_MANAGER_H
#define COMPONENT_MANAGER_H

#include "entity.hpp"
#include "component_array.hpp"
#include <memory>
#include <unordered_map>

class ComponentManager
{
private:
  std::unordered_map<const char *, std::shared_ptr<void>> componentArrays;

public:
  template <typename T>
  std::shared_ptr<ComponentArray<T>> getComponentArray()
  {
    const char *typeName = typeid(T).name();

    if (componentArrays.find(typeName) == componentArrays.end())
    {
      componentArrays[typeName] = std::make_shared<ComponentArray<T>>();
    }

    return std::static_pointer_cast<ComponentArray<T>>(componentArrays[typeName]);
  }

  template <typename T>
  void addComponent(Entity::Id entity, T component)
  {
    getComponentArray<T>()->insert(entity, component);
  }

  template <typename T>
  void removeComponent(Entity::Id entity)
  {
    getComponentArray<T>()->remove(entity);
  }

  template <typename T>
  T &getComponent(Entity::Id entity)
  {
    return getComponentArray<T>()->get(entity);
  }

  template <typename T>
  bool hasComponent(Entity::Id entity)
  {
    return getComponentArray<T>()->has(entity);
  }
};

#endif
