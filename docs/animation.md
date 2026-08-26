# Animation

Hades has two skeletal animation paths. The first is the imported one described in
[Models & Animation](models.md): an `AnimationComponent` plays a clip that came in the model file,
by index. The second — everything on this page — is the authored one: clips you key in the editor,
an animator state machine that decides which of them plays, and a runtime that turns the result into
a bone palette for the same skinned mesh pipeline.

## Overview

Three pieces, all of them plain JSON on disk:

- **Authored clips** — `AnimationClipAsset` (`src/engine/animation/animation_clip.hpp`). A clip holds
  one `AnimationBoneTrack` per joint, and a track binds to its joint by **name**, not by index. Each
  track keeps separate translation, rotation and scale sub-tracks; a sub-track with no keys is not
  animated at all, so a rotation-only clip leaves translation and scale alone. Every key carries its
  own interpolation mode, and a clip also carries named events. Stored as
  `<workspace>/.hades/animations/<name>.json`.
- **The animator graph** — `AnimatorGraph` (`animator_graph.hpp`): parameters, layers, states,
  transitions and blend trees. Stored as `<workspace>/.hades/animators/<name>.json`.
- **The runtime** — an `AnimatorComponent` on an entity names a graph, a default clip, or both. The
  playback state is deliberately *not* in the component: `AnimationRuntime` owns one
  `AnimatorInstance` per entity, and `AnimatorSystem` advances them every frame.

Imported animation is not thrown away. `ModelAsset::clips` still holds the assimp import (channels
indexed by node, no easing, no events), and the **Clips** tab of the Animation panel bakes one into
an editable clip with `AnimationClipAsset::bake_from_model`, which rebinds every channel by node
name. An entity may carry both `AnimationComponent` and `AnimatorComponent`; when both are present
the animator wins.

`Skeleton` (`skeleton.hpp`) is a 1:1 view over `ModelAsset::nodes`, so a joint index is also a node
index. That identity is what lets an authored pose become a GPU bone palette without any lookup
table.

## The Animation panel

The **Animation** panel (plugin id `animation-editor`) rigs a model, authors clips against its
skeleton, and previews them live in the viewport. It has three tabs — **Animate**, **Rig** and
**Clips** — which all share one target. Open it with **Open in Animation editor** on an entity's
**Animator** component in the **Properties** panel; visibility is then remembered per workspace in
`.hades/settings.json`.

### Choosing a target

The target bar picks either an entity in the active world that carries a **Model** component, or a
model file straight from the workspace — the second one so a rig can be built before anything in the
scene references the mesh. **Follow selection** keeps the target in sync with the entity selected in
the scene.

The target matters for more than the skeleton: the viewport overlay and the live pose preview both
need a real entity to draw on, because the renderer draws entities and not asset files. With a
workspace-model target the panel still edits clips and rigs, but nothing is drawn in the viewport
and nothing is previewed on a mesh — so it says so, and offers **Add to world**, which creates an
entity carrying that model in the open world and targets it. That entity is an ordinary one: it
shows up in the hierarchy, it is saved with the world, and it is deleted like any other.

The model is looked up in `ModelAssetCache` every frame — the panel never caches the `ModelAsset`
pointer, because a workspace switch clears the cache underneath it — and a failed import is reported
inline. The cache itself keys on the path and holds the result, success *or* failure, until someone
invalidates it, so **re-exporting the mesh on disk is not picked up on its own**: fixing a broken
export leaves the same error on screen until the model is invalidated. **Save rig** does that for
you; otherwise reopen the workspace.

### The skeleton tree

The left side of the **Animate** tab shows the skeleton built by `Skeleton::from_model`: every node
of the model, parents before children, with a filter box. Joints that skin no vertices
(`SkeletonJoint::skinned == false`) are drawn dimmed — they are real hierarchy nodes, so moving one
still moves its skinned descendants. Selecting a joint shows its local translation, rotation (as
euler degrees) and scale; editing those fields changes the previewed pose, and with **auto-key** on
also writes a key at the play head.

