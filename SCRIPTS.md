# C# Scripting

Hades supports attaching C# scripts to entities through a `Script` component in
the editor. Scripts are compiled when play mode starts, then executed through a
small managed host process that exchanges entity state with the native engine.

## Editor Workflow

1. Select an entity in the editor.
2. In the `Properties` panel, click `Add Script Component`.
3. Add one or more script attachments.
4. For each attachment, set:
   - `Script Path`: path to a `.cs` file
   - `Class Name`: the C# class to instantiate
   - `Enabled`: whether that attachment should run
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
5. Checks for a local `dotnet` SDK.
6. Generates a temporary C# project for a managed host.
7. Adds the generated host source plus every attached script file to that
   project.
8. Builds the host with `dotnet build`.
9. Starts the compiled host with `dotnet HadesScriptHost.dll`.
10. Sends the initial entity list, names, positions, and script class names to
    the host.
11. The host resolves each class, creates instances, and calls `OnStart`.

After that, every frame during play mode:

1. The native engine sends the current entity positions and `deltaTime`.
2. The managed host updates each script instance.
3. The managed host returns the resulting positions.
4. The engine writes those positions back into each entity's
   `PositionComponent3D`.

If the managed host fails, play mode stops and the editor shows the error.

## Cross-Platform Strategy

The scripting path is cross-platform by relying on the `dotnet` CLI instead of
embedding a CLR directly into the C++ engine.

That means:

- macOS, Linux, and Windows all use the same high-level flow
- script compilation happens at play start
- the engine launches a separate managed process for script execution
- no platform-specific C# runtime integration is needed inside the renderer or
  ECS code

## Current Limitations

The current implementation is intentionally narrow:

- scripts can only drive entities that have `PositionComponent3D`
- script output is limited to writing back position values
- compilation happens when play starts; there is no hot reload
- scripts are loaded from file paths relative to the engine process working
  directory unless absolute paths are used
- a local `dotnet` SDK is required to compile and run scripts

## Relevant Engine Files

- `src/engine/components/script_component.hpp`
- `src/engine/runtime/script_runtime.hpp`
- `src/engine/runtime/script_runtime.cpp`
- `src/engine/runtime/subprocess.hpp`
- `src/engine/runtime/subprocess.cpp`
- `src/editor/editor.cpp`
- `src/editor/window_manager.cpp`
