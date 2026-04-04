#ifndef SYSTEM_H
#define SYSTEM_H

#include "system_context.hpp"

namespace hades
{
  class ComponentManager;
  class EntityManager;

  class System
  {
  public:
    virtual ~System() = default;

    /// Legacy update interface. Existing systems override this.
    virtual void update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager) = 0;

    /// New update interface with full system context (includes EventBus).
    /// Default implementation delegates to the legacy 3-param overload.
    /// Override this in systems that need event bus access.
    virtual void update(float deltaTime, SystemContext &context)
    {
      update(deltaTime, context.componentManager, context.entityManager);
    }
  };
}

#endif
