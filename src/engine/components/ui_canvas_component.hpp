#ifndef HADES_ENGINE_COMPONENTS_UI_CANVAS_COMPONENT_HPP
#define HADES_ENGINE_COMPONENTS_UI_CANVAS_COMPONENT_HPP

#include <vector>

#include "../ui/ui_widget.hpp"

namespace hades
{
  enum class UICanvasSpace
  {
    /// Anchored to the viewport: HUD, menus. Laid out in viewport pixels.
    Screen = 0,
    /// Attached to the entity's transform: health bars, nameplates. Laid out
    /// in reference pixels, then mapped onto a `worldWidth`-unit-wide quad.
    World = 1,
  };

  struct UICanvasComponent
  {
    UICanvasSpace space = UICanvasSpace::Screen;
    bool visible = true;

    /// Layout resolution for world-space canvases (pixel coordinate system
    /// the widget tree is authored in). Screen canvases lay out directly in
    /// viewport pixels and ignore these.
    float referenceWidth = 400.0f;
    float referenceHeight = 200.0f;

    /// World-space placement: quad width in world units (height follows the
    /// reference aspect), centered at entity position + offset.
    float worldWidth = 2.0f;
    float offsetX = 0.0f;
    float offsetY = 1.5f;
    float offsetZ = 0.0f;

    /// Face the camera every frame; otherwise use the entity's rotation.
    bool billboard = true;

    /// Cull beyond this camera distance (0 = never), fading out over the
    /// last `fadeDistance` units before the cutoff.
    float maxDistance = 0.0f;
    float fadeDistance = 0.0f;

    /// Screen canvases draw in ascending order; higher sorts on top and is
    /// hit-tested first.
    int sortOrder = 0;

    std::vector<UIWidget> widgets;
  };
}

#endif
