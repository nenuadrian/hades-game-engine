#ifndef HADES_ENGINE_CORE_ECS_SYSTEM_MANAGER_HPP
#define HADES_ENGINE_CORE_ECS_SYSTEM_MANAGER_HPP

#include "system.hpp"
#include "type_id.hpp"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace hades
{
  class ComponentManager;
  class EntityManager;

  enum class SystemPhase : uint8_t
  {
    PrePhysics,
    Physics,
    PostPhysics,
    Logic,
    PreRender,
    Render,
    PostRender,
    Audio
  };

  class SystemManager
  {
  private:
    struct SystemEntry
    {
      ComponentId typeId;
      SystemPhase phase;
      int priority;
      std::shared_ptr<System> system;

      bool operator<(const SystemEntry &other) const
      {
        if (phase != other.phase)
        {
          return static_cast<uint8_t>(phase) < static_cast<uint8_t>(other.phase);
        }
        return priority < other.priority;
      }
    };

    std::vector<SystemEntry> systems_;

  public:
    template <typename T>
    std::shared_ptr<T> registerSystem(SystemPhase phase = SystemPhase::Logic, int priority = 0)
    {
      auto system = std::make_shared<T>();
      systems_.push_back({ComponentTypeId::get<T>(), phase, priority, system});
      std::stable_sort(systems_.begin(), systems_.end());
      return system;
    }

    void updateSystems(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager);
    void updateSystems(float deltaTime, SystemContext &context);
  };
}

#endif
