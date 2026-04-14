# C++ Scripting

Hades supports attaching C++ scripts to entities through a `Script` component in
the editor. Scripts are compiled into a shared library when play mode starts,
then loaded and executed in-process via `dlopen`/`LoadLibrary`.

## Editor Workflow

1. Select an entity in the editor.
2. In the `Components` panel, click `Add Script Component`.
3. Add one or more script attachments to the entity.
4. For each attachment set:
   - **Script Path**: path to a `.cpp` file (relative to the workspace root)
   - **Class**: select the registered script class from the dropdown. The editor
     scans for `HADES_REGISTER_SCRIPT(ClassName)` macros in the file.
   - **Enabled**: whether that attachment should run
5. Press **Game > Play** (or use the menu).

The Debug Console panel opens automatically when play starts. If scripts fail to
compile or load, the error appears there in red.

## Script Shape

Scripts must derive from `hades::HadesScript` and register themselves with the
`HADES_REGISTER_SCRIPT` macro. The base class exposes these virtual entry points:

- `onStart(ScriptContext& ctx)` -- called once when play starts
- `onUpdate(ScriptContext& ctx, float deltaTime)` -- called every frame
- `onKeyDown(ScriptContext& ctx, int keyCode)` -- called on SDL key down events
- `onKeyUp(ScriptContext& ctx, int keyCode)` -- called on SDL key up events
- `onMouseDown(ScriptContext& ctx, int button, float screenX, float screenY)` -- called on mouse button press
- `onMouseUp(ScriptContext& ctx, int button, float screenX, float screenY)` -- called on mouse button release
- `onMouseMove(ScriptContext& ctx, float screenX, float screenY)` -- called on mouse movement

`ScriptContext` provides direct access to the engine's ECS:

| Member              | Type                | Description                        |
|---------------------|---------------------|------------------------------------|
| `entityId`          | `Entity::EntityId`  | The ECS entity identifier          |
| `componentManager`  | `ComponentManager&` | Direct access to all components    |
| `entityManager`     | `EntityManager&`    | Direct access to entity management |
| `viewportWidth`     | `float`             | Current viewport width in pixels   |
| `viewportHeight`    | `float`             | Current viewport height in pixels  |

Scripts can read and write any component through `ctx.componentManager`.

### Convenience Header

Include `engine/hades.hpp` for a single header that provides all script-facing
types: the base class, registration macro, all component types, math types, and
key constants. This enables VS Code autocomplete for the full API.

### Key Constants

Scripts cannot include SDL headers directly. Use the constants from
`engine/runtime/hades_keycodes.hpp` (included by `engine/hades.hpp`):

- `HADES_KEY_LEFT`, `HADES_KEY_RIGHT`, `HADES_KEY_UP`, `HADES_KEY_DOWN`
- `HADES_KEY_SPACE`, `HADES_KEY_RETURN`, `HADES_KEY_ESCAPE`
- `HADES_KEY_A` through `HADES_KEY_Z`
- `HADES_MOUSE_LEFT`, `HADES_MOUSE_RIGHT`, `HADES_MOUSE_MIDDLE`

### Example

```cpp
#include "engine/hades.hpp"

class MoveAlongX : public hades::HadesScript
{
public:
  void onUpdate(hades::ScriptContext& ctx, float deltaTime) override
  {
    auto& pos = ctx.componentManager.getComponent<hades::PositionComponent3D>(ctx.entityId);
    pos.x += 1.0f * deltaTime;
  }

  void onKeyDown(hades::ScriptContext& ctx, int keyCode) override
  {
    if (keyCode == 32) // Space
    {
      auto& pos = ctx.componentManager.getComponent<hades::PositionComponent3D>(ctx.entityId);
      pos.y += 1.0f;
    }
  }
};

HADES_REGISTER_SCRIPT(MoveAlongX)
```

## Observations API

Scripts can expose runtime values via `HadesAPI::observe()`:

```cpp
hades::HadesAPI::observe("score", 42);
hades::HadesAPI::observe("health", 100.0f);
hades::HadesAPI::observe("name", std::string("player"));
```

Observations are collected as JSON via `ScriptRuntime::collect_observations()`.

## World Loading API

Scripts can request loading a different world at runtime:

```cpp
hades::HadesAPI::loadWorld("World2");
```

The load is deferred -- the engine processes it after the current frame's script
update completes. The sequence is: stop all scripts, destroy the current world,
load the new world from `.hades/worlds/<name>.json`, and restart scripts in the
new world.

