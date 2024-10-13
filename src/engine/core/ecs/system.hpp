#include "entity.hpp"
#include "component_manager.hpp"

#include <vector>

class System
{
public:
  std::vector<Entity::EntityId> entities;

  virtual void update(float deltaTime, ComponentManager &componentManager) = 0;
};

class MovementSystem : public System
{
public:
  void update(float deltaTime, ComponentManager &componentManager) override
  {
    // Iterate over entities and update their position based on velocity
    for (auto entity : entities)
    {
      // Assuming we have a component manager instance
      // auto& velocity = componentManager.getComponent<Velocity>(entity);
      // auto& transform = componentManager.getComponent<Transform>(entity);

      // transform.position += velocity.velocity * deltaTime;
    }
  }
};
