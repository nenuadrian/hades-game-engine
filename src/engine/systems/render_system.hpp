#ifndef RENDER_SYSTEM_H
#define RENDER_SYSTEM_H

#include <optional>

#include "../core/ecs/entity.hpp"
#include "../core/ecs/system.hpp"
#include "../core/ecs/system_context.hpp"
#include "../rendering/render_types.hpp"
#include "../rendering/scene_renderer.hpp"

namespace hades
{
  class RenderSystem : public System
  {
  public:
    void update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager) override;
    void update(float deltaTime, SystemContext &context) override;

    /// Set the active world to filter rendering to.
    void set_active_world(std::optional<Entity::EntityId> activeWorld);

    /// Set the viewport aspect ratio (width / height).
    void set_aspect_ratio(float aspectRatio);

    /// Set the camera entity to render from. If not set, uses the main camera.
    void set_camera(std::optional<Entity::EntityId> cameraEntity);

    /// Access the render list produced by the last update.
    const RenderList &lastRenderList() const { return renderList_; }

  private:
    SceneRenderer sceneRenderer_;
    RenderList renderList_;
    std::optional<Entity::EntityId> activeWorld_;
    std::optional<Entity::EntityId> cameraEntity_;
    float aspectRatio_ = 16.0f / 9.0f;
  };
}
#endif
