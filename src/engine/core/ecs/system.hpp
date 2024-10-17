#ifndef SYSTEM_H
#define SYSTEM_H

#include "entity.hpp"
#include "component_manager.hpp"
#include "entity_manager.hpp"

#include <vector>

class System
{
public:
  virtual void update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager) = 0;
};

#endif
