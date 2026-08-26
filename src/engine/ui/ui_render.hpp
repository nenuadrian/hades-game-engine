#ifndef HADES_ENGINE_UI_UI_RENDER_HPP
#define HADES_ENGINE_UI_UI_RENDER_HPP

// The UI collection pass: resolves every UICanvasComponent into the frame's
// RenderList.ui geometry. Called from SceneRenderer::buildRenderList, so the
// same widgets show up in the standalone runtime, the detached play window,
// the editor's Game View and (world-space only) the editing Scene View.

#include <optional>

#include "../core/ecs/entity.hpp"

namespace hades
{
  class ComponentManager;
  class EntityManager;
  struct RenderList;
  struct RenderCamera;

  namespace ui
  {
    /// Collect world-space canvases always, and screen-space canvases only
    /// when a positive viewport size is given (edit-mode viewports pass 0 to
    /// keep HUDs out of the scene editing view). Bind variables are read
    /// through the hades::Blueprints facade when a Blueprint runtime is
    /// live; otherwise the authored values render as-is.
    void collect_ui(
        RenderList &list,
        const RenderCamera &camera,
        ComponentManager &componentManager,
        EntityManager &entityManager,
        std::optional<Entity::EntityId> worldFilter,
        float viewportWidth,
        float viewportHeight);
  }
}

#endif
