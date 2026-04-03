# Frame Metrics

A compile-time profiling system for measuring per-frame performance of the editor. When disabled (default), all instrumentation compiles to nothing — zero runtime cost.

## Enabling

```bash
cmake -DHADES_FRAME_METRICS=ON ..
make
```

To disable, reconfigure with `-DHADES_FRAME_METRICS=OFF` or omit the flag entirely.

## Viewing

Open **Settings > Editor > Show Debug Window**. The debug window displays a metrics table with:

- **Last (ms)** — time spent in the section during the most recent frame
- **Avg (ms)** — average time across all recorded frames
- **Count** — number of times the section was entered during the frame

## Instrumented Sections

### Main loop (`WindowManager::render_frame`)

| Section              | Description                          |
|----------------------|--------------------------------------|
| `frame_total`        | Entire frame                         |
| `event_poll`         | SDL event processing                 |
| `vulkan_render_frame`| Vulkan swapchain setup               |
| `imgui_begin`        | ImGui frame start                    |
| `editor_render`      | All editor UI logic                  |
| `script_update`      | Script runtime update (play mode)    |
| `systems_update`     | ECS system updates (play mode)       |
| `imgui_render`       | ImGui draw submission + Vulkan present |
| `sync_script_editor` | Script editor window render          |
| `sync_play_window`   | Play window render                   |

### Editor (`Editor::render`)

| Section           | Description                              |
|-------------------|------------------------------------------|
| `workspace_cache` | Workspace directory cache refresh        |
| `menu_bar`        | Menu bar rebuild                         |
| `plugins_pre`     | Pre-entity-deletion plugin rendering     |
| `plugins_post`    | Post-entity-deletion plugin rendering    |

## Adding New Sections

Include the header and use the macros:

```cpp
#include "../engine/profiling/frame_metrics.hpp"

// Scoped — times the enclosing block automatically
{
    HADES_FRAME_METRIC_SCOPE("my_section");
    do_work();
}

// Manual begin/end
HADES_FRAME_METRIC_BEGIN("my_section");
do_work();
HADES_FRAME_METRIC_END("my_section");
```

Section names must be string literals (pointer identity is used for lookup, no allocations).
