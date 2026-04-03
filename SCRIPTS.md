# C# Scripting

Hades supports attaching C# scripts to entities through a `Script` component in
the editor. Scripts are compiled when play mode starts, then executed in-process
via the embedded .NET runtime (CoreCLR) using the hostfxr hosting API.

## Editor Workflow

1. Select an entity in the editor.
2. In the `Components` panel, click `Add Script Component`.
3. Add one or more script components to the entity.
4. Expand each script component and set:
   - `Script Path`: path to a `.cs` file
   - `Class Name`: the C# class to instantiate
   - `Enabled`: whether that script component should run
5. Press `Play`.

If `Class Name` is left empty, the editor can derive it from the file name. If
the class name is ambiguous because multiple types share it, use the full
namespace-qualified name.

## Script Shape

Scripts must derive from `Hades.Scripting.HadesScript`. The host exposes two
entry points:

- `OnStart(EntityContext context)`
- `OnUpdate(EntityContext context, float deltaTime)`

`EntityContext` currently exposes:

- `EntityId`
- `Name`
- `Position`

Example:

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

When play mode starts, the engine:

1. Validates the scene as usual, including main camera selection.
2. Collects all entities with a `Script` component.
3. Filters to enabled attachments only.
4. Validates each attachment:
   - the path is present
   - the file exists
   - the file extension is `.cs`
   - the entity has `PositionComponent3D`
5. Checks for a local `dotnet` SDK (required for compilation).
6. Generates a temporary C# project for a managed host.
7. Adds the generated host source plus every attached script file to that
   project.
8. Builds the host with `dotnet build`.
9. Loads the .NET runtime in-process via the hostfxr hosting API.
10. Loads the compiled assembly and resolves managed entry points as native
    function pointers using `[UnmanagedCallersOnly]`.
11. Calls `LoadScene` which resolves each class, creates instances, and calls
    `OnStart`.

After that, every frame during play mode:

1. The engine passes current entity positions to the managed `UpdateFrame`
   function via blittable structs (no serialization overhead).
2. The managed host updates each script instance via `OnUpdate`.
3. The managed host writes the resulting positions back to the output buffer.
4. The engine reads those positions back into each entity's
   `PositionComponent3D`.

If the managed host fails, play mode stops and the editor shows the error.

## Runtime Architecture

The engine embeds the .NET runtime directly using the official CoreCLR hosting
API (`hostfxr`). This means:

- **No subprocess**: scripts run in the engine process, eliminating IPC overhead
- **Direct function calls**: C++ calls managed methods via native function
  pointers, not text serialization
- **Blittable data exchange**: entity positions are passed as packed structs
  through shared memory, not parsed from text

The engine locates the .NET runtime by searching:

- The `DOTNET_ROOT` environment variable (if set)
- Standard install locations (`/usr/local/share/dotnet` on macOS,
  `/usr/share/dotnet` on Linux, `C:\Program Files\dotnet` on Windows)

The `dotnet` CLI is still required for **compilation** (`dotnet build`), but the
compiled assembly is loaded and executed in-process rather than via
`dotnet HadesScriptHost.dll`.

When CMake can resolve `dotnet` during configure, the engine stores that
absolute SDK path and uses it at runtime before falling back to `PATH`. If the
SDK is installed or moved after the initial configure step, rerun CMake so the
captured path stays in sync. You can also override discovery with
`-DHADES_DOTNET_EXECUTABLE=/absolute/path/to/dotnet` and, if needed,
`-DHADES_DOTNET_ROOT=/absolute/path/to/dotnet/root`.

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

## Current Limitations

The current implementation is intentionally narrow:

- scripts can only drive entities that have `PositionComponent3D`
- script output is limited to writing back position values
- compilation happens when play starts; there is no hot reload
