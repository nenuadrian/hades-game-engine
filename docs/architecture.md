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

    miniaudio-based audio engine with 4-bus mixing (Master, Music, Sfx,
    Voice) and 3D spatial audio support.

-   ### Scripting Runtime

    `src/engine/runtime`

    In-process C# scripting via the .NET CoreCLR hosting API. Handles
    script compilation (`dotnet build`), CLR embedding (hostfxr), and
    managed-to-native interop using `[UnmanagedCallersOnly]` function
    pointers. See [SCRIPTS.md](../SCRIPTS.md) for details.

-   ### Editor

    `src/editor`

    ImGui-based editor with workspace management, entity hierarchy, scene
    viewport with gizmos, inspector panels, integrated script editor,
    debug console, play mode, and a plugin system for custom panels.

-   ### GUI

    `src/engine/gui`

    Menu bar abstraction and ImGui GUI implementation used by the editor.

</div>

## Module Map

```
src/
  engine/
    core/ecs/         Entity, Component, System managers, serialization, world utils
    components/       13 data-only component types
    systems/          AudioSystem, MovementSystem, RenderSystem
    rendering/        VulkanRenderer, model preview, vector text
    audio/            AudioEngine (miniaudio wrapper)
    runtime/          ScriptRuntime, ClrHost, subprocess, dotnet config
    gui/              GUI / ImGui abstraction
  editor/
    editor.cpp/hpp    Core editor: render loop, menus, dock layout, logging API
    editor_scene.cpp  Scene viewport, settings window, debug console
    editor_entities.cpp  Entity CRUD, play mode start/stop
    editor_inspector.cpp  Properties and components panels
    editor_workspace.cpp  Workspace tree, script editor, background compilation
    editor_plugins.cpp  Built-in plugin registration
    window_manager.cpp  SDL/Vulkan window, event loop, detached windows
    detached_play_window.cpp  Play mode rendering window
    plugins/          EditorPlugin base class and registration macros
    types.h           EditorState, DebugMessage, enums
```

## Reading Order

Start here for the module map, then use the generated class relationship page
when you need to trace concrete dependencies between types.

## Diagram Generation

The class relationships page is generated from project headers in `src/`:

```bash
python3 scripts/generate_class_diagram.py
```

The generator detects:

- inheritance (`A <|-- B`)
- usage references inside class/struct bodies (`A --> B : uses`)
