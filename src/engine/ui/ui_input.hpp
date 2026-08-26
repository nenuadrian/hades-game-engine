#ifndef HADES_ENGINE_UI_UI_INPUT_HPP
#define HADES_ENGINE_UI_UI_INPUT_HPP

// Pointer tracking and click routing for screen-space canvases. The host
// (GameRuntime / editor play mode) calls these from the same place it
// forwards SDL mouse events to the script and Blueprint runtimes.

#include <optional>
#include <string>

#include "../core/ecs/entity.hpp"

namespace hades
{
  class ComponentManager;
  class EntityManager;
  class BlueprintRuntime;
  class ScriptRuntime;

  namespace ui
  {
    struct UIPointerState
    {
      float x = 0.0f;
      float y = 0.0f;
      bool valid = false;
    };

    /// Record the pointer position in viewport pixels; feeds button hover
    /// tinting during UI collection. Call from mouse-move handling.
    void set_ui_pointer(float x, float y);
    void clear_ui_pointer();
    const UIPointerState &ui_pointer();

    struct UIClickHit
    {
      Entity::EntityId entity = Entity::INVALID;
      std::string widgetId;
      std::string eventName;
    };

    /// Topmost interactive widget under (x, y) across every visible
    /// screen-space canvas (highest sortOrder first). Empty when nothing
    /// interactive is there.
    std::optional<UIClickHit> hit_test_screen_ui(
        ComponentManager &componentManager,
        EntityManager &entityManager,
        std::optional<Entity::EntityId> worldFilter,
        float x,
        float y,
        float viewportWidth,
        float viewportHeight);

    /// Hit test + dispatch: fires the widget's Click Event on the owning
    /// entity's Blueprints (widget id as payload) and delivers
    /// onMessage("ui.clicked", widgetId) to its C++ scripts. Either runtime
    /// may be null. Returns true when a widget consumed the click.
    bool dispatch_ui_click(
        ComponentManager &componentManager,
        EntityManager &entityManager,
        std::optional<Entity::EntityId> worldFilter,
        float x,
        float y,
        float viewportWidth,
        float viewportHeight,
        BlueprintRuntime *blueprintRuntime,
        ScriptRuntime *scriptRuntime);
  }
}

#endif
