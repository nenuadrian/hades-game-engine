#include "system.hpp"
#include "component_manager.hpp"
#include "entity_manager.hpp"
#include <memory>

namespace hades
{
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
