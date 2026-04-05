#ifndef COMPONENT_MANAGER_H
#define COMPONENT_MANAGER_H

#include "component_array.hpp"
#include "entity.hpp"
#include "entity_manager.hpp"
#include "type_id.hpp"
#include <memory>
#include <unordered_map>

namespace hades
{
  class ComponentManager
  {
  private:
    std::unordered_map<ComponentId, std::shared_ptr<IComponentArray>> componentArrays;
    EntityManager *entityManager_ = nullptr;

  public:
    ComponentManager() = default;
    explicit ComponentManager(EntityManager *entityManager) : entityManager_(entityManager) {}

    template <typename T>
    std::shared_ptr<ComponentArray<T>> getComponentArray()
    {
      ComponentId typeId = ComponentTypeId::get<T>();

      if (componentArrays.find(typeId) == componentArrays.end())
      {
        componentArrays[typeId] = std::make_shared<ComponentArray<T>>();
      }

      return std::static_pointer_cast<ComponentArray<T>>(componentArrays[typeId]);
    }

    template <typename T>
    void addComponent(Entity::EntityId entity, T component)
    {
      getComponentArray<T>()->insert(entity, component);
      if (entityManager_ != nullptr)
      {
        entityManager_->setComponentBit(entity, ComponentTypeId::get<T>(), true);
      }
    }

    template <typename T>
    void removeComponent(Entity::EntityId entity)
    {
      getComponentArray<T>()->remove(entity);
      if (entityManager_ != nullptr)
      {
        entityManager_->setComponentBit(entity, ComponentTypeId::get<T>(), false);
      }
    }

    template <typename T>
    T &getComponent(Entity::EntityId entity)
    {
      return getComponentArray<T>()->get(entity);
    }

    template <typename T>
    bool hasComponent(Entity::EntityId entity)
    {
      return getComponentArray<T>()->has(entity);
    }

    void removeAllComponents(Entity::EntityId entity)
    {
      for (auto &[typeId, array] : componentArrays)
      {
        array->removeIfPresent(entity);
      }
      if (entityManager_ != nullptr)
      {
        entityManager_->setComponentSignature(entity, {});
      }
    }
  };
}
#endif
