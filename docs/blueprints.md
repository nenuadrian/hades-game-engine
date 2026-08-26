# Blueprints

Blueprints are Hades' visual scripting system. A Blueprint is a graph of nodes
wired together by **execution** wires (which decide *when* things run) and
**data** wires (which decide *what* values they run on) — the same model Unreal
Engine popularised. Graphs are authored in the editor, compiled to a flat
instruction form, and executed by a small VM alongside the C++ scripting
runtime.

## Concepts

| Concept | What it is |
|---------|------------|
| **Asset** | A `.hbp` file in the workspace. Holds the event graph, variables and user functions. |
| **Event graph** | The main graph. Entry points are event nodes (`Event BeginPlay`, `Event Tick`, …). |
| **Variables** | Named, typed values that persist for the lifetime of the instance. Exposed variables can be overridden per entity from the inspector. |
| **Functions** | Private sub-graphs with typed inputs and outputs, called from `Call Function`. |
| **Instance** | One entity running one Blueprint. Each instance has its own variables and node state. |

Attach a Blueprint to an entity by adding a **Blueprint** component in the
inspector and picking an asset. One entity can run several Blueprints at once,
and the same asset can be attached to any number of entities — it is parsed and
compiled once, then instanced.

## Pin types

| Type | Wire colour | Notes |
|------|-------------|-------|
| `exec` | white | Execution flow. Never carries a value. |
| `bool` | red | |
| `int` | teal | |
| `float` | green | |
| `string` | magenta | Every type converts to `string`. |
| `vector` | gold | Three floats. A scalar wired into a vector pin splats across all axes. |
| `entity` | blue | An entity handle. An unwired entity pin means "the entity this Blueprint is attached to". |
| `wildcard` | grey | Adopts the type of whatever it is wired to (`Select`, `To String`). |

Implicit conversions follow a fixed table: numeric widening is silent, lossy
conversions (`float → int`, `vector → string`) compile with a warning, and
anything genuinely incompatible (`string → vector`) is a compile error.

## Execution model

The compiler turns a graph into a `CompiledBlueprint`: a register file, a
literal pool, resolved execution targets, and a per-node list of the **pure**
nodes feeding it.

* **Exec nodes** sit on the execution chain. Each one returns which of its
  execution output pins the VM should follow next.
* **Pure nodes** have no execution pins. They are re-evaluated immediately
  before *every* exec node that consumes them, so a `Multiply` fed by a For Loop
  index recomputes on every iteration.
* **Loops and Sequence** push themselves onto a continuation stack. When the
  chain they started finishes, the VM re-enters them — that is how `Sequence`
  runs its pins strictly in order and how `For Loop` gets its next iteration.
* **`Delay`** suspends the chain. The pending continuation stack is stored with
  the latent action, so a `Delay` inside a loop body resumes the remaining
  iterations rather than losing them. Latent nodes are rejected inside functions.
* **Guards.** A single event dispatch has a node-execution budget and the call
  stack has a depth limit; exceeding either raises a runtime error and stops
  play mode instead of hanging the editor.

## Node library

