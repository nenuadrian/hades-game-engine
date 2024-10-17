#ifndef RENDER_SYSTEM_H
#define RENDER_SYSTEM_H

#include "../core/ecs/system.hpp"
#include "../components/render_component.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"

namespace hades
{
  class RenderSystem : public System
  {
  public:
    void update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager) override
    {
      // Iterate over entities and update their position based on velocity
      for (auto entity : entityManager.getAllEntities())
      {
        if (componentManager.hasComponent<RenderComponent>(entity))
        {
          auto &renderComp = componentManager.getComponent<RenderComponent>(entity);
        }

        // Submit the draw call
        //  bgfx::submit(0, renderComp.program); // 'program' is the bgfx shader program
      }
    }
  };
}
#endif
