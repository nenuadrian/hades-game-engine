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
- `onMessage(ScriptContext& ctx, const std::string& name, const ScriptValue& value)` -- called
  by the Blueprint nodes `Send Script Message` and `Call Script Function`; the returned
  `ScriptValue` is what the Blueprint reads back

`ScriptContext` provides direct access to the engine's ECS:

| Member              | Type                | Description                        |
|---------------------|---------------------|------------------------------------|
| `entityId`          | `Entity::EntityId`  | The ECS entity identifier          |
| `componentManager`  | `ComponentManager&` | Direct access to all components    |
| `entityManager`     | `EntityManager&`    | Direct access to entity management |
| `viewportWidth`     | `float`             | Current viewport width in pixels   |
| `viewportHeight`    | `float`             | Current viewport height in pixels  |

Scripts can read and write any component through `ctx.componentManager`.

Skeletal animation has its own facade, `hades::Animation`, rather than being driven through
components -- see [Animation](animation.md) for the clip, parameter and event API.

Blueprints have one too, `hades::Blueprints` -- see [Blueprints](#blueprints) below.

Game UI (HUDs, menus, world-space health bars) is driven through `hades::UI` --
see [Game UI](ui.md). Clicks on screen-space widgets arrive as
`onMessage("ui.clicked", widgetId)`.

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

## Blueprints

Scripts and [Blueprints](blueprints.md) run side by side on the same entities, and reach each
other in both directions.

### Calling a Blueprint from a script

`hades::Blueprints` (`src/engine/blueprint/script_blueprint.hpp`, already included by
`engine/hades.hpp`) is the whole surface. Every call is a no-op, or returns a default, when no
Blueprint runtime is playing or the entity has no Blueprint, so scripts do not have to guard.

```cpp
#include "engine/hades.hpp"

class Player : public hades::HadesScript
{
public:
  void onStart(hades::ScriptContext& ctx) override
  {
    // Raise a Custom Event named "Spawned" on this entity's graphs.
    hades::Blueprints::sendEvent(ctx.entityId, "Spawned");
  }

  void onUpdate(hades::ScriptContext& ctx, float deltaTime) override
  {
    // Push state the graph can branch on.
    hades::Blueprints::setFloat(ctx.entityId, "Health", health_);

    // ...and read back whatever the graph decided.
    if (hades::Blueprints::getBool(ctx.entityId, "Stunned"))
    {
      return;
    }
  }

  void takeDamage(hades::ScriptContext& ctx, float amount, hades::Entity::EntityId source)
  {
    health_ -= amount;
    // Payload values fill the Custom Event's parameter pins, positionally.
    hades::Blueprints::sendEvent(
        ctx.entityId, "Damaged", {amount, hades::ScriptValue::fromEntity(source)});
  }

private:
  float health_ = 100.0f;
};

HADES_REGISTER_SCRIPT(Player)
```

| Call | What it does |
|------|--------------|
| `sendEvent(entity, name, payload)` | Raises the Custom Event `name` on every Blueprint attached to `entity` |
| `broadcastEvent(name, payload)` | Same, for every Blueprint instance in the world |
| `getFloat` / `getInt` / `getBool` / `getString` / `getVector` | Reads a graph variable |
| `setFloat` / `setInt` / `setBool` / `setString` / `setVector` | Writes one, coerced to its declared type; false when no graph declares it |
| `getVariable` / `setVariable` | The same, untyped, through `ScriptValue` |
| `hasVariable(entity, name)` | Whether any Blueprint on the entity declares it |
| `isRunning()` / `has(entity)` / `count(entity)` | Whether there is anything to talk to |

Give a Custom Event parameters in the Blueprint editor's details panel and they arrive as the
event's data output pins; extra payload values are dropped, missing ones keep their previous
value. The event name is the one typed on the node, without the `custom:` prefix the runtime
uses internally.

### Calling a script from a Blueprint

The Blueprint palette's **Scripts** category holds three nodes -- `Send Script Message`,
`Call Script Function` and `Broadcast Script Message` -- and all three land on `onMessage`:

```cpp
hades::ScriptValue onMessage(
    hades::ScriptContext& ctx, const std::string& name, const hades::ScriptValue& value) override
{
  if (name == "GetHealth")
  {
    return hades::ScriptValue(health_);  // Call Script Function reads this back
  }
  if (name == "Heal")
  {
    health_ = std::min(100.0f, health_ + value.asFloat());
    return hades::ScriptValue(true);     // any non-empty value means "handled"
  }
  return hades::ScriptValue();           // not handled
}
```

Returning the default `ScriptValue()` means the script did not handle the message: the node's
`Handled` pin goes false, and when an entity carries several scripts the first non-empty answer
wins.

### ScriptValue

Payloads and replies cross as `hades::ScriptValue`, a small tagged value carrying a bool, int,
float, string, `math::Vec3` or entity id. It converts implicitly from the plain C++ types, which
is what lets a payload be written as a braced list, and reads back leniently through `asFloat()`,
`asInt()`, `asString()` and friends -- the same coercion rules Blueprint pins use. Entity ids need
the named factory, `ScriptValue::fromEntity(id)`, so unsigned integers do not silently become
entities.

### Ordering

Scripts update before Blueprints in both the editor and the standalone runtime, which sets the
timing in each direction:

- **Blueprint to script** is synchronous. A `Send Script Message` executed during a graph's Tick
  calls `onMessage` before the node's `then` pin fires.
- **Script to Blueprint** is queued, and drained at the top of the Blueprint runtime's update. A
  `sendEvent` from `onUpdate` is therefore handled later in the same frame, ahead of `Event Tick`.
  A `sendEvent` from `onStart` is handled on the first frame, after `Event BeginPlay`.

Queueing is what makes the round trip safe: a graph that calls a script that sends an event back
never re-enters the VM mid-execution. Events queued from inside `onMessage` are picked up by the
same drain, up to eight rounds per frame, after which the remainder waits for the next frame.
Anything still queued when play stops is discarded.

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
| `src/engine/runtime/hades_value.hpp` | `ScriptValue`, the script/Blueprint boundary type |
| `src/engine/blueprint/script_blueprint.hpp` | The `hades::Blueprints` facade |
| `src/engine/runtime/script_blueprint_nodes.cpp` | The `script.*` Blueprint node category |
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

## Neural Scripts

Hades integrates the `hades-neural-engine` (HNE) for reinforcement learning.
Scripts can opt into RL by subclassing `hades::NeuralScript` instead of
`hades::HadesScript`. A neural script declares its observation and action
spaces, reads observations from the ECS, applies actions, emits a per-step
reward, and signals episode termination. The editor's `Window > Neural Training`
panel trains a PPO policy against the live world; exported policies plug back
into the attachment so the script can run inference at play time.

### Include

```cpp
#include "engine/hades.hpp"
#include "engine/hades_neural.hpp"   // NeuralScript + normalization helpers
```

`engine/hades_neural.hpp` pulls in the `NeuralScript` base, the normalization
helpers (`hades::normalize`, `hades::unnormalize`, `hades::write_obs`,
`hades::write_vec3`), and the HNE core types (`hades::BoxSpace`,
`hades::DiscreteSpace`, `hades::Tensor`, `hades::Action`, ...). For direct HNE
use you can `#include <hne/hne.hpp>`.

### Virtual Hooks

| Method | Purpose |
|--------|---------|
| `hne::SpaceSpec observationSpace() const` | Declares the observation shape (usually `BoxSpace`). Must be stable across calls. |
| `hne::SpaceSpec actionSpace() const` | Declares the action space (`DiscreteSpace`, `MultiDiscreteSpace`, or `BoxSpace`). |
| `void readObservation(ScriptContext&, hne::Tensor& out)` | Fills the observation tensor from the current ECS state. |
| `void applyAction(ScriptContext&, const hne::Action&, float dt)` | Applies the policy's / training step's action to the world. |
| `float computeReward(ScriptContext&, float dt)` | Per-step scalar reward. Default returns `0`. |
| `bool isDone(ScriptContext&)` | `true` ends the episode. Default returns `false`. |
| `void onReset(ScriptContext&)` | Called at the start of each training episode. Default does nothing. |

`onStart` / `onUpdate` still exist but are **not** called in `Inference` or
`TrainingOwned` mode; use `applyAction` for per-step work instead.

### Dispatch Modes

When play starts, `ScriptRuntime` assigns each neural attachment one of three
modes:

- `Legacy` -- attachment uses plain `HadesScript`; `onUpdate` runs each frame.
- `Inference` -- attachment is a `NeuralScript` with a non-empty `Model Path`.
  The policy is loaded via `PolicyRegistry::get_validated(...)` with the
  declared obs/action spaces; each frame: `readObservation` ->
  `policy->evaluate` -> `applyAction`.
- `TrainingOwned` -- attachment is a `NeuralScript` with an empty `Model Path`
  inside a training host. The training loop drives the script; per-frame
  `update` is skipped.

A `NeuralScript` with an empty `Model Path` outside a training host is a
**loud failure**: `start()` returns `false` and the Debug Console shows an
error. A mismatched policy file (wrong obs shape, wrong action space) is the
same kind of loud failure -- the engine never silently falls back to
`onUpdate`.

### Linking a Trained Policy

Each `ScriptAttachment` has a `Model Path` string (relative to the workspace
root). Set it from the inspector or edit the world JSON directly, e.g.:

```
.hades/policies/pole_v1/policy.pt
```

Empty means "legacy `onUpdate`, or training-owned when inside a training host".

### Training via the Editor

1. Open `Window > Neural Training`.
2. Enter a **Run Name** (a subdirectory will be created under
   `.hades/policies/<runName>/`).
3. Pick the **World** containing your training subject.
4. Fill in **Entity** and **Attachment Class** for the entity carrying the
   `NeuralScript`.
5. Adjust PPO hyperparameters in the config editor (rollout length, entropy
   coefficient, learning rate, etc.).
6. Click **Start**. The panel streams reward curves and loss/entropy metrics
   live.
7. Click **Export Policy**. This writes:
   - `.hades/policies/<run>/policy.pt` -- the TorchScript policy.
   - `.hades/policies/<run>/policy.meta.json` -- a sidecar with the obs/action
     specs, trainer config, run name, subject entity, and export timestamp.
   - `.hades/policies/<run>/trainer.ckpt` -- trainer checkpoint (for resume).
8. Paste `.hades/policies/<run>/policy.pt` into the attachment's **Model Path**
   and press `Play`. The script now runs in `Inference` mode.

### Worked Example -- PoleBalance

`tests/test_project/PoleBalance.cpp` is a classic cart-pole benchmark
(`BoxSpace({4})` observation, `DiscreteSpace(2)` action):

```cpp
#include "engine/hades.hpp"
#include "engine/hades_neural.hpp"

class PoleBalance : public hades::NeuralScript
{
public:
  hades::SpaceSpec observationSpace() const override
  {
    hades::BoxSpace box;
    box.shape = {4};
    box.low   = {-1.0f, -1.0f, -1.0f, -1.0f};
    box.high  = { 1.0f,  1.0f,  1.0f,  1.0f};
    return box;
  }

  hades::SpaceSpec actionSpace() const override
  {
    hades::DiscreteSpace d;
    d.n = 2;
    return d;
  }

  void readObservation(hades::ScriptContext&, hades::Tensor& out) override
  {
    hades::write_obs(out, 0, hades::normalize(cartX_,   -2.4f, 2.4f));
    hades::write_obs(out, 1, hades::normalize(cartVel_, -3.0f, 3.0f));
    hades::write_obs(out, 2, hades::normalize(angle_,   -0.21f, 0.21f));
    hades::write_obs(out, 3, hades::normalize(angVel_,  -4.0f, 4.0f));
  }

  void applyAction(hades::ScriptContext& ctx,
                   const hades::Action& action, float dt) override
  {
    const int dir = action.as_discrete() == 0 ? -1 : 1;
    // ... cart-pole dynamics update cartX_, angle_, etc.
    auto& pos = ctx.componentManager
                   .getComponent<hades::PositionComponent3D>(ctx.entityId);
    pos.x = cartX_;
  }

  float computeReward(hades::ScriptContext&, float) override { return 1.0f; }

  bool isDone(hades::ScriptContext&) override
  {
    return std::abs(cartX_) > 2.4f || std::abs(angle_) > 0.21f;
  }

  void onReset(hades::ScriptContext& ctx) override
  {
    cartX_ = cartVel_ = angVel_ = 0.0f;
    angle_ = 0.02f;
    auto& pos = ctx.componentManager
                   .getComponent<hades::PositionComponent3D>(ctx.entityId);
    pos.x = 0.0f;
  }

private:
  float cartX_ = 0, cartVel_ = 0, angle_ = 0.02f, angVel_ = 0;
};

HADES_REGISTER_SCRIPT(PoleBalance)
```

Train with the default PPO config for ~200k steps, export, set `Model Path`
on the Cart entity, and hit `Play`: the cart self-balances.

### Shared Normalization Helpers

All neural scripts should use the helpers from `engine/hades_neural.hpp` so
observations use a consistent `[-1, 1]` scale across scripts and trained
policies remain portable:

- `hades::normalize(v, lo, hi)` -- maps `[lo, hi]` to `[-1, 1]` (clamped).
- `hades::unnormalize(v, lo, hi)` -- inverse.
- `hades::write_obs(tensor, i, v)` -- bounds-checked tensor write.
- `hades::write_vec3(tensor, base, vec3)` -- writes three consecutive floats.

### Advanced -- Direct HNE Inference

For custom inference setups (e.g. an ensemble or an externally-trained policy)
you can drop into HNE directly:

```cpp
#include <hne/hne.hpp>

hne::InferenceRuntime rt("path/to/policy.pt");
hne::Tensor obs; obs.data.assign(4, 0.0f); obs.shape = {4};
hne::Action a = rt.evaluate(obs, /*deterministic=*/true);
```

`PolicyRegistry` (`src/engine/runtime/policy_registry.hpp`) is the
engine-managed, thread-safe cache used by `Inference` mode; external code is
free to hold its own `InferenceRuntime` directly.

### Neural Key Source Files

| File | Purpose |
|------|---------|
| `src/engine/hades_neural.hpp` | Convenience header (base class + helpers + HNE types re-exports) |
| `src/engine/runtime/hades_neural_script.hpp` | `NeuralScript` base class |
| `src/engine/runtime/hades_neural_api.hpp` | `normalize` / `write_obs` / `write_vec3` helpers |
| `src/engine/runtime/policy_registry.hpp` | Lazy thread-safe policy cache with spec validation |
| `src/engine/training/hades_script_env.hpp` | `hne::IEnvironment` adapter used by the training panel |
| `src/editor/plugins/neural_training_plugin.cpp` | `Window > Neural Training` panel |
| `tests/test_project/PoleBalance.cpp` | Worked cart-pole example |
