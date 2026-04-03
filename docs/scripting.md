<!-- Generated from SCRIPTS.md by scripts/mkdocs_hooks.py. Do not edit directly. -->

# C# Scripting

Hades supports attaching C# scripts to entities through a `Script` component in
the editor. Scripts are compiled when play mode starts, then executed in-process
via the embedded .NET runtime (CoreCLR) using the hostfxr hosting API.

## Editor Workflow

1. Select an entity in the editor.
2. In the `Components` panel, click `Add Script Component`.
3. Add one or more script attachments to the entity.
4. For each attachment set:
   - **Script Path**: path to a `.cs` file (relative to the workspace root)
   - **Class**: select the `HadesScript`-derived class to instantiate from the
     dropdown populated from that file. The editor auto-selects the first class
     it finds when you choose a script.
   - **Enabled**: whether that attachment should run
5. Press **Game > Play** (or use the menu).

The Debug Console panel opens automatically when play starts. If scripts fail to
compile or load, the error appears there in red.

## Script Shape

Scripts must derive from `Hades.Scripting.HadesScript`. The host exposes two
virtual entry points:

- `OnStart(EntityContext context)` -- called once when play starts
- `OnUpdate(EntityContext context, float deltaTime)` -- called every frame

`EntityContext` exposes:

| Property   | Type     | Access    | Description                        |
|------------|----------|-----------|------------------------------------|
| `EntityId` | `uint`   | read-only | The ECS entity identifier          |
| `Name`     | `string` | read-only | The entity's `NameComponent` value |
| `Position` | `Vector3`| read/write| The entity's 3D position           |

`Vector3` is a simple struct with `X`, `Y`, `Z` float fields.

### Example

```csharp
using Hades.Scripting;

public sealed class MoveAlongX : HadesScript
{
    public override void OnUpdate(EntityContext context, float deltaTime)
    {
        var position = context.Position;
        position.X += 1.0f * deltaTime;
        context.Position = position;
    }
}
```

## What Happens On Play

When play mode starts, the engine runs through these steps in
`ScriptRuntime::start()` (`src/engine/runtime/script_runtime.cpp`):

1. **Scene validation** -- a default world and exactly one main camera must
   exist. Errors are reported to the Debug Console.
2. **Entity collection** (`collect_scripted_entities`) -- every entity in the
   active world with an enabled `ScriptComponent` attachment is gathered. Each
   scripted entity must also have a `PositionComponent3D`.
3. **Source file resolution** -- each attachment's `.cs` path is resolved
   relative to the workspace root. The file must exist and have the `.cs`
   extension.
4. **SDK detection** -- the engine runs `dotnet --version` to verify that a
   .NET SDK >= 7.0 is available.
