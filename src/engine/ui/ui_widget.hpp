#ifndef HADES_ENGINE_UI_UI_WIDGET_HPP
#define HADES_ENGINE_UI_UI_WIDGET_HPP

// The authored UI data model: a tree of typed widgets living inside a
// UICanvasComponent. Deliberately a plain aggregate with defaults on every
// member, like the rest of src/engine/components/, and deliberately light on
// includes -- scripts see this header through engine/hades.hpp.
//
// The engine ships primitives (panel, text, bar, button) and composition;
// what a health bar or a menu looks like is authored by the game, not baked
// into the engine. Widget behaviour lives in UIWidgetRegistry, so games and
// plugins can register new types that serialize, edit and draw like the
// built-ins.

#include <string>
#include <vector>

namespace hades
{
  /// An axis-aligned rectangle in canvas pixels, origin top-left, y down.
  struct UIRect
  {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool contains(float px, float py) const
    {
      return px >= x && px < x + w && py >= y && py < y + h;
    }
  };

  struct UIWidget
  {
    /// Addressable name, unique within its canvas. hades::UI, the `ui.*`
    /// Blueprint nodes and click events all refer to widgets by this id.
    std::string id;

    /// Registry type name: "panel", "text", "bar", "button", or anything a
    /// game registered. Unknown types survive load/save and draw nothing.
    std::string type = "panel";

    bool visible = true;

    /// Layout: the widget's own anchor point sits at
    /// `parent.origin + anchor * parent.size + offset`, and the box extends
    /// `width x height` pixels around that point (the anchor is also the
    /// pivot, so anchor 0.5/0.5 centers the box on the resolved point).
    float anchorX = 0.5f;
    float anchorY = 0.5f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float width = 100.0f;
    float height = 24.0f;

    /// Primary color. Panels and bars use it as the background, text as the
    /// glyph color, buttons as the face.
    float colorR = 1.0f;
    float colorG = 1.0f;
    float colorB = 1.0f;
    float colorA = 1.0f;

    /// Secondary color. Bars use it for the fill, buttons for the label.
    float fillColorR = 1.0f;
    float fillColorG = 1.0f;
    float fillColorB = 1.0f;
    float fillColorA = 1.0f;

    /// Text content (text and button labels).
    std::string text;
    float textSize = 16.0f;

    /// Normalized 0..1 value (bar fill fraction).
    float value = 1.0f;

    /// Name of a Blueprint variable on the owning entity to read every frame
    /// instead of `value` / `text`. View-only: the component is not mutated.
    std::string bindVariable;

    /// Custom Event fired on the owning entity's Blueprints when this widget
    /// is clicked (screen-space canvases only). The widget id rides along as
    /// the event payload, and scripts hear the same click as an
    /// onMessage("ui.clicked", id).
    std::string onClickEvent;

    std::vector<UIWidget> children;
  };
}

#endif
