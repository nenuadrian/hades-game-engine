#ifndef HADES_ENGINE_UI_UI_WIDGET_REGISTRY_HPP
#define HADES_ENGINE_UI_UI_WIDGET_REGISTRY_HPP

// Widget-type registry, mirroring BlueprintNodeRegistry: a Meyers singleton
// of imperatively registered type descriptors. The inspector's widget
// palette, the layout pass and the draw pass all iterate it, so a type a
// game or plugin registers behaves exactly like a built-in.

#include <string>
#include <string_view>
#include <vector>

#include "ui_widget.hpp"

namespace hades
{
  /// Canvas-space draw output of a widget: solid quads and line segments in
  /// canvas pixels (origin top-left, y down). The renderer maps these into
  /// screen or world space depending on the canvas.
  struct UIDrawList
  {
    struct Color
    {
      float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    };

    struct Quad
    {
      float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
      Color color;
    };

    struct Line
    {
      float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
      Color color;
    };

    std::vector<Quad> quads;
    std::vector<Line> lines;

    void add_quad(float x, float y, float w, float h, const Color &color);
    void add_line(float x1, float y1, float x2, float y2, const Color &color);

    /// Lays out `text` with the engine's stroke font and appends the glyph
    /// segments, top-left corner at (x, y). `wrapWidth` 0 disables wrapping.
    void add_text(std::string_view text, float x, float y, float sizePx,
                  float wrapWidth, const Color &color);
  };

  /// Width x height in pixels the stroke font needs for `text` at `sizePx`.
  void measure_ui_text(std::string_view text, float sizePx, float wrapWidth,
                       float &outWidth, float &outHeight);

  /// Per-draw context the renderer hands a widget's draw function. Bind
  /// overrides come from Blueprint variables and are view-only.
  struct UIWidgetDrawParams
  {
    bool hovered = false;
    const float *valueOverride = nullptr;
    const std::string *textOverride = nullptr;

    float effective_value(const UIWidget &widget) const
    {
      return valueOverride != nullptr ? *valueOverride : widget.value;
    }

    const std::string &effective_text(const UIWidget &widget) const
    {
      return textOverride != nullptr ? *textOverride : widget.text;
    }
  };

  using UIWidgetDrawFn =
      void (*)(const UIWidget &, const UIRect &, const UIWidgetDrawParams &, UIDrawList &);

  struct UIWidgetType
  {
    /// Stable name stored in scene files ("panel", "bar", ...). Never rename.
    std::string name;
    std::string displayName;
    std::string tooltip;
    /// Clickable types take part in hit testing even without an
    /// onClickEvent (the click still reaches scripts as "ui.clicked").
    bool clickable = false;
    /// Field defaults applied when the type is added in the inspector or via
    /// hades::UI::addWidget.
    UIWidget defaults;
    UIWidgetDrawFn draw = nullptr;
  };

  class UIWidgetRegistry
  {
  public:
    static UIWidgetRegistry &instance();

    void register_type(UIWidgetType type);
    const UIWidgetType *find(const std::string &name) const;
    /// Registration order == inspector palette order.
    const std::vector<UIWidgetType> &all() const { return types_; }

  private:
    std::vector<UIWidgetType> types_;
  };

  /// Registers panel, text, bar and button. Idempotent, and called by
  /// everything that consumes the registry, so callers never have to
  /// remember -- the same contract as register_builtin_blueprint_nodes().
  void register_builtin_ui_widgets();
}

#endif
