#ifndef SYSTEM_H
#define SYSTEM_H

namespace hades
{
  class ComponentManager;
  class EntityManager;

  class System
  {
  public:
    virtual void update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager) = 0;
  };
}

#endif
