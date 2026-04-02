#ifndef HADES_ENGINE_RUNTIME_MAIN_CAMERA_SELECTION_HPP
#define HADES_ENGINE_RUNTIME_MAIN_CAMERA_SELECTION_HPP

#include <optional>

#include "../components/camera_component.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"
#include "../core/ecs/world_utils.hpp"

namespace hades
{
  enum class MainCameraSelectionStatus
  {
    Ready,
    NoCameraPresent,
    NoMainCameraSelected,
    MultipleMainCamerasSelected,
  };

  struct MainCameraSelection
  {
    MainCameraSelectionStatus status = MainCameraSelectionStatus::NoCameraPresent;
    std::optional<Entity::EntityId> entity;
  };

  inline const char *main_camera_selection_message(MainCameraSelectionStatus status)
  {
    switch (status)
    {
    case MainCameraSelectionStatus::Ready:
      return "Ready";
    case MainCameraSelectionStatus::NoCameraPresent:
      return "Play mode requires at least one camera in the active world.";
    case MainCameraSelectionStatus::NoMainCameraSelected:
      return "Play mode requires one camera in the active world to be marked as Main Camera.";
    case MainCameraSelectionStatus::MultipleMainCamerasSelected:
      return "Play mode requires exactly one Main Camera in the active world. Clear the extra main-camera flags.";
    }

    return "Unknown main-camera state.";
  }

  inline MainCameraSelection select_main_camera(
      EntityManager &entityManager,
      ComponentManager &componentManager,
      std::optional<Entity::EntityId> world = std::nullopt)
  {
    bool foundAnyCamera = false;
    std::optional<Entity::EntityId> mainCamera;

    for (Entity::EntityId entity : entityManager.getAllEntities())
    {
      if (world.has_value() && !entity_belongs_to_world(entity, *world, componentManager))
      {
        continue;
      }

      if (!componentManager.hasComponent<CameraComponent>(entity))
      {
        continue;
      }

      foundAnyCamera = true;
      const auto &camera = componentManager.getComponent<CameraComponent>(entity);
      if (!camera.isMainCamera)
      {
        continue;
      }

      if (mainCamera.has_value())
      {
        return MainCameraSelection{MainCameraSelectionStatus::MultipleMainCamerasSelected, std::nullopt};
      }

      mainCamera = entity;
    }

    if (!foundAnyCamera)
    {
      return MainCameraSelection{MainCameraSelectionStatus::NoCameraPresent, std::nullopt};
    }

    if (!mainCamera.has_value())
    {
      return MainCameraSelection{MainCameraSelectionStatus::NoMainCameraSelected, std::nullopt};
    }

    return MainCameraSelection{MainCameraSelectionStatus::Ready, mainCamera};
  }
}

#endif
