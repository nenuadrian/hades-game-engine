#include "ui_layout.hpp"

namespace hades::ui
{
  UIRect resolve_widget_rect(const UIRect &parent, const UIWidget &widget)
  {
    const float anchorPointX = parent.x + widget.anchorX * parent.w + widget.offsetX;
    const float anchorPointY = parent.y + widget.anchorY * parent.h + widget.offsetY;
    UIRect rect;
    rect.w = widget.width;
    rect.h = widget.height;
    rect.x = anchorPointX - widget.anchorX * widget.width;
    rect.y = anchorPointY - widget.anchorY * widget.height;
    return rect;
  }

  namespace
  {
    void build_recursive(
        const std::vector<UIWidget> &widgets,
        const UIRect &parentRect,
        const UIBuildHooks &hooks,
        UIDrawList &out)
    {
      for (const auto &widget : widgets)
      {
        if (!widget.visible)
        {
          continue;
        }

        const UIRect rect = resolve_widget_rect(parentRect, widget);
        const UIWidgetType *type = UIWidgetRegistry::instance().find(widget.type);
        if (type != nullptr && type->draw != nullptr)
        {
          UIWidgetDrawParams params;
          float boundValue = 0.0f;
          std::string boundText;
          if (hooks.bindValue && hooks.bindValue(widget, boundValue))
          {
            params.valueOverride = &boundValue;
          }
          if (hooks.bindText && hooks.bindText(widget, boundText))
          {
            params.textOverride = &boundText;
          }
          if (hooks.isHovered && type->clickable)
          {
            params.hovered = hooks.isHovered(rect);
          }
          type->draw(widget, rect, params, out);
        }

        build_recursive(widget.children, rect, hooks, out);
      }
    }

    void hit_test_recursive(
        const std::vector<UIWidget> &widgets,
        const UIRect &parentRect,
        float x,
        float y,
        UIHit &best)
    {
      // Authored order is draw order, so a later match is on top: keep
      // overwriting `best` as the walk proceeds.
      for (const auto &widget : widgets)
      {
        if (!widget.visible)
        {
          continue;
        }

        const UIRect rect = resolve_widget_rect(parentRect, widget);
        if (rect.contains(x, y))
        {
          const UIWidgetType *type = UIWidgetRegistry::instance().find(widget.type);
          const bool clickableType = type != nullptr && type->clickable;
          if (clickableType || !widget.onClickEvent.empty())
          {
            best.widget = &widget;
            best.rect = rect;
          }
        }

        hit_test_recursive(widget.children, rect, x, y, best);
      }
    }
  }

  void build_canvas_draw_list(
      const std::vector<UIWidget> &widgets,
      const UIRect &canvasRect,
      const UIBuildHooks &hooks,
      UIDrawList &out)
  {
    register_builtin_ui_widgets();
    build_recursive(widgets, canvasRect, hooks, out);
  }

  UIHit hit_test_widgets(
      const std::vector<UIWidget> &widgets,
      const UIRect &canvasRect,
      float x,
      float y)
  {
    register_builtin_ui_widgets();
    UIHit best;
    hit_test_recursive(widgets, canvasRect, x, y, best);
    return best;
  }
}
