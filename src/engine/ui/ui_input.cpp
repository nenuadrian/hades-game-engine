#include "ui_input.hpp"

#include <algorithm>
#include <vector>

#include "../blueprint/blueprint_runtime.hpp"
#include "../blueprint/blueprint_value.hpp"
#include "../components/ui_canvas_component.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"
#include "../core/ecs/query.hpp"
#include "../runtime/script_runtime.hpp"
#include "ui_layout.hpp"

namespace hades::ui
{
  namespace
  {
    UIPointerState g_pointer;
  }

  void set_ui_pointer(float x, float y)
  {
    g_pointer.x = x;
    g_pointer.y = y;
    g_pointer.valid = true;
  }

  void clear_ui_pointer()
  {
    g_pointer = UIPointerState{};
  }

  const UIPointerState &ui_pointer()
  {
    return g_pointer;
  }

  std::optional<UIClickHit> hit_test_screen_ui(
      ComponentManager &componentManager,
      EntityManager &entityManager,
      std::optional<Entity::EntityId> worldFilter,
      float x,
      float y,
      float viewportWidth,
      float viewportHeight)
  {
    if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
    {
      return std::nullopt;
    }

    struct CanvasRef
    {
      Entity::EntityId entity;
      const UICanvasComponent *canvas;
    };
    std::vector<CanvasRef> canvases;
    for (Entity::EntityId entity :
         query<UICanvasComponent>(entityManager, componentManager, worldFilter))
    {
      const auto &canvas = componentManager.getComponent<UICanvasComponent>(entity);
      if (canvas.space == UICanvasSpace::Screen && canvas.visible && !canvas.widgets.empty())
      {
        canvases.push_back(CanvasRef{entity, &canvas});
      }
    }

    // Highest sortOrder draws last, so it gets first claim on the click.
    std::stable_sort(canvases.begin(), canvases.end(),
                     [](const CanvasRef &a, const CanvasRef &b)
                     {
                       return a.canvas->sortOrder > b.canvas->sortOrder;
                     });

    const UIRect canvasRect{0.0f, 0.0f, viewportWidth, viewportHeight};
    for (const auto &ref : canvases)
    {
      const UIHit hit = hit_test_widgets(ref.canvas->widgets, canvasRect, x, y);
      if (hit.widget != nullptr)
      {
        UIClickHit result;
        result.entity = ref.entity;
        result.widgetId = hit.widget->id;
        result.eventName = hit.widget->onClickEvent;
        return result;
      }
    }
    return std::nullopt;
  }

  bool dispatch_ui_click(
      ComponentManager &componentManager,
      EntityManager &entityManager,
      std::optional<Entity::EntityId> worldFilter,
      float x,
      float y,
      float viewportWidth,
      float viewportHeight,
      BlueprintRuntime *blueprintRuntime,
      ScriptRuntime *scriptRuntime)
  {
    const auto hit = hit_test_screen_ui(
        componentManager, entityManager, worldFilter, x, y, viewportWidth, viewportHeight);
    if (!hit.has_value())
    {
      return false;
    }

    if (blueprintRuntime != nullptr && !hit->eventName.empty())
    {
      blueprintRuntime->send_custom_event_to(
          hit->entity, hit->eventName, {BlueprintValue::from_string(hit->widgetId)});
    }
    if (scriptRuntime != nullptr)
    {
      scriptRuntime->send_message(hit->entity, "ui.clicked", ScriptValue(hit->widgetId));
    }
    return true;
  }
}
