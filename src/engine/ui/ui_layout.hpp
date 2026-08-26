#ifndef HADES_ENGINE_UI_UI_LAYOUT_HPP
#define HADES_ENGINE_UI_UI_LAYOUT_HPP

// Pure-CPU layout resolution and hit testing over a widget tree. No renderer
// or ECS dependencies, so the unit tests can exercise it directly.

#include <functional>
#include <string>
#include <vector>

#include "ui_widget.hpp"
#include "ui_widget_registry.hpp"

namespace hades::ui
{
  /// The rect a widget occupies inside its parent's resolved rect: anchor
  /// point at `parent.origin + anchor * parent.size + offset`, box of
  /// `width x height` pixels pivoted on that same anchor.
  UIRect resolve_widget_rect(const UIRect &parent, const UIWidget &widget);

  /// Per-widget hooks the emit pass consults. Both optional.
  struct UIBuildHooks
  {
    /// Return true and fill `outValue` to override `widget.value` (bar fill).
    std::function<bool(const UIWidget &, float &outValue)> bindValue;
    /// Return true and fill `outText` to override `widget.text`.
    std::function<bool(const UIWidget &, std::string &outText)> bindText;
    /// Hover test in canvas pixels; feeds button tinting.
    std::function<bool(const UIRect &)> isHovered;
  };

  /// Walks `widgets` depth-first in authored order (later siblings and
  /// children draw on top) and appends every visible widget's primitives.
  void build_canvas_draw_list(
      const std::vector<UIWidget> &widgets,
      const UIRect &canvasRect,
      const UIBuildHooks &hooks,
      UIDrawList &out);

  struct UIHit
  {
    const UIWidget *widget = nullptr;
    UIRect rect;
  };

  /// Topmost interactive widget containing (x, y), in canvas pixels.
  /// Interactive = a clickable registry type, or any widget with a non-empty
  /// onClickEvent. Invisible subtrees are skipped.
  UIHit hit_test_widgets(
      const std::vector<UIWidget> &widgets,
      const UIRect &canvasRect,
      float x,
      float y);
}

#endif
