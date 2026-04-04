#include "system_manager.hpp"

namespace hades
{
  void SystemManager::updateSystems(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager)
  {
    for (auto &entry : systems_)
    {
      entry.system->update(deltaTime, componentManager, entityManager);
    }
  }

  void SystemManager::updateSystems(float deltaTime, SystemContext &context)
  {
    for (auto &entry : systems_)
    {
      entry.system->update(deltaTime, context);
    }
  }
}