The same selection drives the viewport, as long as the target is an **entity**: the skeleton is drawn
over the mesh, clicking a joint picks it, and the transform gizmo moves the selected joint instead of
the entity. There is no pose-editing switch to find — the gizmo is handed to the joint exactly while
a joint is selected on the **Animate** tab, and handed straight back to the entity transform the
moment you deselect the joint, leave that tab, uncheck **Draw skeleton**, close the panel or enter
play mode. **Draw skeleton**, **Joint names** and **Unskinned joints** under **Viewport Overlay**
control what the overlay shows.

### The dope sheet

The dope sheet opens with a **Summary** row carrying every key time in the clip, then one row per
bone, expandable into one row per channel, and an **Events** row when the clip has events. Tracks
whose bone does not exist on the current skeleton are drawn greyed out rather than hidden — that is
what a retargeting mismatch looks like.

- Click a key to select it, shift-click to extend the selection across that row, ctrl/cmd-click to
  toggle one key. Clicking a bone or summary row selects every key on it.
- Drag on empty space to box-select.
- Drag selected keys horizontally to retime them. Moves go through
  `AnimationClipAsset::move_key`, so a key landing on an existing one replaces it.
- **Delete** removes the selection.
- Right-click opens **Insert key here**, **Delete selected** and an **Interpolation** submenu that
  rewrites the mode of every selected key.
- The play head is the vertical line in the ruler; drag it, or click anywhere in the ruler, to scrub.

**Snap** is on by default and rounds every retime and every inserted key to the clip's frame grid —
`frameRate`, 30 fps unless you change it in the clip properties — and the ruler readout says `snap`
or `free`. Keys that land within `AnimationClipAsset::kKeyEpsilon` (1e-4 s) of each other are
collapsed into one, so the sheet can never accumulate duplicate keys at the same time.

### Interpolation and easing

Interpolation is per key and describes the segment that *starts* at that key. The modes are `step`,
`linear`, `easeIn`, `easeOut`, `easeInOut` and `bezier` (`Interpolation` in `animation_types.hpp`).

Every mode is a reshaping of the normalised segment parameter by `apply_easing`; the value itself is
then a plain lerp for translation and scale and a slerp for rotation. Easing therefore never leaves
the unit quaternion sphere, and a key can be switched between `easeInOut` and an authored curve
without the motion jumping.

`bezier` uses the key's own `EaseCurve`, four control values in the same layout as the CSS
`cubic-bezier` function: `(x1, y1)` and `(x2, y2)` with implicit endpoints `(0, 0)` and `(1, 1)`. The
default is `0.25, 0.1, 0.25, 1.0`. The x controls are clamped to `[0, 1]` so the curve stays solvable;
y is left free, so overshoot and anticipation curves work.

### The curve editor

**Curves** draws the value curves for the rows in the selection under the dope sheet: three polylines
(x, y, z) for translation and scale, and euler degrees for rotation. Each segment is
sampled through `apply_easing`, so the drawn shape is the easing that will actually play, not a
straight line between keys. Key points are draggable — vertically to change the value, horizontally
to retime.

### Auto-key and the transport

The transport row has first frame, previous frame, play/pause, next frame and last frame, **Loop**,
**Speed** (-4x to 4x, negative plays backwards), a frame counter reading `12 / 45 @ 30 fps`,
**Auto-key**, and **Key selected** and **Key whole pose** buttons that key at the play head. With
auto-key on, a gizmo drag or a numeric edit of a joint writes a key on its own
(`AnimationClipAsset::set_pose_key` for a whole pose). The space bar is deliberately *not* bound —
the viewport already uses it — so play/pause is a button.

### Events

An event is a time, a name, and an optional string and float (`AnimationEventKey`). The events editor
lists them with those four fields plus add and remove. At playback, `events_in_range` fires each
event whose time the play head crossed during the frame, including across a loop wrap, and scripts
and Blueprints react to them by name. An instance queues at most 64 events per frame.

### Where clips live

