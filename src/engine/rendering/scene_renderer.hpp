#ifndef HADES_ENGINE_RENDERING_SCENE_RENDERER_HPP
#define HADES_ENGINE_RENDERING_SCENE_RENDERER_HPP

#include <optional>

#include "../core/ecs/entity.hpp"
#include "render_types.hpp"

namespace hades
{
  class ComponentManager;
  class EntityManager;

  class SceneRenderer
  {
  public:
    /// Build a RenderCamera from a camera entity's components.
    RenderCamera buildCamera(
        Entity::EntityId cameraEntity,
        float aspectRatio,
        ComponentManager &componentManager) const;

    /// Build a RenderCamera from explicit parameters (for editor orbit camera).
    RenderCamera buildCamera(
        const math::Vec3 &position,
        const math::Vec3 &target,
        float fovY,
        float aspectRatio,
        float nearClip,
        float farClip) const;

    /// Build the complete render list for the current frame.
    ///
    /// `uiViewportWidth/Height` is the pixel size screen-space UI canvases
    /// lay out against. Leave 0 (edit-mode viewports) to collect world-space
    /// UI only and keep HUDs out of the view.
    RenderList buildRenderList(
        const RenderCamera &camera,
        ComponentManager &componentManager,
        EntityManager &entityManager,
        std::optional<Entity::EntityId> worldFilter = std::nullopt,
        float uiViewportWidth = 0.0f,
        float uiViewportHeight = 0.0f);

  private:
    void collectLights(
        RenderList &list,
        ComponentManager &componentManager,
        EntityManager &entityManager,
        std::optional<Entity::EntityId> worldFilter);

    void collectRenderables(
        RenderList &list,
        const RenderCamera &camera,
        ComponentManager &componentManager,
        EntityManager &entityManager,
        std::optional<Entity::EntityId> worldFilter);

    float computeBoundsRadius(const math::Vec3 &scale) const;
  };
}

#endif
