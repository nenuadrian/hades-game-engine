#ifndef HADES_ENGINE_UI_SCRIPT_UI_HPP
#define HADES_ENGINE_UI_SCRIPT_UI_HPP

// Scripting-friendly UI facade over UICanvasComponent widget trees.
//
// Use this from HadesScript subclasses to drive HUDs, menus and world-space
// widgets (health bars over monsters) that were authored in the inspector or
// built at runtime with addWidget:
//
//   void onUpdate(ScriptContext &ctx, float dt) override
//   {
//     hades::UI::setValue(ctx.entityId, "health", health_ / maxHealth_);
//     hades::UI::setText(hudEntity_, "score", "SCORE " + std::to_string(score_));
//   }
//
// Clicks on screen-space widgets arrive as onMessage(ctx, "ui.clicked", id)
// and, on Blueprints, as the widget's configured Click Event.
//
// Deliberately mirrors `hades::Audio`: static, entity-first, registered by
// the host at play start, and every call is a no-op (or returns a default)
// when no UI-capable host is running, so scripts do not have to guard.

#include <string>

#include "../core/ecs/entity.hpp"
#include "ui_widget.hpp"

namespace hades
{
  class ComponentManager;

  /// Called by the runtime / editor once the ECS is live so the facade can
  /// reach UICanvasComponent data. Pass nullptr on shutdown so scripts that
  /// fire afterwards become no-ops.
  void register_script_ui_components(ComponentManager *componentManager);

  class UI
  {
  public:
    // ---- Queries ---------------------------------------------------------
    static bool hasCanvas(Entity::EntityId entity);
    static bool widgetExists(Entity::EntityId entity, const std::string &widgetId);
    static float getValue(Entity::EntityId entity, const std::string &widgetId);
    static std::string getText(Entity::EntityId entity, const std::string &widgetId);

    // ---- Widget state ----------------------------------------------------
    static bool setText(Entity::EntityId entity, const std::string &widgetId, const std::string &text);
    static bool setValue(Entity::EntityId entity, const std::string &widgetId, float value);
    static bool setVisible(Entity::EntityId entity, const std::string &widgetId, bool visible);
    static bool setColor(Entity::EntityId entity, const std::string &widgetId,
                         float r, float g, float b, float a = 1.0f);
    static bool setFillColor(Entity::EntityId entity, const std::string &widgetId,
                             float r, float g, float b, float a = 1.0f);

    // ---- Canvas state ----------------------------------------------------
    static bool setCanvasVisible(Entity::EntityId entity, bool visible);

    // ---- Structure -------------------------------------------------------
    /// Append a widget of registry type `type` ("panel", "text", "bar",
    /// "button", or a game-registered type) under `parentId` ("" = root).
    static bool addWidget(Entity::EntityId entity, const std::string &parentId,
                          const std::string &type, const std::string &widgetId);
    static bool removeWidget(Entity::EntityId entity, const std::string &widgetId);

    // ---- Escape hatch ----------------------------------------------------
    /// Direct pointer into the live widget tree, for anything the setters
    /// don't cover. Valid only until the tree is structurally changed; may
    /// return nullptr. Prefer the setters.
    static UIWidget *widget(Entity::EntityId entity, const std::string &widgetId);
  };
}

#endif
