#ifndef HADES_ENGINE_CORE_ECS_SYSTEM_MANAGER_HPP
#define HADES_ENGINE_CORE_ECS_SYSTEM_MANAGER_HPP

#include "system.hpp"
#include <memory>
#include <typeinfo>
#include <unordered_map>

namespace hades
{
  class ComponentManager;
  class EntityManager;

  class SystemManager
  {
  private:
    std::unordered_map<const char *, std::shared_ptr<System>> systems;

  public:
    template <typename T>
    std::shared_ptr<T> registerSystem()
    {
      const char *typeName = typeid(T).name();

      auto system = std::make_shared<T>();
      systems[typeName] = system;
      return system;
    }

    void updateSystems(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager)
    {
      for (auto &system : systems)
      {
        system.second->update(deltaTime, componentManager, entityManager);
      }
    }
  };
}

#endif
