#include "render_system.hpp"

#include "../components/render_component.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"

namespace hades
{
  void RenderSystem::update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager)
  {
    (void)deltaTime;

    for (auto entity : entityManager.getAllEntities())
    {
      if (componentManager.hasComponent<RenderComponent>(entity))
      {
        auto &renderComp = componentManager.getComponent<RenderComponent>(entity);
        (void)renderComp;
      }
    }
  }
}
