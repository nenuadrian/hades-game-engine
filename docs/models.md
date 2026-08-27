# Models & Animation

Hades imports 3D model files through [assimp](https://github.com/assimp/assimp) and renders them — including skeletal animation — through the same forward-lit mesh pipeline used for primitives.

Supported formats: **FBX**, **OBJ**, **glTF / GLB**, **COLLADA (.dae)**.

## Adding a model to a scene

1. Put the model file anywhere inside your workspace (the file tree shows it with a cube icon).
2. In the **Entities** panel, right-click a parent and choose **Add Entity → Model**, or add a **Model** component to an existing entity from the Properties panel.
3. In the **Properties** panel, pick the file in the **Model Asset** dropdown — it lists every model file found in the workspace.

The inspector shows what was imported: mesh count, triangle count, bones, and animation clips. Load failures are shown inline with assimp's error message, and a warning appears when the file carries no skeleton (a common symptom of exporting without the armature/skin).

In the scene view, the selected model entity is outlined with a wire box matching the asset's actual bind-pose bounds. Scenes with no enabled light get a default headlight (a camera-aligned directional light) so geometry stays visibly shaded; add any light entity to take over lighting.

Asset paths are stored workspace-relative, so scenes stay portable; the standalone runtime resolves the same paths against the project directory.

## Materials

Imported materials (base color, metallic/roughness, opacity) are used per mesh by default. Add a **Mesh Renderer** component to override all of the model's materials with a single editable material — the same one used by primitives, including wireframe mode.

## Animation

Add an **Animator** component (the *Model* entity preset includes one already). With neither a **Graph** nor a **Default Clip** set it plays the model's own first animation, so a freshly imported character moves as soon as it is in the scene. To choose a different one, pick it from **Default Clip** — animation that came inside the model file is listed there under *In this model* as `model.fbx#Walk`, alongside any clips you have authored.

From there, **Speed**, **Looping** and **Play On Start** cover simple playback, and a **Graph** takes over when you want a state machine. Nothing has to be baked or converted first.

**Animation** is the superseded predecessor: it plays a clip by *index*, so re-exporting a model with its clips reordered silently changes what plays. It still loads and still runs for existing scenes, but **Add Component** no longer offers it, and its inspector section has a **Convert to Animator** button that carries the clip and playback flags across in one click.

Authoring your own clips, driving them from an animator state machine, and rigging a model that arrived without a skeleton are covered in [Animation](animation.md).

## How it works

- `ModelAssetCache` loads each file once (lazily, on first use) into a CPU-side `ModelAsset`: meshes with per-vertex bone indices/weights, the node hierarchy, a bone palette, and animation clips with TRS keyframes.
- Every imported mesh renders through the skinned pipeline. Rigid meshes are bound to a single pseudo-bone for the node that references them, so one vertex format and one shader path covers everything.
- Each frame the bone palette comes from `AnimationRuntime::palette_for` — the editor's pose preview if one is published, otherwise the animator's own output — and falls back to `ModelAsset::samplePose` for a legacy `AnimationComponent` and then to the bind pose. The Vulkan mesh pipeline uploads it to a dynamic-offset uniform buffer — up to 128 bones per model, skinned on the GPU (4 weights per vertex).
- An animation living inside a model file is named `model.fbx#Walk` and baked into a playable clip on demand by `AnimationClipCache`, so the animator needs no copy on disk to play it. See [Animation](animation.md).
- Serialization uses the standard component registry: `model` stores the asset path, `animator` stores the graph, default clip and playback flags, and `animation` still round-trips clip index, playing/looping flags and speed for scenes that carry it.