| Category | Nodes |
|----------|-------|
| Events | BeginPlay, Tick, Key Down/Up, Mouse Down/Up/Move, Collision Begin/End, Custom Event |
| Flow Control | Branch, Sequence, For Loop, While Loop, Do Once, Gate, Flip Flop, Delay, Call Event, Stop Execution |
| Variables | Get, Set |
| Functions | Call Function (plus the Entry/Return nodes placed inside function graphs) |
| Math | Add/Subtract/Multiply/Divide (float and integer), Modulo, Negate, Absolute, Min, Max, Clamp, Lerp, Sin, Cos, Square Root, Power, Random Float/Integer |
| Vector | Make, Break, Add, Subtract, Scale, Dot, Cross, Length, Normalize, Distance, Lerp |
| Logic | AND, OR, XOR, NOT, comparisons, Equal (float/int/bool/string), Select |
| Conversion / String | To String, To Integer, To Float, To Boolean, Append, String Length |
| Entity | Self, Is Valid, Find Entity by Name, Get/Set Name, Get Parent, Get Child Count, Get Child, Destroy Entity |
| Transform | Get/Set Position, Add Offset, Get/Set Rotation (Euler degrees), Get/Set Scale |
| Physics | Add Force, Add Impulse, Set Linear Velocity |
| Audio | Play Sound, Stop Sound, Set Volume |
| Debug / World / Time | Print String, Observe, Load World, Get Delta Seconds, Get Time Seconds |
| Constants | Float / Integer / Boolean / String / Vector literals |
| Animation | Play / Play Once / Stop / Set Speed, Go To State, Get State, Get Time, Is Playing, Set/Get Float, Bool, Int and Trigger parameters, Animation Event Fired — see [animation](animation.md) |
| Scripts | Send Script Message, Call Script Function, Broadcast Script Message — see [scripting](scripting.md) |
| UI | Set Widget Text / Value / Visible / Color / Fill Color, Set Canvas Visible, Get Widget Value / Text, Widget Exists — see [Game UI](ui.md) |

`Observe` publishes into the same observation set that `hades::HadesAPI::observe`
writes to, so a Blueprint can feed the [REST API](api.md) and RL training loop
exactly like a C++ script can. `Load World` queues the same world switch that
`HadesAPI::loadWorld` does.

### Custom Event parameters

A Custom Event can declare a payload. Select the node and add parameters in the
details panel; each one becomes a data output pin on the event, and a matching
data input pin on every **Call Event** that targets it, so the two sides cannot
drift apart. The same pins receive the payload when a C++ script raises the
event with `hades::Blueprints::sendEvent`.

Values are coerced to the declared type on arrival, so a script that sends a
string into a `float` parameter gets the parsed number rather than a type error.

### Talking to C++ scripts

