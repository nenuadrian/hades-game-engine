# Game UI

Hades ships a game-facing UI system for HUDs, menus, and world-space widgets
such as health bars floating over monsters. The engine provides *mechanism*
— canvases, composable widget primitives, layout, input routing, data
binding — and the game composes them into whatever it wants. There is no
built-in "health bar" feature: a health bar is a `bar` widget on a
world-space canvas, authored by you.

The same widgets render everywhere a scene renders: the standalone runtime
(exported games), the detached play window, and the editor's Game View.
World-space canvases also draw in the editing Scene View so they can be
authored in place; screen-space canvases stay out of edit-mode viewports.

## Canvases

Everything hangs off a **UI Canvas** component (`UICanvasComponent`,
inspector: *Add Component → UI Canvas*). A canvas holds a tree of widgets
and lives in one of two spaces:

| Space | Use | Layout |
|-------|-----|--------|
| **Screen** | HUD, menus, overlays | Viewport pixels; anchors are viewport fractions |
| **World** | Health bars, nameplates, interaction prompts | Reference pixels mapped onto a `worldWidth`-unit quad at the entity |

World canvases can **billboard** (face the camera) or follow the entity's
rotation, sit at a world-space `offset` from the entity origin, cull beyond
`maxDistance`, and fade out over the last `fadeDistance` units. They render
with depth testing on and depth writes off, so level geometry occludes them
but overlapping translucent bars don't clip each other. Screen canvases draw
after the 3D scene in `sortOrder` order; a higher order draws on top and is
hit-tested first.

## Widgets

Widgets are typed nodes in a tree. Each has an **id** (how scripts,
Blueprints and click events address it), anchor/offset/size layout, and
colors. Children lay out inside their parent's resolved rectangle; anchor
0.5/0.5 centers, 0/0 pins top-left, 1/1 bottom-right (the anchor is also
the pivot).

Built-in types:

| Type | Draws | Notes |
|------|-------|-------|
| `panel` | solid rectangle | grouping / backgrounds |
| `text` | stroke-font text | A–Z, digits, punctuation; `width` is the wrap width |
| `bar` | background + fill scaled by `value` (0..1) | fill uses the secondary color |
| `button` | face + centered label, hover tint | clickable |

Text renders with the engine's built-in vector stroke font — no font assets
needed, and it works identically in exported games.

The set is extensible: register a `UIWidgetType` (name, defaults, draw
function) with `UIWidgetRegistry::instance()` and the new type appears in
the inspector palette, serializes with scenes, and is addressable from both
APIs, exactly like a built-in. See `src/engine/ui/ui_widget_registry.cpp`
for the built-ins as the pattern.

## Driving UI from C++ scripts

The `hades::UI` facade mirrors `hades::Audio`: static, entity-first, and a
safe no-op when nothing is registered.

```cpp
#include "engine/hades.hpp"

class MonsterHealth : public hades::HadesScript
{
  float health_ = 1.0f;

  void onUpdate(hades::ScriptContext &ctx, float dt) override
  {
    health_ = std::max(0.0f, health_ - 0.1f * dt);
    hades::UI::setValue(ctx.entityId, "healthBar", health_);
  }

  hades::ScriptValue onMessage(hades::ScriptContext &ctx,
                               const std::string &name,
                               const hades::ScriptValue &value) override
  {
    if (name == "ui.clicked" && value.asString() == "restartButton")
    {
      // clicked!
    }
    return {};
  }
};
HADES_REGISTER_SCRIPT(MonsterHealth)
```

Widgets can also be built entirely at runtime:

```cpp
hades::UI::addWidget(hudEntity, "", "panel", "root");
hades::UI::addWidget(hudEntity, "root", "text", "score");
hades::UI::setText(hudEntity, "score", "SCORE 0");
```

`hades::UI::widget()` returns a raw `UIWidget*` as the escape hatch for
anything the setters don't cover.

## Driving UI from Blueprints

The **UI** node category operates on the same widget trees: *Set Widget
Text / Value / Visible / Color / Fill Color*, *Set Canvas Visible*, and the
pure *Get Widget Value / Text* and *Widget Exists*. Nodes take the target
entity (unconnected = the Blueprint's own entity) and the widget id.

Two Blueprint integrations need no nodes at all:

- **Binding** — set a widget's *Bind Variable* to a Blueprint variable name
  on the owning entity and the widget reads it every frame (a bar tracks
  `health` with zero wiring). Binding is view-only; the component's
  authored value is untouched, so play-mode changes reset cleanly.
- **Click events** — a widget's *Click Event* names a Custom Event fired on
  the owning entity's Blueprints when the widget is clicked, with the
  widget id as the payload. Clicks also reach C++ scripts as
  `onMessage("ui.clicked", widgetId)`.

Click handling applies to screen-space canvases; the topmost interactive
widget under the cursor wins. `button` widgets are always interactive; any
other type becomes interactive by having a Click Event.

## A monster health bar, end to end

1. On the monster entity: *Add Component → UI Canvas*, space **World**,
   offset Y above the model, *Billboard* on, *Max Distance* ~40.
2. Add a `bar` widget, id `healthBar`; optionally a `text` child for the
   name.
3. Drive it either way:
   - Blueprint: set the bar's *Bind Variable* to `health`, done — or use
     *Set Widget Value* nodes.
   - C++: `hades::UI::setValue(ctx.entityId, "healthBar", fraction)`.

Everything serializes with the scene, snapshots/restores across play mode,
and ships in exported games unchanged.

## Limitations (current)

- Widgets are flat-colored; there is no texture/image widget yet (the
  engine has no texture pipeline — when it grows one, an `image` widget
  slots into the registry).
- Screen-space click coordinates in the docked editor Game View use the
  editor window's coordinate space (the same convention script mouse input
  already uses there). The detached play window and exported games are
  exact.
- World-space canvases are display-only; hit testing covers screen space.
- `TextComponent` (world-space 3D text) is still editor-preview only; use a
  world-space canvas with a `text` widget for runtime text.

## Code map

| Piece | Where |
|-------|-------|
| Widget model / canvas component | `src/engine/ui/ui_widget.hpp`, `src/engine/components/ui_canvas_component.hpp` |
| Widget type registry + built-ins | `src/engine/ui/ui_widget_registry.{hpp,cpp}` |
| Layout + hit testing | `src/engine/ui/ui_layout.{hpp,cpp}` |
| Frame collection (canvas → RenderList) | `src/engine/ui/ui_render.{hpp,cpp}` |
| Input routing / click dispatch | `src/engine/ui/ui_input.{hpp,cpp}` |
| C++ facade | `src/engine/ui/script_ui.{hpp,cpp}` |
| Blueprint nodes | `src/engine/ui/ui_blueprint_nodes.{hpp,cpp}` |
| GPU pipeline | `src/engine/rendering/vulkan_ui_pipeline.{hpp,cpp}`, `shaders/ui.{vert,frag}.glsl` |
| Tests | `src/tests/ui_system_tests.cpp` |
