#include "system_manager.hpp"

namespace hades
{
  void SystemManager::updateSystems(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager)
  {
    for (auto &system : systems)
    {
      system.second->update(deltaTime, componentManager, entityManager);
    }
  }
}
