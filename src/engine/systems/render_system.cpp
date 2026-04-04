#include "render_system.hpp"

#include "../components/camera_component.hpp"
#include "../components/position_component_3d.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"
#include "../core/ecs/query.hpp"
#include "../runtime/main_camera_selection.hpp"

namespace hades
{
  void RenderSystem::update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager)
  {
    (void)deltaTime;

    // Find the camera entity.
    Entity::EntityId cameraEntityId = Entity::INVALID;
    if (cameraEntity_.has_value())
    {
      cameraEntityId = *cameraEntity_;
    }
    else
    {
      const auto mainCamera = select_main_camera(entityManager, componentManager, activeWorld_);
      if (mainCamera.status == MainCameraSelectionStatus::Ready && mainCamera.entity.has_value())
      {
        cameraEntityId = *mainCamera.entity;
      }
    }

    if (cameraEntityId == Entity::INVALID)
    {
      renderList_.clear();
      return;
    }

    // Build camera and render list.
    RenderCamera camera = sceneRenderer_.buildCamera(cameraEntityId, aspectRatio_, componentManager);
    renderList_ = sceneRenderer_.buildRenderList(camera, componentManager, entityManager, activeWorld_);
  }

  void RenderSystem::update(float deltaTime, SystemContext &context)
  {
    update(deltaTime, context.componentManager, context.entityManager);
  }

  void RenderSystem::set_active_world(std::optional<Entity::EntityId> activeWorld)
  {
    activeWorld_ = activeWorld;
  }

  void RenderSystem::set_aspect_ratio(float aspectRatio)
  {
    aspectRatio_ = aspectRatio;
  }

  void RenderSystem::set_camera(std::optional<Entity::EntityId> cameraEntity)
  {
    cameraEntity_ = cameraEntity;
  }
}
