#ifndef HADES_ENGINE_CORE_ECS_SYSTEM_CONTEXT_HPP
#define HADES_ENGINE_CORE_ECS_SYSTEM_CONTEXT_HPP

namespace hades
{
  class ComponentManager;
  class EntityManager;
  class EventBus;

  struct SystemContext
  {
    ComponentManager &componentManager;
    EntityManager &entityManager;
    EventBus &eventBus;
  };
}

#endif
