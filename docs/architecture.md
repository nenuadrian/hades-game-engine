# Architecture Overview

Hades is split into a small set of core areas so runtime systems, rendering,
and editor code stay easy to trace.

<div class="grid cards architecture-grid" markdown>

-   ### ECS core

    `src/engine/core/ecs`

    Entity, component, and system management primitives. Includes scene
    serialization (JSON), world utilities, and the entity factory.

-   ### Components

    `src/engine/components`

    Data-only gameplay and render state definitions. 13 component types
    covering transforms, rendering, audio, scripting, and metadata.

-   ### Systems

    `src/engine/systems`

    Runtime behavior that operates across component sets. Three systems:
    AudioSystem, MovementSystem, RenderSystem.

-   ### Rendering

    `src/engine/rendering`

    Vulkan-based renderer with ImGui integration, software model preview
    rasterizer, and vector text geometry generation.

-   ### Audio

    `src/engine/audio`

    SoLoud-based audio engine with 4-bus mixing (Master, Music, Sfx,
    Voice) and 3D spatial audio support.

-   ### Scripting Runtime

    `src/engine/runtime`

    C++ scripting compiled into shared libraries at play start. Handles
    script compilation, dynamic loading, and direct ECS access from user
    scripts. See [Scripting](scripting.md) for details.

-   ### Editor

    `src/editor`

    ImGui-based editor with workspace management, entity hierarchy, scene
    viewport with gizmos, inspector panels, integrated script editor,
    debug console, play mode, and a plugin system for custom panels.

-   ### GUI

    `src/engine/gui`

    Menu bar abstraction and ImGui GUI implementation used by the editor.

</div>
