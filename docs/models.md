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

Add an **Animation** component (the *Model* entity preset includes one already). If the model asset has clips you can:

- pick the **Clip** from a dropdown,
- toggle **Playing** / **Looping** and set **Speed** (negative plays in reverse),
- scrub **Time** to preview a pose in the editor without entering play mode.

During play mode the `AnimationSystem` advances the clip time each frame; one-shot clips clamp at the end and stop, looping clips wrap.

## How it works

- `ModelAssetCache` loads each file once (lazily, on first use) into a CPU-side `ModelAsset`: meshes with per-vertex bone indices/weights, the node hierarchy, a bone palette, and animation clips with TRS keyframes.
- Every imported mesh renders through the skinned pipeline. Rigid meshes are bound to a single pseudo-bone for the node that references them, so one vertex format and one shader path covers everything.
- Each frame, `SceneRenderer` samples the entity's clip time into a bone palette (`ModelAsset::samplePose`), which the Vulkan mesh pipeline uploads to a dynamic-offset uniform buffer — up to 128 bones per model, skinned on the GPU (4 weights per vertex).
- Serialization uses the standard component registry: `model` stores the asset path, `animation` stores clip index, playing/looping flags and speed.