5. **Project generation** -- a temporary directory is created under the system
   temp folder (`hades-script-host-<N>/`). Inside it the engine writes:
   - `HostProgram.cs` -- the generated managed host (see
     [Generated Host](#generated-host) below)
   - `HadesScriptHost.csproj` -- a library project referencing the host source
     and every user `.cs` file
6. **Compilation** -- `dotnet build` is invoked on the generated project.
   Compilation errors (including a hint when Java-style `System.out.println` is
   detected) are reported to the Debug Console.
7. **Runtime config** -- if `dotnet build` did not emit a
   `runtimeconfig.json`, the engine writes one matching the detected target
   framework (e.g. `net9.0`).
8. **CLR initialization** (`ClrHost::initialize`) -- the engine locates
   `libhostfxr` (see [Runtime Location](#runtime-location)), loads it via
   `dlopen`/`LoadLibrary`, and calls `hostfxr_initialize_for_runtime_config`
   to create a host context.
9. **Entry point resolution** (`ClrHost::get_managed_function`) -- three
   managed methods are resolved as native function pointers using the
   `[UnmanagedCallersOnly]` convention:
   - `ScriptHost.LoadScene`
   - `ScriptHost.UpdateFrame`
   - `ScriptHost.Shutdown`
10. **Scene loading** -- `LoadScene` is called with packed interop structs
    containing entity IDs, names, positions, and class names. The managed host
    resolves each class via reflection, verifies it derives from
    `HadesScript`, creates an instance, and calls `OnStart`.

After initialization, every frame during play mode:

1. The engine packs current entity positions into a blittable
   `InteropEntityPosition` array and calls `UpdateFrame`.
2. The managed host calls `OnUpdate` on every script instance.
3. Updated positions are written to an output buffer and read back into each
   entity's `PositionComponent3D`.
4. If the managed host throws, play mode stops and the exception message is
   shown in the Debug Console.

When play stops (or the user clicks **Game > Stop**), `Shutdown` is called,
the CLR host context is closed, and all script state is discarded.

## Generated Host

The engine generates a complete C# source file (`HostProgram.cs`) embedded as a
string literal in `render_host_runtime_source()`. Key types in the generated
source:

| Type              | Visibility | Purpose                                      |
|-------------------|-----------|-----------------------------------------------|
| `Vector3`         | public    | Position struct with `X`, `Y`, `Z` fields     |
| `EntityContext`   | public    | Per-entity state passed to scripts            |
| `HadesScript`     | public    | Abstract base class for user scripts          |
| `ScriptHost`      | public    | Static class with `[UnmanagedCallersOnly]` entry points |

The `ScriptHost` class must be **public** because the .NET hosting API
(`load_assembly_and_get_function_pointer`) resolves types by name from outside
the assembly and cannot access internal types.

Interop structs (`InteropEntityData`, `InteropEntityPosition`,
`InteropString`, `InteropLoadResult`, `InteropUpdateResult`) use
`[StructLayout(LayoutKind.Sequential, Pack = 1)]` to match the C++ side
exactly.

## Runtime Location

The engine finds the .NET runtime by searching for `libhostfxr` in order:

1. The path configured at CMake time (`HADES_DOTNET_ROOT`)
2. The `DOTNET_ROOT` environment variable
3. Standard install locations:
   - macOS: `/usr/local/share/dotnet`
   - Linux: `/usr/share/dotnet`, `/usr/local/share/dotnet`, `/usr/lib/dotnet`
   - Windows: `C:\Program Files\dotnet`, `C:\Program Files (x86)\dotnet`

Within each root it scans `host/fxr/<version>/` and picks the highest version.

The `dotnet` CLI is needed only for **compilation** (`dotnet build`). The
compiled assembly is loaded and executed in-process via hostfxr rather than
spawning `dotnet HadesScriptHost.dll`.

## Background Compilation

The editor also compiles workspace scripts in the background (outside of play
mode) to show compile status in the inspector. This uses
`ScriptRuntime::compile()` which calls `dotnet build` the same way but does
not initialize the CLR or load the assembly. Background compile results use
request IDs so that stale results from a previous compile are discarded if the
scripts changed again before the compile finished.

## Debugging Script Errors

All script-related errors are routed to the **Debug Console** panel
(**Windows > Debug Console**). The console opens automatically when play starts
and whenever an error is logged. Common errors and what they mean:

| Error | Cause |
|-------|-------|
| `dotnet SDK is required` | No `dotnet` command found on `PATH` or at the CMake-configured path. Install .NET SDK 7.0+. |
| `C# script compilation failed` | The `dotnet build` failed. The build output is included in the message. Check for C# syntax errors in your scripts. |
| `Script file does not exist` | The `.cs` path in the script attachment does not point to an existing file. |
| `Scripted entities currently require a PositionComponent3D` | Add a position component to any entity that has scripts. |
| `Failed to initialize the .NET runtime context` | The hostfxr library was found but the CLR could not start. Check that the .NET runtime version matches the SDK version. |
| `Failed to locate managed method ... (error 0x80070057)` | The CLR loaded the assembly but could not find the entry point. This usually means the `ScriptHost` type is not public, or there is a target framework mismatch between the SDK and the installed runtime. |
| `Unable to locate script class '...'` | The class name in the script attachment does not match any type in the compiled assembly. Check spelling and namespace. |
| `Type '...' must derive from Hades.Scripting.HadesScript` | The specified class exists but does not extend the required base class. |
| `A managed script threw an exception during OnUpdate` | A runtime exception occurred in user script code. The exception message is included. |
| `Play mode requires at least one camera` | Add a camera entity to the active world. |
| `Play mode requires one camera ... marked as Main Camera` | Select a camera entity and tick the "Main Camera" checkbox in the inspector. |

## Cross-Platform Strategy

The scripting path is cross-platform:

- macOS, Linux, and Windows all use the same high-level flow
- Script compilation happens at play start via `dotnet build`
- The hostfxr library is loaded dynamically (`dlopen`/`LoadLibrary`)
- No platform-specific C# runtime integration is needed inside the renderer or
  ECS code

## Requirements

- **.NET SDK 7.0+**: required for compilation (uses `[UnmanagedCallersOnly]`)
- **.NET Runtime**: must be installed for hostfxr to load the CLR in-process
- The SDK major version determines the target framework (e.g. SDK 9 targets
  `net9.0`)

## Key Source Files

| File | Purpose |
|------|---------|
| `src/engine/runtime/script_runtime.cpp` | Script compilation, CLR hosting, interop, and the generated C# host source |
| `src/engine/runtime/clr_host.cpp` | hostfxr loading and managed function resolution |
| `src/engine/runtime/clr_host.hpp` | `ClrHost` class interface |
| `src/engine/runtime/main_camera_selection.hpp` | Camera validation before play |
| `src/engine/runtime/subprocess.hpp` | Process spawning for `dotnet build` |
| `src/engine/runtime/dotnet_config.hpp` | CMake-configured dotnet path |
| `src/engine/components/script_component.hpp` | `ScriptComponent` with attachment list |
| `src/editor/editor_entities.cpp` | Play mode start/stop logic (`start_play_mode`) |
| `src/editor/window_manager.cpp` | Play window management and runtime fault handling |

## Current Limitations

- Scripts can only drive entities that have `PositionComponent3D`
- Script output is limited to writing back position values
- Compilation happens when play starts; there is no hot reload
- Only one world is active during play mode
- Scripts do not have access to other components (audio, text, etc.)