## Raycasting API

`HadesAPI` provides utilities for screen-to-world raycasting:

```cpp
HadesAPI::Ray ray = hades::HadesAPI::screenToWorldRay(
    screenX, screenY,
    ctx.viewportWidth, ctx.viewportHeight,
    cameraPosition, viewMatrix, projMatrix);

float distance = hades::HadesAPI::rayDistanceToPoint(ray, entityPosition);
```

This enables click-on-entity detection without requiring a physics engine
raycast.

## What Happens On Play

When play mode starts, the engine runs through these steps in
`ScriptRuntime::start()` (`src/engine/runtime/script_runtime.cpp`):

1. **Entity collection** (`collect_scripted_entities`) -- every entity in the
   active world with an enabled `ScriptComponent` attachment is gathered.
2. **Source file resolution** -- each attachment's `.cpp` path is resolved
   relative to the workspace root. The file must exist and have the `.cpp`
   extension.
3. **Macro scanning** -- source files are scanned for `HADES_REGISTER_SCRIPT`
   macros to discover registered class names.
4. **Registry generation** -- a `script_registry.cpp` file is generated with
   `extern "C"` factory functions and a lookup table.
5. **Compilation** -- the C++ compiler (detected at CMake configure time) is
   invoked to build all user source files plus the registry into a shared
   library (`.dylib`/`.so`/`.dll`).
6. **Loading** -- the shared library is loaded via `dlopen`/`LoadLibrary` and
   the factory table is resolved via `dlsym`/`GetProcAddress`.
7. **Instantiation** -- for each scripted entity, the engine creates script
   instances via the factory table and calls `onStart()`.

After initialization, every frame during play mode:

1. The engine calls `onUpdate()` on every script instance, passing a
   `ScriptContext` with direct references to the engine's `ComponentManager`
   and `EntityManager`.
2. SDL key down/up and mouse events are forwarded to the corresponding callbacks.
3. Scripts read and write components directly -- no marshaling or data copying.

When play stops, all script instances are destroyed and the shared library is
unloaded.

## Background Compilation

The editor compiles workspace scripts in the background (outside of play mode)
to show compile status in the inspector. This uses `ScriptRuntime::compile()`
which invokes the compiler but does not load the resulting library.

## Debugging Script Errors

All script-related errors are routed to the **Debug Console** panel
(**Windows > Debug Console**). Common errors:

| Error | Cause |
|-------|-------|
| `Script compilation failed` | The C++ compiler reported errors. Check your script source. |
| `Script file does not exist` | The `.cpp` path in the attachment does not point to an existing file. |
| `No HADES_REGISTER_SCRIPT macros found` | Your scripts must use `HADES_REGISTER_SCRIPT(ClassName)`. |
| `Script class not found` | The class name in the attachment doesn't match any registered class. |
| `Failed to load script library` | The shared library could not be loaded. Check compiler output. |
| `Failed to launch compiler` | The C++ compiler path from CMake is invalid. Reconfigure CMake. |

## Requirements

- **C++ compiler**: detected automatically at CMake configure time. The same
  compiler used to build the engine is used to compile scripts.
- Scripts are compiled with `-std=c++20` (or `/std:c++20` on MSVC).

## Key Source Files

| File | Purpose |
|------|---------|
| `src/engine/hades.hpp` | Convenience header including all script-facing types |
| `src/engine/runtime/hades_script.hpp` | `HadesScript` base class, `ScriptContext`, `HadesAPI` |
| `src/engine/runtime/hades_script_registration.hpp` | `HADES_REGISTER_SCRIPT` macro and factory types |
| `src/engine/runtime/hades_keycodes.hpp` | Key and mouse button constants |
| `src/engine/runtime/script_runtime.cpp` | Script compilation, loading, and lifecycle |
| `src/engine/runtime/script_compiler.cpp` | Shared library compilation |
| `src/engine/runtime/script_loader.cpp` | Dynamic library loading |
| `src/engine/runtime/subprocess.hpp` | Process spawning for compiler invocation |
| `src/engine/components/script_component.hpp` | `ScriptComponent` with attachment list |
| `src/editor/script_analysis.cpp` | Parses `HADES_REGISTER_SCRIPT` macros for editor dropdowns |

## Current Limitations

- Compilation happens when play starts; there is no hot reload
- A crash in script code will crash the editor (no sandboxing)
- Only one world is active during play mode
