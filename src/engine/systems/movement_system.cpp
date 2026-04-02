#include "movement_system.hpp"

#include "../components/position_component_3d.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"

namespace hades
{
  void MovementSystem::update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager)
  {
    (void)deltaTime;

    for (auto entity : entityManager.getAllEntities())
    {
      if (componentManager.hasComponent<PositionComponent3D>(entity))
      {
      }
    }
  }
}
