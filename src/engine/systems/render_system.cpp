#include "render_system.hpp"

#include "../components/render_component.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"
#include "../core/ecs/query.hpp"

namespace hades
{
  void RenderSystem::update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager)
  {
    (void)deltaTime;

    for (auto entity : query<RenderComponent>(entityManager))
    {
      auto &renderComp = componentManager.getComponent<RenderComponent>(entity);
      (void)renderComp;
    }
  }
}
