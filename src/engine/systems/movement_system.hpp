#ifndef MOVEMENT_SYSTEM_h
#define MOVEMENT_SYSTEM_h

#include "../core/ecs/system.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"
#include "../components/position_component_3d.hpp"

class MovementSystem : public System
{
public:
  void update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager) override
  {
    // Iterate over entities and update their position based on velocity
    for (auto entity : entityManager.getAllEntities())
    {
      if (componentManager.hasComponent<PositionComponent3D>(entity))
      {
      }
      // Assuming we have a component manager instance
      // auto& velocity = componentManager.getComponent<Velocity>(entity);
      // auto& transform = componentManager.getComponent<Transform>(entity);

      // transform.position += velocity.velocity * deltaTime;
    }
  }
};

#endif
