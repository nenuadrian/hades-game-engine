#include "ui_widget_registry.hpp"

#include <algorithm>

#include "../rendering/vector_text.hpp"

namespace hades
{
  void UIDrawList::add_quad(float x, float y, float w, float h, const Color &color)
  {
    if (w <= 0.0f || h <= 0.0f || color.a <= 0.0f)
    {
      return;
    }
    quads.push_back(Quad{x, y, w, h, color});
  }

  void UIDrawList::add_line(float x1, float y1, float x2, float y2, const Color &color)
  {
    if (color.a <= 0.0f)
    {
      return;
    }
    lines.push_back(Line{x1, y1, x2, y2, color});
  }

  void UIDrawList::add_text(std::string_view text, float x, float y, float sizePx,
                            float wrapWidth, const Color &color)
  {
    if (text.empty() || sizePx <= 0.0f || color.a <= 0.0f)
    {
      return;
    }

    VectorTextStyle style;
    style.characterHeight = sizePx;
    style.wrapWidth = wrapWidth;
    const VectorTextLayout layout = layout_vector_text(text, style);
    lines.reserve(lines.size() + layout.segments.size());
    for (const auto &segment : layout.segments)
    {
      lines.push_back(Line{
          x + segment.start.x, y + segment.start.y,
          x + segment.end.x, y + segment.end.y,
          color});
    }
  }

  void measure_ui_text(std::string_view text, float sizePx, float wrapWidth,
                       float &outWidth, float &outHeight)
  {
    outWidth = 0.0f;
    outHeight = 0.0f;
    if (text.empty() || sizePx <= 0.0f)
    {
      return;
    }
    VectorTextStyle style;
    style.characterHeight = sizePx;
    style.wrapWidth = wrapWidth;
    const VectorTextLayout layout = layout_vector_text(text, style);
    outWidth = layout.width;
    outHeight = layout.height;
  }

  UIWidgetRegistry &UIWidgetRegistry::instance()
  {
    static UIWidgetRegistry registry;
    return registry;
  }

  void UIWidgetRegistry::register_type(UIWidgetType type)
  {
    for (auto &existing : types_)
    {
      if (existing.name == type.name)
      {
        existing = std::move(type);
        return;
      }
    }
    types_.push_back(std::move(type));
  }

  const UIWidgetType *UIWidgetRegistry::find(const std::string &name) const
  {
    for (const auto &type : types_)
    {
      if (type.name == name)
      {
        return &type;
      }
    }
    return nullptr;
  }

  namespace
  {
    UIDrawList::Color widget_color(const UIWidget &w)
    {
      return {w.colorR, w.colorG, w.colorB, w.colorA};
    }

    UIDrawList::Color widget_fill_color(const UIWidget &w)
    {
      return {w.fillColorR, w.fillColorG, w.fillColorB, w.fillColorA};
    }

    void draw_panel(const UIWidget &widget, const UIRect &rect,
                    const UIWidgetDrawParams &, UIDrawList &out)
    {
      out.add_quad(rect.x, rect.y, rect.w, rect.h, widget_color(widget));
    }

    void draw_text(const UIWidget &widget, const UIRect &rect,
                   const UIWidgetDrawParams &params, UIDrawList &out)
    {
      out.add_text(params.effective_text(widget), rect.x, rect.y,
                   widget.textSize, rect.w, widget_color(widget));
    }

    void draw_bar(const UIWidget &widget, const UIRect &rect,
                  const UIWidgetDrawParams &params, UIDrawList &out)
    {
      out.add_quad(rect.x, rect.y, rect.w, rect.h, widget_color(widget));
      const float fraction = std::clamp(params.effective_value(widget), 0.0f, 1.0f);
      if (fraction > 0.0f)
      {
        out.add_quad(rect.x, rect.y, rect.w * fraction, rect.h, widget_fill_color(widget));
      }
    }

    void draw_button(const UIWidget &widget, const UIRect &rect,
                     const UIWidgetDrawParams &params, UIDrawList &out)
    {
      UIDrawList::Color face = widget_color(widget);
      if (params.hovered)
      {
        face.r = std::min(1.0f, face.r * 1.35f + 0.05f);
        face.g = std::min(1.0f, face.g * 1.35f + 0.05f);
        face.b = std::min(1.0f, face.b * 1.35f + 0.05f);
      }
      out.add_quad(rect.x, rect.y, rect.w, rect.h, face);

      const std::string &label = params.effective_text(widget);
      if (!label.empty())
      {
        float textW = 0.0f;
        float textH = 0.0f;
        measure_ui_text(label, widget.textSize, 0.0f, textW, textH);
        out.add_text(label,
                     rect.x + (rect.w - textW) * 0.5f,
                     rect.y + (rect.h - textH) * 0.5f,
                     widget.textSize, 0.0f, widget_fill_color(widget));
      }
    }
  }

  void register_builtin_ui_widgets()
  {
    static bool registered = false;
    if (registered)
    {
      return;
    }
    registered = true;

    auto &registry = UIWidgetRegistry::instance();

    {
      UIWidgetType type;
      type.name = "panel";
      type.displayName = "Panel";
      type.tooltip = "A solid rectangle; the grouping block everything else nests in.";
      type.defaults.type = "panel";
      type.defaults.colorR = 0.10f;
      type.defaults.colorG = 0.10f;
      type.defaults.colorB = 0.12f;
      type.defaults.colorA = 0.60f;
      type.draw = draw_panel;
      registry.register_type(std::move(type));
    }

    {
      UIWidgetType type;
      type.name = "text";
      type.displayName = "Text";
      type.tooltip = "Stroke-font text. Width doubles as the wrap width.";
      type.defaults.type = "text";
      type.defaults.text = "Text";
      type.defaults.width = 200.0f;
      type.draw = draw_text;
      registry.register_type(std::move(type));
    }

    {
      UIWidgetType type;
      type.name = "bar";
      type.displayName = "Bar";
      type.tooltip = "A background with a fill scaled by `value` (0..1). Health, mana, progress.";
      type.defaults.type = "bar";
      type.defaults.width = 160.0f;
      type.defaults.height = 16.0f;
      type.defaults.colorR = 0.08f;
      type.defaults.colorG = 0.08f;
      type.defaults.colorB = 0.10f;
      type.defaults.colorA = 0.85f;
      type.defaults.fillColorR = 0.18f;
      type.defaults.fillColorG = 0.80f;
      type.defaults.fillColorB = 0.32f;
      type.draw = draw_bar;
      registry.register_type(std::move(type));
    }

    {
      UIWidgetType type;
      type.name = "button";
      type.displayName = "Button";
      type.tooltip = "A clickable face with a centered label. Fires its Click Event and ui.clicked.";
      type.clickable = true;
      type.defaults.type = "button";
      type.defaults.width = 160.0f;
      type.defaults.height = 36.0f;
      type.defaults.colorR = 0.15f;
      type.defaults.colorG = 0.17f;
      type.defaults.colorB = 0.22f;
      type.defaults.colorA = 0.90f;
      type.defaults.text = "Button";
      type.draw = draw_button;
      registry.register_type(std::move(type));
    }
  }
}