Clips are written by `AnimationClipCache::saveClip` to
`<workspace>/.hades/animations/<name>.json`, formatted with an indent of 2. A clip reference is
either a bare name (`"run"`), a workspace-relative path, or an absolute path. `.hades/` is what the
exporter ships, which is why the assets live there — and an exported game resolves the same
references against its project directory, because `GameRuntime` points `AnimationClipCache` at that
directory exactly the way it points `ModelAssetCache` at it. The clip properties block edits `name`,
`duration`, `frameRate`, `looping`, `additive` and `additiveReferenceTime`; `recompute_duration`
grows the duration to cover the last key but never shrinks it, so deliberate trailing hold time
survives.

The **Clips** tab lists every clip in the workspace and creates, duplicates, renames and deletes
them. A new clip starts from `AnimationClipAsset::from_rest_pose`, a single key holding the
skeleton's rest pose. Below that it lists the model's own imported animations with their length and
channel count, and **Bake to editable clip** turns one into an authored clip you can key.

### Keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| `Delete` | Delete the selected keys |
| Shift-click | Extend the key selection |
| Ctrl/Cmd-click | Toggle one key in the selection |
| Ctrl/Cmd + `Z` | Undo |
| Ctrl/Cmd + Shift + `Z` | Redo |
| Ctrl/Cmd + wheel | Zoom the timeline around the cursor |
| Shift + wheel, or middle-drag | Pan the timeline sideways |
| Wheel | Scroll the row list |

Undo and redo apply while the panel is focused, and work on snapshots of the clip JSON — one entry
per user action, capped at 64.

## Rigging

The **Rig** tab authors a `RigAsset`: a skeleton that overlays an imported model, for a mesh that
arrived without one (a sculpt exported with no armature) or one whose imported skeleton you want to
extend.

A rig joint stores its parent **by name**, so inserting or re-parenting a joint never corrupts the
hierarchy, and a joint may parent onto a node that came from the source file. Each joint carries a
rest translation, rotation and scale, editable in the tab. `RigAsset::topological_sort` reorders
joints so parents always precede children and reports a cycle instead of producing one.
`rig_from_model` seeds a rig from whatever skeleton the import already produced, so an imported rig
can be edited rather than rebuilt; a seeded rig has `replaceImportedSkeleton` false and is appended
to the imported hierarchy, while a rig built from scratch replaces it.

Skin weights come from **Bind all meshes** (`compute_auto_weights`) or **Bind selected mesh**
(`compute_auto_weights_for_mesh`), in one of three modes:

| Mode | What it does | Use it for |
|------|--------------|------------|
| `Rigid` | The nearest joint takes the whole vertex, weight 1. | Mechanical parts, props, anything with no deformation across a seam. |
| `Envelope` | Falloff around the bone *segment* (parent to joint), squared, cut off at the falloff radius in model units. | Limbs and torsos — the default, and the mode that respects bone length. |
| `Smooth` | Inverse distance to the joint positions, with the falloff used as the exponent (clamped to 16). | Blobby or low-poly meshes where envelopes leave gaps. |

**Falloff** is the one knob both distance modes read, and **Max influences** is clamped to 1–4
because the vertex format carries 4 weights. The weights of every vertex are normalised on apply, and
binding writes into the rig, never into the mesh file.

No vertex is ever left unbound. A vertex that no envelope reaches — the usual symptom of a falloff
radius set too small for the mesh's units — falls back to the nearest joint at weight 1, because a
wrong-looking vertex is something you can see and fix, while an unweighted one silently stays at the
origin. So an `Envelope` bind that comes out looking rigid in patches is telling you to raise
**Falloff**, not that the bind failed.

The hard limit is **128 bones** (`kMaxModelBones`), which is the size of the palette the mesh
pipeline uploads. The tab warns as the joint count approaches it, and `apply_rig` refuses a rig that
would exceed it and says how many bones were needed, counting the imported bones it is keeping when
`replaceImportedSkeleton` is false — it never silently truncates.