The **Scripts** category is the Blueprint half of the [script bridge](scripting.md#blueprints).
All three nodes land on `HadesScript::onMessage` for the target entity's
scripts:

| Node | What it does |
|------|--------------|
| **Send Script Message** | Calls `onMessage` on the target entity's scripts and moves on. Pins: target, name, value. |
| **Call Script Function** | Same, but reads the reply back. Outputs `Result` and `Handled`, which is false when no script answered. |
| **Broadcast Script Message** | Calls `onMessage` on every scripted entity in the world. |

The **Value** pin — and **Call Script Function**'s **Result** pin — take their
type from a combo in the details panel rather than from a wire, because a
message crossing into C++ gives the compiler nothing to infer from.

Dispatch is synchronous: scripts update before Blueprints, so a message sent
from a graph runs inside the same frame. The other direction is queued — see
[Scripting](scripting.md#blueprints) for the ordering rules.

## Editing

Open the panel from **Windows → Blueprint Editor**, or double-click a `.hbp` in
the workspace tree. New assets come from the toolbar's **New** button or from
**New Blueprint** in the workspace context menu.

| Action | Input |
|--------|-------|
| Pan | Scroll (two-finger drag on a trackpad), or `Space`/`Alt` + left-drag, or middle/right-drag |
| Zoom | `Ctrl`/`Cmd` + scroll, or the `-` / `+` buttons in the toolbar |
| Frame the whole graph | `F`, or the **Fit** button |
| Add node | Right-click empty canvas, or drop a wire on empty canvas |
| Node menu (delete, duplicate, break all links) | Right-click the node |
| Move a node | Drag it (drags the whole selection) |
| Connect | Drag from one pin to another |
| Break links on a pin | Right-click the pin |
| Select a wire | Click it |
| Box select | Left-drag on empty canvas |
| Duplicate | `Ctrl`/`Cmd` + `D` |
| Delete | `Delete` or `Backspace` |
| Save | `Ctrl`/`Cmd` + `S` |

Opening an asset frames its whole graph automatically — graphs routinely use
negative coordinates, so a view parked at the origin would start with most of the
nodes off-screen.

Events, variables and functions in the sidebar each have a right-click menu too,
so a graph can be pruned without hunting for the node on the canvas.

**Compile** validates the whole asset and lists errors and warnings underneath
the canvas; clicking a message jumps to the offending node. Play mode refuses to
start if any attached Blueprint has compile errors, and while play mode is
running, recently executed nodes and wires glow so you can watch the graph run.

## File format

`.hbp` files are JSON and are meant to be diff-friendly: nodes carry stable ids,
and links reference pins **by name**, so reordering a node type's pins does not
scramble existing graphs.

```json
{
  "version": 1,
  "name": "PlayerController",
  "variables": [
    { "name": "Speed", "type": "float", "default": 6.0, "exposed": true }
  ],
  "functions": [],
  "graph": {
    "nodes": [
      { "id": 1, "type": "event.tick", "x": -520.0, "y": -120.0 },
      { "id": 2, "type": "transform.add_offset", "x": 190.0, "y": -120.0 }
    ],
    "links": [
      { "kind": "exec", "from": { "node": 1, "pin": "exec" },
                        "to":   { "node": 2, "pin": "exec" } }
    ]
  }
}
```

Loading repairs what it can: nodes without ids get fresh ones, and links whose
endpoints no longer exist are dropped. Anything the loader cannot repair — an
unknown node type, a link to a pin that no longer exists — surfaces as a compile
error rather than being silently discarded.

`tests/test_project/Blueprints/PlayerController.hbp` is a worked example: tick
driven movement through a user function, a jump with a latent landing, and a
`BeginPlay` greeting.

Exporting a project copies every `.hbp` in the workspace alongside the saved
worlds, preserving its relative path, so an exported game resolves the same
`assetPath` the editor did.

## Adding a node type

Node types live in `src/engine/blueprint/blueprint_nodes.cpp`. A node is a plain
function pointer plus a description of its pins:

```cpp
BlueprintExecResult node_add(BlueprintExecContext &context)
{
  context.set_output(0, BlueprintValue::from_float(
      context.input(0).as_float() + context.input(1).as_float()));
  return BlueprintExecResult::stop();
}

define({"math.add", "Add", "Math", "Float addition.", BlueprintNodeKind::Pure, {}, {},
        {pin("a", ValueType::Float), pin("b", ValueType::Float)},
        {pin("result", ValueType::Float)}, node_add, "plus sum +"});
```

Exec nodes return `BlueprintExecResult::next(pinIndex)` to continue, `loop()` to
be re-entered after the chain they started finishes, or `wait(seconds)` to
suspend. Nodes whose pin layout depends on the asset (variable accessors,
function calls) supply a `signatureFn` instead of a fixed signature.

Keep the registry name stable once a node ships — it is what `.hbp` files store.

A subsystem can own its own category rather than adding to the built-in file:
expose a `register_*_blueprint_nodes()` entry point and call it from the tail of
`register_builtin_blueprint_nodes()`. The Animation category does exactly this
from `src/engine/animation/animation_blueprint_nodes.cpp`.

## Where the code lives

| Path | Responsibility |
|------|----------------|
| `src/engine/blueprint/blueprint_value.*` | The dynamic value type and the conversion tables |
| `src/engine/blueprint/blueprint_graph.*` | Node/link/variable/function data model and JSON round trip |
| `src/engine/blueprint/blueprint_node_registry.*` | Node type registry and signature resolution |
| `src/engine/blueprint/blueprint_nodes.cpp` | The built-in node library |
| `src/engine/blueprint/blueprint_compiler.*` | Validation, wildcard resolution, register allocation, pure-node scheduling |
| `src/engine/blueprint/blueprint_vm.*` | The execution VM, instances and latent actions |
| `src/engine/blueprint/blueprint_asset.*` | `.hbp` load/save and the compiled-asset cache |
| `src/engine/blueprint/blueprint_runtime.*` | Drives every Blueprint in the running world |
| `src/engine/blueprint/blueprint_engine_host.*` | Bridges graphs to physics, audio, logging and HadesAPI |
| `src/editor/blueprint/blueprint_editor_panel.*` | The node graph editor panel |