Rigs are stored separately from both the model and the clips, at
`<workspace>/.hades/rigs/<flattened model path>.json`: the whole model reference is flattened into
one file name (`/`, `\`, `:` and spaces become `_`), so two models named `character.glb` in different
folders never collide. `ModelAssetCache` applies the rig every time it loads the model, which means
**re-exporting the mesh never destroys the rig somebody built on top of it**. **Save rig** writes the
file and invalidates the model so the next frame re-imports it with the rig applied; a rig that
cannot be applied is logged and ignored rather than breaking the model.

## The Animator panel

The **Animator** panel (plugin id `animator-graph`, opened with **Open Animator graph** on the
**Animator** component) is a state-machine canvas over an `AnimatorGraph` asset. States are draggable
nodes, transitions are arrows between them, the left rail holds the parameters that gameplay code
drives, and the right pane holds the details of whatever is selected.

**Parameters** come in four types (`AnimParamType`): `float`, `int`, `bool` and `trigger`. A trigger
is a bool that the animator *consumes*: a transition that fires clears every trigger named in its
conditions, which is the standard fire-once input for jumps, attacks and hits. The other three types
are set and left alone. A condition on a parameter the graph does not declare never holds, so a
mistyped name stalls the transition rather than firing it at random.

**States** (`AnimState`) are one of three kinds:

- `clip` — plays one clip, with a speed multiplier and a looping flag.
- `blendTree1D` — blends the clips whose `thresholdX` values bracket `blendParameterX`. Outside the
  authored range the nearest end entry plays on its own. This is the locomotion case: idle at 0, walk
  at 2, run at 6, one `speed` parameter.
- `blendTree2D` — gradient-band blending over every entry in the (`blendParameterX`,
  `blendParameterY`) plane: each entry's weight falls linearly to zero exactly where a neighbour
  takes over from it. That makes the blend continuous everywhere (no pop where the nearest entries
  swap rank), reproduces a clip exactly on an exact hit, and clamps to the boundary clip outside the
  authored set the way `blendTree1D` clamps to its end entry — which is what an authored 8-way strafe
  set expects at its cardinal points and past them.

A blend tree runs on one timeline whose length is the weighted average of its clips, so the feet of a
walk and a run stay in step through the blend instead of sliding against each other.

**Transitions** (`AnimTransition`) have a source and a destination state, a crossfade `duration` in
seconds (0 snaps), and a list of conditions that must *all* hold. A source of "any state" makes the
transition eligible from whatever state is current. `hasExitTime` holds the transition back until the
source state's normalised time has passed `exitTime`, which is how you let an attack finish before
returning to idle. `canInterrupt` decides whether the transition may fire while another crossfade is
already running. Ties are broken by `priority` (higher first), then by declaration order.

A condition is a parameter, an operator and a threshold. The operators are `greater`, `less`,
`greaterOrEqual`, `lessOrEqual`, `equals`, `notEquals`, `isTrue` and `isFalse`.

**Layers** (`AnimLayer`) are independent state machines stacked on one skeleton. Layer 0 is the base
pose and defines the result; every layer above it contributes at its own `weight`, restricted to a
bone mask built from `maskBones` (empty means the whole skeleton, and `maskIncludesDescendants`
extends the mask down the hierarchy from each named joint). An `additive` layer is applied as a delta
rather than a blend: translations and scales add and rotations compose, measured against the clip's
own `additiveReferenceTime` pose for an additive clip, or the rest pose for a normal one. That is how
an aim offset, a recoil or a lean stacks on top of a full-body run.

**Validate** runs `AnimatorGraph::validate` and lists the structural problems it finds inline:
duplicate parameter or state names, a default state index out of range, transitions pointing at a
state that does not exist, conditions naming an unknown parameter, and blend trees with no entries or
no blend parameter. A new graph is passed through `ensure_default_layer`, so it has one layer with
one state and is immediately playable.

During play mode the panel debugs the live animator for the selected entity, read from
`AnimationRuntime::instance().find(entity)`: the active state is highlighted with its normalised
time, a running transition is highlighted, and the parameter rail shows the values the game is
actually writing — and lets you poke them, which is the fastest way to find out why a character is
stuck in a state.

Graphs are saved through `AnimationClipCache::saveGraph` to
`<workspace>/.hades/animators/<name>.json`. An entity uses one by setting **Graph** on its
**Animator** component; **Default Clip** is what plays when no graph is set, and **Play On Start**,
**Speed** and **Default Blend** (0.15 s unless changed) cover the rest. Parameter overrides authored
on the component are applied once, on the entity's first frame of play, and left to the game after
that.

## Scripting

`hades::Animation` (`src/engine/animation/script_animation.hpp`, already included by
`engine/hades.hpp`) is the whole scripting surface. Every call is a no-op, or returns a default, for
an entity with no model or no animator, so scripts do not have to guard.

```cpp
#include "engine/hades.hpp"

#include <cmath>

class PlayerAnimation : public hades::HadesScript
{
public:
  void onStart(hades::ScriptContext& ctx) override
  {
    // Snap on the first frame: there is no previous pose to blend out of.
    hades::Animation::play(ctx.entityId, "idle", 0.0f);
  }

  void onUpdate(hades::ScriptContext& ctx, float deltaTime) override
  {
    auto& pos = ctx.componentManager.getComponent<hades::PositionComponent3D>(ctx.entityId);

    const float dx = pos.x - lastX_;
    const float dz = pos.z - lastZ_;
    lastX_ = pos.x;
    lastZ_ = pos.z;

    // The graph blends idle -> walk -> run along this parameter.
    // The script never picks a clip.
    const float speed = deltaTime > 0.0f ? std::sqrt(dx * dx + dz * dz) / deltaTime : 0.0f;
    hades::Animation::setFloat(ctx.entityId, "speed", speed);
    hades::Animation::setBool(ctx.entityId, "grounded", pos.y <= 0.001f);

    if (hades::Animation::eventFired(ctx.entityId, "footstep"))
    {
      hades::Audio::playSfxr(hades::Audio::SfxrBlip);
    }
  }

  void onKeyDown(hades::ScriptContext& ctx, int keyCode) override
  {
    if (keyCode == hades::HADES_KEY_SPACE)
    {
      // Consumed by the first transition that uses it, so it fires once.
      hades::Animation::setTrigger(ctx.entityId, "jump");
    }
  }

private:
  float lastX_ = 0.0f;
  float lastZ_ = 0.0f;
};

HADES_REGISTER_SCRIPT(PlayerAnimation)
```

The full API. Every method takes the entity id first, and `layer` selects the animator layer — 0, the
base layer, everywhere except `stop`, which defaults to stopping all of them.

| Method | Arguments | Returns |
|--------|-----------|---------|
| `Animation::play` | `entity, clip, blendSeconds = -1, looping = true, layer = 0` | `void` — crossfades to a clip by name or path; 0 seconds snaps, negative uses the component's **Default Blend** |
| `Animation::playOnce` | `entity, clip, blendSeconds = -1, layer = 0` | `void` — plays once and holds the last frame; blend as for `play` |
| `Animation::stop` | `entity, layer = -1` | `void` — stops one layer, or all of them, holding the current pose |
| `Animation::restart` | `entity, layer = 0` | `void` — restarts the active source from its first frame |
| `Animation::seek` | `entity, seconds, layer = 0` | `void` |
| `Animation::setPlaying` | `entity, playing` | `void` |
| `Animation::isPlaying` | `entity` | `bool` |
| `Animation::setSpeed` | `entity, speed` | `void` — multiplies the per-state speed |
| `Animation::speed` | `entity` | `float` — 0 when the entity has no animator |
| `Animation::gotoState` | `entity, state, blendSeconds = 0.2f, layer = 0` | `bool` — false when there is no graph or no such state |
| `Animation::currentState` | `entity, layer = 0` | `std::string` — empty when there is no graph |
| `Animation::currentClip` | `entity, layer = 0` | `std::string` |
| `Animation::normalizedTime` | `entity, layer = 0` | `float` in `[0, 1]` |
| `Animation::isTransitioning` | `entity, layer = 0` | `bool` |
| `Animation::setFloat` | `entity, name, value` | `void` |
| `Animation::setInt` | `entity, name, value` | `void` |
| `Animation::setBool` | `entity, name, value` | `void` |
| `Animation::setTrigger` | `entity, name` | `void` — latched until a transition consumes it |
| `Animation::resetTrigger` | `entity, name` | `void` |
| `Animation::getFloat` | `entity, name` | `float` — 0 when the parameter does not exist |
| `Animation::getInt` | `entity, name` | `int` |
| `Animation::getBool` | `entity, name` | `bool` |
| `Animation::eventFired` | `entity, name` | `bool` — true for the frame the event fires; poll it in `onUpdate` |
| `Animation::drainEvents` | `entity` | `std::vector<AnimationEventFired>` — every event this frame, consumed by the call |

Each fired event carries `name`, `stringValue`, `floatValue`, the `clip` it came from and the `time`
of the key. Animation events are also published on the event bus as `AnimationEvent` (with the
entity), so a system can subscribe instead of polling.

Polling is non-destructive, so any number of consumers can watch the same event in one frame — a
script playing a footstep sound and a Blueprint node spawning dust both see it. `AnimatorSystem`
clears the buffer once per frame, ahead of the update that refills it, which is what keeps
`eventFired` true for exactly the frame after the play head crosses the key. `drainEvents` is the
exception: it hands the list over and empties it, so use it from one owner only.

## Blueprints

The Blueprint palette (see [Blueprints](blueprints.md)) has an **Animation** category holding the
same facade, one node per call. The **Target** pin defaults to the entity that owns the Blueprint, so
it can be left unconnected.

| Node | What it does |
|------|--------------|
| **Play Animation** | Crossfades to a clip by name. Pins: clip, blend seconds, loop. |
| **Play Animation Once** | Crossfades to a clip, plays it once, holds the last frame. |
| **Stop Animation** | Stops every layer and leaves the skeleton on its current pose. |
| **Set Animation Speed** | Scales playback rate; 0 freezes, negative plays backwards. |
| **Go To Animation State** | Crossfades the graph to a named state, bypassing its transition rules. Outputs `success`. |
| **Get Animation State** | The name of the state currently playing. |
| **Get Animation Time** | Playback position of the active clip in 0..1. |
| **Is Animation Playing** | True while the animation is advancing. |
| **Set Animation Float** | Writes a float parameter, such as movement speed. |
| **Set Animation Int** | Writes an int parameter, such as a weapon index. |
| **Set Animation Bool** | Writes a bool parameter, such as grounded. |
| **Set Animation Trigger** | Fires a one-shot trigger parameter. |
| **Get Animation Float** | Reads a float parameter back, 0 when it does not exist. |
| **Get Animation Int** | Reads an int parameter back, 0 when it does not exist. |
| **Get Animation Bool** | Reads a bool parameter back, false when it does not exist. |
| **Animation Event Fired** | True on the frame a named event fires — footsteps, hit windows, VFX. |

The pattern to aim for is the same one the scripting example uses: the Blueprint sets parameters and
fires triggers, and the graph decides which pose that means. Reach for **Play Animation** and **Go To
Animation State** for the cases a state machine should not own — a cutscene, a one-off emote, a
debug key.

## How it works

Per frame, during play:

1. Scripts and Blueprints run first and write parameters, triggers and clip requests through
   `hades::Animation`. A write creates the entity's `AnimatorInstance` if it does not exist yet, so
   an `onStart` that plays a clip is never dropped.
2. `AnimatorSystem` (registered in the `Logic` phase by both the editor and the standalone runtime)
   walks every entity with a `ModelComponent` *and* an `AnimatorComponent`. On the first frame it
   binds the graph and applies the component's authored parameter overrides; afterwards it only
   pushes values that changed in the inspector, so it never stamps over what a script decided.
3. `AnimatorInstance::update` evaluates each layer: pick a transition (any-state edges included,
   guarded by conditions, exit time and interruptibility, highest priority first), advance the active
   source, sample the clip — or the blend tree's two or three contributing clips — into a `Pose` that
   starts as the skeleton's rest pose, and crossfade against the outgoing source. Only the source
   carrying at least half of the crossfade fires events, so a blend never plays both clips'
   footsteps.
4. Layer 0 defines the pose; higher layers blend or add over it through their mask and weight, and
   the rotations are renormalised once at the end.
5. The local-space pose is composed up the hierarchy into model-space joint matrices
   (`Skeleton::local_to_global`) and turned into the skinning palette
   (`ModelAsset::paletteFromNodeGlobals`, which applies `globalInverseTransform` and each bone's
   offset matrix). The palette stays on the instance, which `AnimationRuntime` owns.
6. `SceneRenderer` asks `AnimationRuntime::palette_for` for each model entity. The priority is: the
   animation editor's preview, then the animator, then the legacy `AnimationComponent`, then the bind
   pose. A palette whose size does not match the model's bind pose is dropped rather than uploaded.
   The Vulkan mesh pipeline uploads up to 128 matrices per model into a dynamic-offset uniform
   buffer and skins on the GPU with 4 weights per vertex.

In the editor, the Animation panel publishes its own palette through
`AnimationRuntime::set_preview_palette` every frame it renders, keyed by the target **entity** —
which is what makes scrubbing show the authored pose on the actual mesh without that entity needing
an animator at all, and why a workspace-model target previews nothing until **Add to world** gives
it one. The panel runs at plugin order
20, before the viewport draws, so a scrub lands in the same frame. The preview is cleared when the
panel closes or is collapsed, loses its target or its workspace, or play mode starts — otherwise a
stale preview would freeze the character during play. Previews are also dropped when the entity is
destroyed, because entity ids are recycled and the palette would otherwise be painted onto whatever
inherits the id.

Because tracks bind by joint name, a clip is retargetable: any skeleton that shares the naming can
play it, and re-importing a model that renumbers its nodes does not invalidate a single key. A track
whose name matches nothing is simply not applied, and the dope sheet greys it out.

## File formats

All three assets are JSON written with an indent of 2, and all three are read tolerantly: a missing
or malformed field falls back to its default rather than failing the load. All three also stamp a
`version`. Clips and rigs *check* it: a file whose `version` is newer than the build understands is
refused outright rather than half-read and then written back as version 1, which would silently
delete whatever the newer format added. Graphs stamp the field but do not read it yet. So a
hand-edited clip or rig must keep `"version": 1` — bumping it makes the asset fail to load.

A clip, `<workspace>/.hades/animations/wave.json`. Keys use short field names because a long clip has
a lot of them: `t` is time, `v` the value, `i` the interpolation of the segment starting at this key,
and `e` the four bezier control values (written only for `bezier` keys). Rotations are `[x, y, z, w]`.

```json
{
  "version": 1,
  "name": "wave",
  "sourceModel": "models/character.glb",
  "duration": 1.5,
  "frameRate": 30.0,
  "looping": true,
  "additive": false,
  "additiveReferenceTime": 0.0,
  "tracks": [
    {
      "bone": "upper_arm.R",
      "translations": [],
      "rotations": [
        { "t": 0.0, "i": "easeInOut", "v": [0.0, 0.0, 0.0, 1.0] },
        { "t": 0.75, "i": "bezier", "e": [0.25, 0.1, 0.25, 1.0], "v": [0.0, 0.0, 0.707, 0.707] },
        { "t": 1.5, "i": "linear", "v": [0.0, 0.0, 0.0, 1.0] }
      ],
      "scales": []
    }
  ],
  "events": [
    { "time": 0.75, "name": "wave_peak", "stringValue": "", "floatValue": 0.0 }
  ]
}
```

A graph, `<workspace>/.hades/animators/player.json`. `fromState` and `toState` are indices into the
layer's `states` array; `-1` as a source means "any state".

```json
{
  "version": 1,
  "name": "player",
  "description": "",
  "sourceModel": "models/character.glb",
  "parameters": [
    { "name": "speed", "type": "float", "floatValue": 0.0, "intValue": 0, "boolValue": false },
    { "name": "jump", "type": "trigger", "floatValue": 0.0, "intValue": 0, "boolValue": false }
  ],
  "layers": [
    {
      "name": "Base",
      "weight": 1.0,
      "additive": false,
      "maskBones": [],
      "maskIncludesDescendants": true,
      "defaultState": 0,
      "states": [
        {
          "name": "Locomotion",
          "kind": "blendTree1D",
          "clip": "",
          "speed": 1.0,
          "looping": true,
          "blendParameterX": "speed",
          "blendParameterY": "",
          "entries": [
            { "clip": "idle", "thresholdX": 0.0, "thresholdY": 0.0, "speed": 1.0 },
            { "clip": "walk", "thresholdX": 2.0, "thresholdY": 0.0, "speed": 1.0 },
            { "clip": "run", "thresholdX": 6.0, "thresholdY": 0.0, "speed": 1.0 }
          ],
          "x": 40.0,
          "y": 40.0
        },
        {
          "name": "Jump",
          "kind": "clip",
          "clip": "jump",
          "speed": 1.0,
          "looping": false,
          "blendParameterX": "",
          "blendParameterY": "",
          "entries": [],
          "x": 300.0,
          "y": 40.0
        }
      ],
      "transitions": [
        {
          "fromState": -1,
          "toState": 1,
          "duration": 0.1,
          "hasExitTime": false,
          "exitTime": 1.0,
          "canInterrupt": true,
          "priority": 10,
          "conditions": [
            { "parameter": "jump", "op": "isTrue", "threshold": 0.0 }
          ]
        },
        {
          "fromState": 1,
          "toState": 0,
          "duration": 0.2,
          "hasExitTime": true,
          "exitTime": 0.9,
          "canInterrupt": false,
          "priority": 0,
          "conditions": []
        }
      ]
    }
  ]
}
```

A rig, `<workspace>/.hades/rigs/models_character.glb.json`. `parent` is a joint name — either another
rig joint or a node of the model — and empty means the rig root. `t`, `r` and `s` are the rest
transform. Each mesh binding is flat: 4 joint indices and 4 weights per vertex, in vertex order, with
`-1` marking an unused slot.

```json
{
  "version": 1,
  "sourceModel": "models/character.glb",
  "replaceImportedSkeleton": true,
  "joints": [
    {
      "name": "root",
      "parent": "",
      "t": [0.0, 0.0, 0.0],
      "r": [0.0, 0.0, 0.0, 1.0],
      "s": [1.0, 1.0, 1.0]
    },
    {
      "name": "spine",
      "parent": "root",
      "t": [0.0, 0.85, 0.0],
      "r": [0.0, 0.0, 0.0, 1.0],
      "s": [1.0, 1.0, 1.0]
    }
  ],
  "meshes": [
    {
      "meshIndex": 0,
      "indices": [0, 1, -1, -1, 1, -1, -1, -1],
      "weights": [0.75, 0.25, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0]
    }
  ]
}
```

## Key source files

| File | Purpose |
|------|---------|
| `src/engine/animation/animation_types.hpp` | `Pose`, `BoneMask`, keys, `Interpolation`, `EaseCurve`, `apply_easing` |
| `src/engine/animation/animation_clip.hpp` | `AnimationClipAsset`: tracks, key editing, sampling, events, JSON |
| `src/engine/animation/skeleton.hpp` | `Skeleton` view over `ModelAsset::nodes`, pose to palette |
| `src/engine/animation/pose_ops.hpp` | Blend, additive blend, mask building, rotation renormalisation |
| `src/engine/animation/animator_graph.hpp` | `AnimatorGraph`: parameters, layers, states, transitions, validation |
| `src/engine/animation/animator_instance.hpp` | Per-entity player: transitions, blend trees, layers, events |
| `src/engine/animation/animation_runtime.hpp` | Entity to instance registry, plus the editor pose preview |
| `src/engine/animation/animation_clip_cache.hpp` | Clip and graph loading, saving and listing under `.hades/` |
| `src/engine/animation/rig_asset.hpp` | `RigAsset`, `apply_rig`, `compute_auto_weights`, rig storage |
| `src/engine/animation/script_animation.hpp` | The `hades::Animation` scripting facade |
| `src/engine/animation/animation_blueprint_nodes.cpp` | The Blueprint **Animation** node category |
| `src/engine/components/animator_component.hpp` | `AnimatorComponent`, the serialised description |
| `src/engine/systems/animator_system.hpp` | Per-frame evaluation and event publishing |
| `src/editor/plugins/animation_editor_plugin.hpp` | The **Animation** panel |
| `src/editor/plugins/animator_graph_plugin.hpp` | The **Animator** panel |
| `src/editor/animation_timeline.hpp` | The dope sheet and curve editor widget |
| `src/editor/animation_edit_state.hpp` | The seam between the panel and the viewport overlay |
