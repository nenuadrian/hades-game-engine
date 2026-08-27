#include "editor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "../engine/animation/animation_clip_cache.hpp"
#include "../engine/animation/animation_runtime.hpp"
#include "../engine/animation/animator_graph.hpp"
#include "../engine/animation/animator_instance.hpp"
#include "../engine/assets/model_asset.hpp"
#include "../engine/assets/model_asset_cache.hpp"
#include "blueprint/blueprint_editor_panel.hpp"
#include "../engine/blueprint/blueprint_asset.hpp"
#include "../engine/components/animation_component.hpp"
#include "../engine/components/blueprint_component.hpp"
#include "../engine/components/animator_component.hpp"
#include "../engine/components/audio_listener_component.hpp"
#include "../engine/components/model_component.hpp"
#include "../engine/components/audio_source_component.hpp"
#include "../engine/components/camera_component.hpp"
#include "../engine/components/light_component.hpp"
#include "../engine/components/mesh_renderer_component.hpp"
#include "../engine/components/name_component.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/components/primitive_component.hpp"
#include "../engine/components/collider_component.hpp"
#include "../engine/components/render_component.hpp"
#include "../engine/components/rigid_body_component.hpp"
#include "../engine/components/rotation_component_3d.hpp"
#include "../engine/components/scale_component_3d.hpp"
#include "../engine/components/script_component.hpp"
#include "../engine/components/text_component.hpp"
#include "../engine/components/ui_canvas_component.hpp"
#include "../engine/ui/ui_widget_registry.hpp"
#include "../engine/ui/ui_widget_ops.hpp"
#include "../engine/components/transform_hierarchy_component.hpp"
#include "../engine/components/world_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"

namespace hades
{
  namespace
  {
    constexpr char PROPERTIES_WINDOW_TITLE[] = "Properties";

    const char *primitive_type_label(PrimitiveType type)
    {
      switch (type)
      {
      case PrimitiveType::Cube:
        return "Cube";
      case PrimitiveType::Plane:
        return "Panel";
      case PrimitiveType::Sphere:
        return "Sphere";
      }

      return "Unknown";
    }

    std::string script_component_label(const ScriptAttachment &attachment, std::size_t index)
    {
      std::string suffix;
      if (!attachment.className.empty())
      {
        suffix = attachment.className;
      }
      else if (!attachment.scriptPath.empty())
      {
        suffix = std::filesystem::path(attachment.scriptPath).stem().string();
      }

      std::string label = "Script Component " + std::to_string(index + 1);
      if (!suffix.empty())
      {
        label += ": " + suffix;
      }

      return label;
    }

    // AnimatorParamOverride stores its type as a string so the scene file stays
    // readable; these keep the combo and the string in step.
    constexpr const char *ANIMATOR_PARAM_TYPE_LABELS[] = {"float", "int", "bool"};

    int animator_param_type_index(const std::string &type)
    {
      if (type == "int")
      {
        return 1;
      }
      if (type == "bool")
      {
        return 2;
      }

      return 0;
    }

    /// Authorable override type for a graph parameter. Triggers are never
    /// authored, so they fall back to the bool editor rather than vanishing
    /// from the picker.
    const char *animator_override_type_for(AnimParamType type)
    {
      switch (type)
      {
      case AnimParamType::Int:
        return "int";
      case AnimParamType::Bool:
      case AnimParamType::Trigger:
        return "bool";
      case AnimParamType::Float:
        break;
      }

      return "float";
    }

  }

  void Editor::properties(EntityManager &entityManager, ComponentManager &componentManager)
  {
    ImGui::Begin(PROPERTIES_WINDOW_TITLE);

    if (!state.selectedEntity.has_value())
    {
      ImGui::TextDisabled("No selection.");
      ImGui::End();
      return;
    }

    const Entity::EntityId entity = *state.selectedEntity;
    const bool isWorld = componentManager.hasComponent<WorldComponent>(entity);
    ImGui::Text("%s %u", isWorld ? "World" : "Entity", entity);
    if (componentManager.hasComponent<NameComponent>(entity))
    {
      auto &name = componentManager.getComponent<NameComponent>(entity);
      std::array<char, 128> nameBuffer{};
      std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", name.value.c_str());
      if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size()))
      {
        name.value = nameBuffer.data();
      }
    }
    else
    {
      ImGui::TextDisabled("Unnamed");
    }

    ImGui::Separator();

    if (isWorld)
    {
      ImGui::BeginDisabled();
    }
    {
      static int selectedComponentType = 0;
      // "Animation" is deliberately absent: AnimatorComponent now plays a
      // clip that lives inside a model file directly ("model.fbx#Walk"), so
      // the legacy index-addressed player has nothing left that it alone can
      // do. Existing scenes still load and edit it — see the Animation
      // section below, which offers a one-click conversion — but nothing new
      // should be created with it.
      const char *componentTypes[] = {"Script Component", "Rigid Body", "Mesh Renderer", "Model", "Animator", "Blueprint", "UI Canvas"};
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.65f);
      ImGui::Combo("##AddComponentType", &selectedComponentType, componentTypes, IM_ARRAYSIZE(componentTypes));
      ImGui::SameLine();
      if (ImGui::Button("Add Component"))
      {
        if (selectedComponentType == 0)
        {
          if (!componentManager.hasComponent<ScriptComponent>(entity))
          {
            ScriptComponent scriptComponent;
            scriptComponent.attachments.push_back(ScriptAttachment());
            componentManager.addComponent(entity, scriptComponent);
          }
          else
          {
            componentManager.getComponent<ScriptComponent>(entity).attachments.push_back(ScriptAttachment());
          }
        }
        else if (selectedComponentType == 1)
        {
          if (!componentManager.hasComponent<RigidBodyComponent>(entity))
          {
            componentManager.addComponent(entity, RigidBodyComponent{});
          }
          if (!componentManager.hasComponent<RotationComponent3D>(entity))
          {
            componentManager.addComponent(entity, RotationComponent3D{});
          }
          if (!componentManager.hasComponent<ColliderComponent>(entity))
          {
            componentManager.addComponent(entity, ColliderComponent{});
          }
        }
        else if (selectedComponentType == 2)
        {
          if (!componentManager.hasComponent<MeshRendererComponent>(entity))
          {
            componentManager.addComponent(entity, MeshRendererComponent{});
          }
        }
        else if (selectedComponentType == 3)
        {
          if (!componentManager.hasComponent<ModelComponent>(entity))
          {
            componentManager.addComponent(entity, ModelComponent{});
          }
        }
        else if (selectedComponentType == 4)
        {
          if (!componentManager.hasComponent<AnimatorComponent>(entity))
          {
            componentManager.addComponent(entity, AnimatorComponent{});
          }
        }
        else if (selectedComponentType == 5)
        {
          if (!componentManager.hasComponent<BlueprintComponent>(entity))
          {
            BlueprintComponent blueprintComponent;
            blueprintComponent.attachments.push_back(BlueprintAttachment());
            componentManager.addComponent(entity, blueprintComponent);
          }
          else
          {
            componentManager.getComponent<BlueprintComponent>(entity).attachments.push_back(BlueprintAttachment());
          }
        }
        else if (selectedComponentType == 6)
        {
          if (!componentManager.hasComponent<UICanvasComponent>(entity))
          {
            componentManager.addComponent(entity, UICanvasComponent{});
          }
        }
      }
    }
    if (isWorld)
    {
      ImGui::EndDisabled();
    }

    ImGui::Separator();

    if (componentManager.hasComponent<PositionComponent3D>(entity) && ImGui::CollapsingHeader("Transform"))
    {
      auto &position = componentManager.getComponent<PositionComponent3D>(entity);
      ImGui::DragFloat3("Position", &position.x, 0.1f);
    }

    if (componentManager.hasComponent<RotationComponent3D>(entity) && ImGui::CollapsingHeader("Rotation"))
    {
      auto &rot = componentManager.getComponent<RotationComponent3D>(entity);
      ImGui::DragFloat4("Quaternion (x,y,z,w)", &rot.qx, 0.01f);
    }

    if (componentManager.hasComponent<ScaleComponent3D>(entity) && ImGui::CollapsingHeader("Scale"))
    {
      auto &scale = componentManager.getComponent<ScaleComponent3D>(entity);
      ImGui::DragFloat3("Scale", &scale.x, 0.01f, 0.01f, 100.0f);
    }

    if (componentManager.hasComponent<MeshRendererComponent>(entity) && ImGui::CollapsingHeader("Material"))
    {
      auto &meshRenderer = componentManager.getComponent<MeshRendererComponent>(entity);
      auto &mat = meshRenderer.material;

      ImGui::ColorEdit3("Base Color", &mat.baseColorR);
      ImGui::DragFloat("Metallic", &mat.metallic, 0.01f, 0.0f, 1.0f);
      ImGui::DragFloat("Roughness", &mat.roughness, 0.01f, 0.0f, 1.0f);
      ImGui::DragFloat("Opacity", &mat.opacity, 0.01f, 0.0f, 1.0f);
      ImGui::Checkbox("Wireframe", &mat.wireframe);
    }

    if (componentManager.hasComponent<UICanvasComponent>(entity) && ImGui::CollapsingHeader("UI Canvas"))
    {
      // Widget labels here ("Visible", "Offset", ...) collide with other
      // sections, so scope the whole canvas editor (see the animator
      // section's note on ImGui's shared ActiveId).
      ImGui::PushID("UICanvasSection");
      auto &canvas = componentManager.getComponent<UICanvasComponent>(entity);
      register_builtin_ui_widgets();

      int space = static_cast<int>(canvas.space);
      if (ImGui::Combo("Space", &space, "Screen (HUD / menus)\0World (attached to entity)\0"))
      {
        canvas.space = static_cast<UICanvasSpace>(space);
      }
      ImGui::Checkbox("Visible", &canvas.visible);

      if (canvas.space == UICanvasSpace::World)
      {
        ImGui::DragFloat2("Reference Size (px)", &canvas.referenceWidth, 1.0f, 1.0f, 8192.0f);
        ImGui::DragFloat("World Width", &canvas.worldWidth, 0.05f, 0.01f, 100.0f);
        ImGui::DragFloat3("Offset", &canvas.offsetX, 0.05f);
        ImGui::Checkbox("Billboard", &canvas.billboard);
        ImGui::DragFloat("Max Distance", &canvas.maxDistance, 0.5f, 0.0f, 10000.0f);
        ImGui::DragFloat("Fade Distance", &canvas.fadeDistance, 0.5f, 0.0f, 10000.0f);
        ImGui::TextDisabled("Widgets lay out in reference pixels, mapped onto a %.2f-unit-wide quad.",
                            canvas.worldWidth);
      }
      else
      {
        ImGui::DragInt("Sort Order", &canvas.sortOrder, 1.0f);
        ImGui::TextDisabled("Widgets lay out in viewport pixels (anchors are viewport fractions).");
      }

      ImGui::SeparatorText("Widgets");

      // Recursive widget tree editor. Removal is deferred to after the walk
      // so the vector is never mutated mid-iteration.
      struct WidgetTreeEditor
      {
        static void add_widget_controls(std::vector<UIWidget> &siblings, const char *comboId)
        {
          const auto &types = UIWidgetRegistry::instance().all();
          static int selectedType = 0;
          if (selectedType >= static_cast<int>(types.size()))
          {
            selectedType = 0;
          }
          ImGui::SetNextItemWidth(140.0f);
          if (ImGui::BeginCombo(comboId, types.empty() ? "?" : types[selectedType].displayName.c_str()))
          {
            for (int i = 0; i < static_cast<int>(types.size()); ++i)
            {
              if (ImGui::Selectable(types[i].displayName.c_str(), i == selectedType))
              {
                selectedType = i;
              }
            }
            ImGui::EndCombo();
          }
          ImGui::SameLine();
          if (ImGui::Button("Add Widget") && !types.empty())
          {
            const UIWidgetType &type = types[selectedType];
            UIWidget widget = type.defaults;
            widget.type = type.name;
            // Auto-id: type name plus a counter that skips taken names.
            int suffix = 1;
            std::string id;
            do
            {
              id = type.name + std::to_string(suffix++);
            } while (hades::ui::find_widget(siblings, id) != nullptr);
            widget.id = id;
            siblings.push_back(std::move(widget));
          }
        }

        static void draw(std::vector<UIWidget> &widgets)
        {
          int removeIndex = -1;
          for (int i = 0; i < static_cast<int>(widgets.size()); ++i)
          {
            auto &widget = widgets[i];
            ImGui::PushID(i);
            const std::string label = widget.id + "  (" + widget.type + ")";
            const bool open = ImGui::TreeNode("##widget", "%s", label.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
            if (ImGui::SmallButton("x"))
            {
              removeIndex = i;
            }
            if (open)
            {
              ImGui::InputText("Id", &widget.id);
              ImGui::Checkbox("Visible", &widget.visible);
              ImGui::DragFloat2("Anchor", &widget.anchorX, 0.01f, 0.0f, 1.0f);
              ImGui::DragFloat2("Offset (px)", &widget.offsetX, 1.0f);
              ImGui::DragFloat2("Size (px)", &widget.width, 1.0f, 0.0f, 8192.0f);
              ImGui::ColorEdit4("Color", &widget.colorR);
              if (widget.type == "bar" || widget.type == "button")
              {
                ImGui::ColorEdit4("Fill / Label Color", &widget.fillColorR);
              }
              if (widget.type == "text" || widget.type == "button")
              {
                ImGui::InputText("Text", &widget.text);
                ImGui::DragFloat("Text Size (px)", &widget.textSize, 0.5f, 1.0f, 512.0f);
              }
              if (widget.type == "bar")
              {
                ImGui::SliderFloat("Value", &widget.value, 0.0f, 1.0f);
              }
              ImGui::InputText("Bind Variable", &widget.bindVariable);
              ImGui::SetItemTooltip("Blueprint variable on this entity that drives the value/text.");
              const UIWidgetType *typeInfo = UIWidgetRegistry::instance().find(widget.type);
              if ((typeInfo != nullptr && typeInfo->clickable) || !widget.onClickEvent.empty())
              {
                ImGui::InputText("Click Event", &widget.onClickEvent);
                ImGui::SetItemTooltip(
                    "Blueprint Custom Event fired on click; the widget id is the payload.");
              }

              draw(widget.children);
              add_widget_controls(widget.children, "##childType");
              ImGui::TreePop();
            }
            ImGui::PopID();
          }
          if (removeIndex >= 0)
          {
            widgets.erase(widgets.begin() + removeIndex);
          }
        }
      };

      WidgetTreeEditor::draw(canvas.widgets);
      WidgetTreeEditor::add_widget_controls(canvas.widgets, "##rootType");

      ImGui::Spacing();
      if (ImGui::Button("Remove UI Canvas"))
      {
        componentManager.removeComponent<UICanvasComponent>(entity);
      }
      ImGui::PopID();
    }

    if (componentManager.hasComponent<ModelComponent>(entity) && ImGui::CollapsingHeader("Model", ImGuiTreeNodeFlags_DefaultOpen))
    {
      auto &model = componentManager.getComponent<ModelComponent>(entity);
      auto &modelCache = ModelAssetCache::instance();

      std::vector<std::string> modelOptions = workspaceModelFiles_;
      if (!model.assetPath.empty() &&
          std::find(modelOptions.begin(), modelOptions.end(), model.assetPath) == modelOptions.end())
      {
        modelOptions.push_back(model.assetPath);
        std::sort(modelOptions.begin(), modelOptions.end());
      }

      const std::string previewValue =
          model.assetPath.empty() ? "<Select a workspace model>" : model.assetPath;
      if (ImGui::BeginCombo("Model Asset", previewValue.c_str()))
      {
        const bool noneSelected = model.assetPath.empty();
        if (ImGui::Selectable("<None>", noneSelected))
        {
          model.assetPath.clear();
        }
        if (noneSelected)
        {
          ImGui::SetItemDefaultFocus();
        }

        for (const auto &modelPath : modelOptions)
        {
          const bool selected = (model.assetPath == modelPath);
          if (ImGui::Selectable(modelPath.c_str(), selected))
          {
            model.assetPath = modelPath;
          }
          if (selected)
          {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      if (modelOptions.empty())
      {
        ImGui::TextDisabled("No model files (.fbx/.obj/.gltf/.glb/.dae) in the workspace.");
      }

      if (!model.assetPath.empty())
      {
        const ModelAsset *asset = modelCache.get(model.assetPath);
        if (asset != nullptr)
        {
          ImGui::TextDisabled(
              "%zu meshes, %zu triangles, %zu bones, %zu clips",
              asset->meshes.size(), asset->triangleCount(), asset->bones.size(), asset->clips.size());
          if (!asset->hasSkeleton)
          {
            ImGui::TextColored(
                ImVec4(0.88f, 0.72f, 0.34f, 1.0f),
                "No skeleton in this file. If it should be rigged, re-export\n"
                "with the armature/skin included (e.g. Mixamo: FBX Binary).");
          }
        }
        else
        {
          const std::string loadError = modelCache.errorFor(model.assetPath);
          ImGui::TextColored(
              ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "Failed to load: %s",
              loadError.empty() ? "unknown error" : loadError.c_str());
        }
      }

      ImGui::TextDisabled("Add a Mesh Renderer component to override the imported materials.");
    }

    if (componentManager.hasComponent<AnimationComponent>(entity) && ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
    {
      // Scoped for the same reason as the Animator section below: "Looping"
      // here collides with the Audio Source section's "Looping".
      ImGui::PushID("animation_component");

      auto &anim = componentManager.getComponent<AnimationComponent>(entity);

      if (componentManager.hasComponent<AnimatorComponent>(entity))
      {
        ImGui::TextDisabled("An Animator component is attached; it supersedes this clip player.");
      }

      std::string animationModelPath;
      const ModelAsset *asset = nullptr;
      if (componentManager.hasComponent<ModelComponent>(entity))
      {
        animationModelPath = componentManager.getComponent<ModelComponent>(entity).assetPath;
        asset = ModelAssetCache::instance().get(animationModelPath);
      }

      // This component addresses its clip by INDEX into the model's own
      // animation list, so re-exporting the mesh with the clips reordered
      // silently plays a different one. The Animator addresses the same clip
      // by name — "model.fbx#Walk" — and brings crossfading, events, layers
      // and graphs with it, so there is nothing left to stay here for.
      ImGui::TextColored(
          ImVec4(0.88f, 0.72f, 0.34f, 1.0f),
          "Legacy: plays a clip by index, so a re-export that reorders clips\n"
          "changes what plays. The Animator names the same clip instead.");

      if (asset == nullptr)
      {
        ImGui::TextDisabled("Add a Model component with a loaded asset to play animations.");
      }
      else if (asset->clips.empty())
      {
        ImGui::TextDisabled("This model has no animation clips.");
      }
      else
      {
        anim.clipIndex = std::clamp(anim.clipIndex, 0, static_cast<int>(asset->clips.size()) - 1);
        const auto &activeClip = asset->clips[anim.clipIndex];

        if (ImGui::BeginCombo("Clip", activeClip.name.c_str()))
        {
          for (int i = 0; i < static_cast<int>(asset->clips.size()); ++i)
          {
            const bool selected = (i == anim.clipIndex);
            if (ImGui::Selectable(asset->clips[i].name.c_str(), selected))
            {
              anim.clipIndex = i;
              anim.time = 0.0f;
            }
            if (selected)
            {
              ImGui::SetItemDefaultFocus();
            }
          }
          ImGui::EndCombo();
        }

        ImGui::Checkbox("Playing", &anim.playing);
        ImGui::SameLine();
        ImGui::Checkbox("Looping", &anim.looping);
        ImGui::DragFloat("Speed", &anim.speed, 0.01f, -4.0f, 4.0f);
        ImGui::SliderFloat("Time", &anim.time, 0.0f, activeClip.duration, "%.2f s");
        ImGui::TextDisabled("Playback advances in play mode; scrub Time to preview here.");

        ImGui::Separator();
        const bool convertible = !activeClip.name.empty() && !animationModelPath.empty();
        ImGui::BeginDisabled(!convertible);
        if (ImGui::Button(ICON_FA_RIGHT_LEFT "  Convert to Animator"))
        {
          AnimatorComponent animator;
          if (componentManager.hasComponent<AnimatorComponent>(entity))
          {
            // Keep whatever is already authored there; only the clip and the
            // playback flags come across.
            animator = componentManager.getComponent<AnimatorComponent>(entity);
          }
          animator.defaultClip =
              animationModelPath + AnimationClipCache::kImportedClipSeparator + activeClip.name;
          animator.looping = anim.looping;
          animator.speed = anim.speed;
          animator.playOnStart = anim.playing;

          if (componentManager.hasComponent<AnimatorComponent>(entity))
          {
            componentManager.getComponent<AnimatorComponent>(entity) = animator;
          }
          else
          {
            componentManager.addComponent(entity, animator);
          }
          componentManager.removeComponent<AnimationComponent>(entity);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
          ImGui::SetTooltip(
              convertible
                  ? "Replace this with an Animator playing the same clip by name."
                  : "The active clip has no name, so it cannot be addressed by reference.");
        }
      }

      ImGui::PopID();
    }

    if (componentManager.hasComponent<AnimatorComponent>(entity) && ImGui::CollapsingHeader("Animator", ImGuiTreeNodeFlags_DefaultOpen))
    {
      // CollapsingHeader does not open an ID scope, so every widget below would
      // otherwise sit at the window root alongside the legacy Animation and
      // Audio Source sections — which use the same visible labels ("Looping",
      // "Speed", "Play On Start"). Two live items with one ID share ImGui's
      // ActiveId, so dragging one "Speed" drags both. Scope the whole section.
      ImGui::PushID("animator_component");

      auto &animator = componentManager.getComponent<AnimatorComponent>(entity);
      auto &animationCache = AnimationClipCache::instance();

      // Nothing re-roots AnimationClipCache on a workspace switch the way
      // refresh_workspace_cache() re-roots ModelAssetCache, so every consumer
      // keeps it honest itself (see AnimationEditorPlugin::sync_asset_roots and
      // AnimatorGraphPlugin::sync_asset_root). Without this the combos below
      // scan `.hades/animators` relative to the process working directory and
      // come up empty. setAssetRoot drops every entry, hence the guard.
      if (animationCache.assetRoot() != activeWorkspacePath_)
      {
        animationCache.setAssetRoot(activeWorkspacePath_);
      }

      // AnimatorSystem iterates query<ModelComponent, AnimatorComponent> and
      // skips entities whose model asset failed to load, so an animator with
      // no model never runs. Say so here: "Add Component > Animator" does not
      // pull a ModelComponent in, and the failure is otherwise silent.
      if (!componentManager.hasComponent<ModelComponent>(entity))
      {
        ImGui::TextColored(
            ImVec4(0.88f, 0.72f, 0.34f, 1.0f),
            "No Model component. Add one with a skinned model or this animator\n"
            "will never be evaluated.");
      }
      else if (ModelAssetCache::instance().get(
                   componentManager.getComponent<ModelComponent>(entity).assetPath) == nullptr)
      {
        ImGui::TextColored(
            ImVec4(0.88f, 0.72f, 0.34f, 1.0f),
            "The Model component has no loaded asset, so there is no skeleton\n"
            "to pose and this animator will never be evaluated.");
      }

      const std::string graphPreview = animator.graphPath.empty() ? "(none)" : animator.graphPath;
      if (ImGui::BeginCombo("Graph", graphPreview.c_str()))
      {
        const bool noneSelected = animator.graphPath.empty();
        if (ImGui::Selectable("(none)", noneSelected))
        {
          animator.graphPath.clear();
        }
        if (noneSelected)
        {
          ImGui::SetItemDefaultFocus();
        }

        for (const auto &graphName : animationCache.listGraphs())
        {
          const bool selected = (animator.graphPath == graphName);
          if (ImGui::Selectable(graphName.c_str(), selected))
          {
            animator.graphPath = graphName;
          }
          if (selected)
          {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      const AnimatorGraph *graph =
          animator.graphPath.empty() ? nullptr : animationCache.graph(animator.graphPath);
      if (!animator.graphPath.empty() && graph == nullptr)
      {
        const std::string loadError = animationCache.errorFor(animator.graphPath);
        ImGui::TextColored(
            ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "Failed to load graph: %s",
            loadError.empty() ? "unknown error" : loadError.c_str());
      }

      // A graph owns state selection, so the default clip is only the clip-mode
      // fallback. Dim it rather than hide it, so the authored value stays visible.
      const bool graphDrivesPlayback = !animator.graphPath.empty();
      if (graphDrivesPlayback)
      {
        ImGui::BeginDisabled();
      }
      const std::string clipPreview = animator.defaultClip.empty() ? "(none)" : animator.defaultClip;
      if (ImGui::BeginCombo("Default Clip", clipPreview.c_str()))
      {
        const bool noneSelected = animator.defaultClip.empty();
        if (ImGui::Selectable("(none)", noneSelected))
        {
          animator.defaultClip.clear();
        }
        if (noneSelected)
        {
          ImGui::SetItemDefaultFocus();
        }

        // Animation that came inside the model file, addressable directly
        // rather than only after a bake into .hades/animations.
        std::vector<std::string> importedClips;
        if (componentManager.hasComponent<ModelComponent>(entity))
        {
          importedClips = animationCache.listImportedClips(
              componentManager.getComponent<ModelComponent>(entity).assetPath);
        }
        if (!importedClips.empty())
        {
          ImGui::TextDisabled("In this model");
          for (const auto &clipName : importedClips)
          {
            ImGui::PushID(clipName.c_str());
            const bool selected = (animator.defaultClip == clipName);
            if (ImGui::Selectable(clipName.c_str(), selected))
            {
              animator.defaultClip = clipName;
            }
            if (selected)
            {
              ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
          }
          ImGui::Separator();
          ImGui::TextDisabled("Authored clips");
        }

        for (const auto &clipName : animationCache.listClips())
        {
          const bool selected = (animator.defaultClip == clipName);
          if (ImGui::Selectable(clipName.c_str(), selected))
          {
            animator.defaultClip = clipName;
          }
          if (selected)
          {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }
      if (graphDrivesPlayback)
      {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
          ImGui::SetTooltip("The animator graph picks the clip to play.\n"
                            "Clear the graph to drive this entity from a single clip.");
        }
      }

      ImGui::Checkbox("Play On Start", &animator.playOnStart);
      ImGui::SameLine();
      ImGui::Checkbox("Looping", &animator.looping);
      ImGui::DragFloat("Speed", &animator.speed, 0.01f, 0.01f, 8.0f);
      ImGui::DragFloat("Default Blend", &animator.defaultBlendSeconds, 0.01f, 0.0f, 2.0f, "%.2f s");

      // DragFloat only clamps while dragging; ctrl+click typing needs this.
      animator.speed = std::clamp(animator.speed, 0.01f, 8.0f);
      animator.defaultBlendSeconds = std::clamp(animator.defaultBlendSeconds, 0.0f, 2.0f);

      ImGui::Separator();
      ImGui::TextDisabled("Parameter Overrides");

      if (animator.parameters.empty())
      {
        ImGui::TextDisabled("None. Overrides are applied to the animator when the entity starts playing.");
      }

      std::optional<std::size_t> removeParameterIndex;
      if (!animator.parameters.empty() &&
          ImGui::BeginTable(
              "##animator_parameters",
              4,
              ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
      {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableHeadersRow();

        for (std::size_t index = 0; index < animator.parameters.size(); ++index)
        {
          auto &parameter = animator.parameters[index];
          ImGui::PushID(static_cast<int>(index));
          ImGui::TableNextRow();

          ImGui::TableSetColumnIndex(0);
          ImGui::SetNextItemWidth(-FLT_MIN);
          if (graph != nullptr)
          {
            const std::string namePreview = parameter.name.empty() ? "<Select a parameter>" : parameter.name;
            if (ImGui::BeginCombo("##name", namePreview.c_str()))
            {
              for (const auto &graphParameter : graph->parameters)
              {
                const bool selected = (parameter.name == graphParameter.name);
                // The add-picker below refuses to override the same parameter
                // twice, so this one has to agree: two rows with one name are
                // both applied and the later silently wins.
                if (!selected &&
                    std::any_of(
                        animator.parameters.begin(), animator.parameters.end(),
                        [&graphParameter](const AnimatorParamOverride &other)
                        { return other.name == graphParameter.name; }))
                {
                  continue;
                }

                if (ImGui::Selectable(graphParameter.name.c_str(), selected))
                {
                  parameter.name = graphParameter.name;
                  parameter.type = animator_override_type_for(graphParameter.type);
                }
                if (selected)
                {
                  ImGui::SetItemDefaultFocus();
                }
              }
              ImGui::EndCombo();
            }
          }
          else
          {
            ImGui::InputText("##name", &parameter.name);
          }

          ImGui::TableSetColumnIndex(1);
          ImGui::SetNextItemWidth(-FLT_MIN);
          int parameterType = animator_param_type_index(parameter.type);
          if (ImGui::Combo("##type", &parameterType, ANIMATOR_PARAM_TYPE_LABELS, IM_ARRAYSIZE(ANIMATOR_PARAM_TYPE_LABELS)))
          {
            parameter.type = ANIMATOR_PARAM_TYPE_LABELS[parameterType];
          }

          ImGui::TableSetColumnIndex(2);
          ImGui::SetNextItemWidth(-FLT_MIN);
          if (parameter.type == "int")
          {
            ImGui::DragInt("##value", &parameter.intValue);
          }
          else if (parameter.type == "bool")
          {
            ImGui::Checkbox("##value", &parameter.boolValue);
          }
          else
          {
            ImGui::DragFloat("##value", &parameter.floatValue, 0.01f);
          }

          ImGui::TableSetColumnIndex(3);
          if (ImGui::SmallButton(ICON_FA_TRASH "##remove_parameter"))
          {
            removeParameterIndex = index;
          }

          ImGui::PopID();
        }

        ImGui::EndTable();
      }

      if (removeParameterIndex.has_value())
      {
        animator.parameters.erase(animator.parameters.begin() + static_cast<std::ptrdiff_t>(*removeParameterIndex));
      }

      if (graph != nullptr)
      {
        // With a graph bound the names are known, so offer them instead of a
        // free-text box an override could silently misspell.
        if (ImGui::BeginCombo("##add_parameter", ICON_FA_PLUS "  Add Override", ImGuiComboFlags_HeightLarge))
        {
          std::size_t offered = 0;
          for (const auto &graphParameter : graph->parameters)
          {
            const bool alreadyOverridden =
                std::any_of(
                    animator.parameters.begin(), animator.parameters.end(),
                    [&graphParameter](const AnimatorParamOverride &existing)
                    { return existing.name == graphParameter.name; });
            if (alreadyOverridden)
            {
              continue;
            }

            ++offered;
            if (ImGui::Selectable(graphParameter.name.c_str()))
            {
              AnimatorParamOverride added;
              added.name = graphParameter.name;
              added.type = animator_override_type_for(graphParameter.type);
              added.floatValue = graphParameter.floatValue;
              added.intValue = graphParameter.intValue;
              added.boolValue = graphParameter.boolValue;
              animator.parameters.push_back(added);
            }
          }

          if (offered == 0)
          {
            ImGui::TextDisabled("No parameters left to override.");
          }
          ImGui::EndCombo();
        }
      }
      else if (ImGui::Button(ICON_FA_PLUS "  Add Override"))
      {
        animator.parameters.push_back(AnimatorParamOverride{});
      }

      ImGui::Separator();

      if (state.isPlaying)
      {
        // Playback state lives in AnimationRuntime, not the component, so this
        // is read-only status rather than an editable field.
        const AnimatorInstance *player = AnimationRuntime::instance().find(entity);
        if (player == nullptr)
        {
          ImGui::TextDisabled("No animator instance for this entity yet.");
        }
        else
        {
          const std::string currentState = player->current_state();
          const std::string currentClip = player->current_clip();
          ImGui::TextDisabled(
              "State: %s   Clip: %s   t: %.2f%s",
              currentState.empty() ? "-" : currentState.c_str(),
              currentClip.empty() ? "-" : currentClip.c_str(),
              player->normalized_time(),
              player->is_transitioning() ? "   (blending)" : "");
        }
      }
      else
      {
        ImGui::TextDisabled("Live animator state appears here in play mode.");
      }

      if (ImGui::Button(ICON_FA_FILM "  Open in Animation editor"))
      {
        show_plugin("animation-editor");
      }
      ImGui::SameLine();
      if (ImGui::Button(ICON_FA_DIAGRAM_PROJECT "  Open Animator graph"))
      {
        show_plugin("animator-graph");
      }

      ImGui::PopID();
    }

    if (componentManager.hasComponent<RigidBodyComponent>(entity) && ImGui::CollapsingHeader("Rigid Body"))
    {
      auto &rb = componentManager.getComponent<RigidBodyComponent>(entity);

      int bodyType = static_cast<int>(rb.type);
      const char *bodyTypeLabels[] = {"Static", "Kinematic", "Dynamic"};
      if (ImGui::Combo("Body Type", &bodyType, bodyTypeLabels, IM_ARRAYSIZE(bodyTypeLabels)))
      {
        rb.type = static_cast<RigidBodyType>(bodyType);
      }

      if (rb.type == RigidBodyType::Dynamic)
      {
        ImGui::DragFloat("Mass", &rb.mass, 0.1f, 0.01f, 10000.0f);
      }
      ImGui::DragFloat("Linear Damping", &rb.linearDamping, 0.01f, 0.0f, 10.0f);
      ImGui::DragFloat("Angular Damping", &rb.angularDamping, 0.01f, 0.0f, 10.0f);
      ImGui::DragFloat("Friction", &rb.friction, 0.01f, 0.0f, 2.0f);
      ImGui::DragFloat("Restitution", &rb.restitution, 0.01f, 0.0f, 2.0f);
      ImGui::DragFloat("Gravity Scale", &rb.gravityScale, 0.1f, 0.0f, 10.0f);

      if (rb.mass < 0.01f)
      {
        rb.mass = 0.01f;
      }
    }

    if (componentManager.hasComponent<ColliderComponent>(entity) && ImGui::CollapsingHeader("Collider"))
    {
      auto &col = componentManager.getComponent<ColliderComponent>(entity);

      int shapeType = static_cast<int>(col.shape);
      const char *shapeLabels[] = {"Box", "Sphere", "Capsule"};
      if (ImGui::Combo("Shape", &shapeType, shapeLabels, IM_ARRAYSIZE(shapeLabels)))
      {
        col.shape = static_cast<ColliderShape>(shapeType);
      }

      switch (col.shape)
      {
      case ColliderShape::Box:
        ImGui::DragFloat3("Half Extents", &col.halfExtentX, 0.05f, 0.01f, 100.0f);
        break;
      case ColliderShape::Sphere:
        ImGui::DragFloat("Radius", &col.radius, 0.05f, 0.01f, 100.0f);
        break;
      case ColliderShape::Capsule:
        ImGui::DragFloat("Half Height", &col.capsuleHalfHeight, 0.05f, 0.01f, 100.0f);
        ImGui::DragFloat("Capsule Radius", &col.capsuleRadius, 0.05f, 0.01f, 100.0f);
        break;
      }
    }

    if (componentManager.hasComponent<CameraComponent>(entity) && ImGui::CollapsingHeader("Camera"))
    {
      auto &camera = componentManager.getComponent<CameraComponent>(entity);
      bool isMainCamera = camera.isMainCamera;

      if (ImGui::Checkbox("Main Camera", &isMainCamera))
      {
        if (isMainCamera)
        {
          set_main_camera(entity, entityManager, componentManager);
        }
        else
        {
          camera.isMainCamera = false;
        }

        state.playModeMessage.clear();
      }

      ImGui::DragFloat("Field of view", &camera.fovY, 0.5f, 1.0f, 179.0f);
      ImGui::DragFloat("Near clip", &camera.nearClip, 0.01f, 0.001f, camera.farClip);
      ImGui::DragFloat("Far clip", &camera.farClip, 1.0f, camera.nearClip, 10000.0f);

      if (camera.fovY < 1.0f)
      {
        camera.fovY = 1.0f;
      }
      else if (camera.fovY > 179.0f)
      {
        camera.fovY = 179.0f;
      }
      if (camera.nearClip < 0.001f)
      {
        camera.nearClip = 0.001f;
      }
      if (camera.farClip <= camera.nearClip)
      {
        camera.farClip = camera.nearClip + 0.001f;
      }
    }

    if (componentManager.hasComponent<LightComponent>(entity) && ImGui::CollapsingHeader("Light"))
    {
      auto &light = componentManager.getComponent<LightComponent>(entity);

      int lightType = static_cast<int>(light.type);
      const char *lightTypeLabels[] = {"Directional", "Point", "Spot"};
      if (ImGui::Combo("Light Type", &lightType, lightTypeLabels, IM_ARRAYSIZE(lightTypeLabels)))
      {
        light.type = static_cast<LightType>(lightType);
      }

      ImGui::Checkbox("Enabled", &light.enabled);
      ImGui::ColorEdit3("Color", &light.colorR);
      ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 10.0f);

      if (light.type == LightType::Directional || light.type == LightType::Spot)
      {
        ImGui::DragFloat3("Direction", &light.directionX, 0.01f, -1.0f, 1.0f);
        float len = std::sqrt(light.directionX * light.directionX +
                              light.directionY * light.directionY +
                              light.directionZ * light.directionZ);
        if (len > 1e-5f)
        {
          light.directionX /= len;
          light.directionY /= len;
          light.directionZ /= len;
        }
      }

      if (light.type == LightType::Point || light.type == LightType::Spot)
      {
        ImGui::DragFloat("Range", &light.range, 0.5f, 0.1f, 1000.0f);
      }

      if (light.type == LightType::Spot)
      {
        ImGui::DragFloat("Inner Cone Angle", &light.innerConeAngle, 0.5f, 0.0f, light.outerConeAngle);
        ImGui::DragFloat("Outer Cone Angle", &light.outerConeAngle, 0.5f, light.innerConeAngle, 89.0f);
      }

      ImGui::DragFloat("Ambient Contribution", &light.ambientContribution, 0.01f, 0.0f, 1.0f);
      ImGui::Checkbox("Cast Shadows", &light.castShadows);
      if (light.castShadows)
      {
        ImGui::TextDisabled("Shadow casting is not yet implemented.");
      }
    }

    if (componentManager.hasComponent<AudioListenerComponent>(entity) && ImGui::CollapsingHeader("Audio Listener"))
    {
      auto &listener = componentManager.getComponent<AudioListenerComponent>(entity);
      ImGui::Checkbox("Listener Enabled", &listener.enabled);
      ImGui::DragFloat3("Listener Forward", &listener.forwardX, 0.01f, -1.0f, 1.0f);
      ImGui::DragFloat3("Listener Up", &listener.upX, 0.01f, -1.0f, 1.0f);
    }

    if (componentManager.hasComponent<TextComponent>(entity) && ImGui::CollapsingHeader("Text", ImGuiTreeNodeFlags_DefaultOpen))
    {
      auto &text = componentManager.getComponent<TextComponent>(entity);
      ImGui::InputTextMultiline(
          "Content",
          &text.content,
          ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeightWithSpacing() * 6.0f));
      ImGui::DragFloat("Font Size", &text.fontSize, 0.05f, 0.05f, 64.0f);
      ImGui::DragFloat("Wrap Width", &text.wrapWidth, 0.05f, 0.0f, 256.0f);
      ImGui::DragFloat("Line Spacing", &text.lineSpacing, 0.01f, 0.8f, 4.0f);
      ImGui::DragFloat("Yaw", &text.yawDegrees, 0.5f, -180.0f, 180.0f, "%.1f deg");
      ImGui::DragFloat("Pitch", &text.pitchDegrees, 0.5f, -89.0f, 89.0f, "%.1f deg");
      ImGui::DragFloat("Roll", &text.rollDegrees, 0.5f, -180.0f, 180.0f, "%.1f deg");

      if (text.fontSize < 0.05f)
      {
        text.fontSize = 0.05f;
      }
      if (text.wrapWidth < 0.0f)
      {
        text.wrapWidth = 0.0f;
      }
      if (text.lineSpacing < 0.8f)
      {
        text.lineSpacing = 0.8f;
      }

      text.yawDegrees = std::remainder(text.yawDegrees, 360.0f);
      text.pitchDegrees = std::clamp(text.pitchDegrees, -89.0f, 89.0f);
      text.rollDegrees = std::remainder(text.rollDegrees, 360.0f);

      ImGui::TextDisabled("Text is rendered as world-space vector strokes.");
    }

    if (componentManager.hasComponent<AudioSourceComponent>(entity) && ImGui::CollapsingHeader("Audio Source"))
    {
      // "Pitch" here hashes to the same window-root ID as the Text section's
      // "Pitch"; an entity carrying both would drive both drags at once.
      ImGui::PushID("audio_source_component");

      auto &source = componentManager.getComponent<AudioSourceComponent>(entity);
      std::array<char, 260> assetPathBuffer{};
      std::snprintf(assetPathBuffer.data(), assetPathBuffer.size(), "%s", source.assetPath.c_str());

      if (ImGui::InputText("Audio Clip", assetPathBuffer.data(), assetPathBuffer.size()))
      {
        source.assetPath = assetPathBuffer.data();
      }

      int selectedBus = static_cast<int>(source.bus);
      const char *busLabels[] = {"Master", "Music", "SFX", "Voice"};
      if (ImGui::Combo("Audio Bus", &selectedBus, busLabels, IM_ARRAYSIZE(busLabels)))
      {
        source.bus = static_cast<AudioBus>(selectedBus);
      }

      ImGui::Checkbox("Play On Start", &source.playOnStart);
      ImGui::Checkbox("Looping", &source.looping);
      ImGui::Checkbox("Streaming", &source.streaming);
      ImGui::Checkbox("Spatialized", &source.spatialized);
      ImGui::SliderFloat("Volume", &source.volume, 0.0f, 2.0f);
      ImGui::SliderFloat("Pitch", &source.pitch, 0.1f, 4.0f);

      if (source.spatialized)
      {
        ImGui::DragFloat("Min Distance", &source.minDistance, 0.1f, 0.1f, 1000.0f);
        ImGui::DragFloat("Max Distance", &source.maxDistance, 0.5f, source.minDistance, 5000.0f);
        ImGui::SliderFloat("Rolloff", &source.rolloff, 0.1f, 4.0f);
      }

      if (source.pitch < 0.1f)
      {
        source.pitch = 0.1f;
      }
      if (source.minDistance < 0.1f)
      {
        source.minDistance = 0.1f;
      }
      if (source.maxDistance < source.minDistance)
      {
        source.maxDistance = source.minDistance;
      }
      if (source.rolloff < 0.1f)
      {
        source.rolloff = 0.1f;
      }

      ImGui::TextDisabled("Audio paths resolve relative to the engine process working directory.");

      ImGui::PopID();
    }

    if (componentManager.hasComponent<RenderComponent>(entity) && ImGui::CollapsingHeader("Render"))
    {
      const auto &render = componentManager.getComponent<RenderComponent>(entity);
      ImGui::Text("Program: %d", render.program);
    }

    if (componentManager.hasComponent<TransformHierarchyComponent>(entity) &&
        ImGui::CollapsingHeader("Transform Hierarchy"))
    {
      const auto &hierarchy = componentManager.getComponent<TransformHierarchyComponent>(entity);

      if (hierarchy.parent.has_value())
      {
        const Entity::EntityId parent = *hierarchy.parent;
        if (ImGui::Selectable(entity_label(parent, componentManager).c_str(), false))
        {
          select_entity(parent);
        }
      }
      else
      {
        ImGui::TextDisabled("%s", isWorld ? "World Root" : "Parent: Root");
      }

      if (hierarchy.children.empty())
      {
        ImGui::TextDisabled("No child entities.");
      }
      else
      {
        ImGui::Text("Children (%zu)", hierarchy.children.size());
        for (const Entity::EntityId child : hierarchy.children)
        {
          if (ImGui::Selectable(entity_label(child, componentManager).c_str(), false))
          {
            select_entity(child);
          }
        }
      }
    }

    if (componentManager.hasComponent<ScriptComponent>(entity))
    {
      auto &scriptComponent = componentManager.getComponent<ScriptComponent>(entity);
      std::optional<std::size_t> removeAttachmentIndex;

      if (scriptComponent.attachments.empty() && ImGui::CollapsingHeader("Scripts"))
      {
        ImGui::TextDisabled("No script components attached.");
      }

      for (std::size_t index = 0; index < scriptComponent.attachments.size(); ++index)
      {
        auto &attachment = scriptComponent.attachments[index];
        ImGui::PushID(static_cast<int>(index));

        const std::string label = script_component_label(attachment, index);
        const std::string collapsingHeaderId = label + "##script_component_panel";
        if (ImGui::CollapsingHeader(collapsingHeaderId.c_str()))
        {
          ImGui::Checkbox("Enabled", &attachment.enabled);
          std::vector<std::string> scriptOptions = workspaceScriptFiles_;
          if (!attachment.scriptPath.empty() &&
              std::find(scriptOptions.begin(), scriptOptions.end(), attachment.scriptPath) == scriptOptions.end())
          {
            scriptOptions.push_back(attachment.scriptPath);
            std::sort(scriptOptions.begin(), scriptOptions.end());
          }

          const std::string previousScriptPath = attachment.scriptPath;
          const std::string previewValue = attachment.scriptPath.empty() ? "<Select a workspace script>" : attachment.scriptPath;
          if (ImGui::BeginCombo("Script", previewValue.c_str()))
          {
            const bool noneSelected = attachment.scriptPath.empty();
            if (ImGui::Selectable("<None>", noneSelected))
            {
              attachment.scriptPath.clear();
            }
            if (noneSelected)
            {
              ImGui::SetItemDefaultFocus();
            }

            for (const auto &scriptPath : scriptOptions)
            {
              const bool selected = (attachment.scriptPath == scriptPath);
              if (ImGui::Selectable(scriptPath.c_str(), selected))
              {
                attachment.scriptPath = scriptPath;
              }
              if (selected)
              {
                ImGui::SetItemDefaultFocus();
              }
            }
            ImGui::EndCombo();
          }

          if (previousScriptPath != attachment.scriptPath)
          {
            attachment.className.clear();
            attachment.publicFieldValues.clear();
          }

          if (scriptOptions.empty())
          {
            ImGui::TextDisabled("No scripts.");
          }
          else if (!attachment.scriptPath.empty())
          {
            ImGui::TextDisabled("%s", attachment.scriptPath.c_str());
            if (!activeWorkspacePath_.empty() && ImGui::Button("Open in External Editor"))
            {
              open_in_external_editor(externalEditor_, activeWorkspacePath_, activeWorkspacePath_ / attachment.scriptPath);
            }
          }

          const std::vector<ParsedScriptClass> *parsedClasses = nullptr;
          const ParsedScriptClass *selectedClass = nullptr;
          if (!attachment.scriptPath.empty() && !activeWorkspacePath_.empty())
          {
            const auto resolvedPath = activeWorkspacePath_ / attachment.scriptPath;
            const std::string pathKey = resolvedPath.string();

            std::error_code modEc;
            const auto modTime = std::filesystem::last_write_time(resolvedPath, modEc);
            bool needsParse = false;
            if (!modEc)
            {
              auto modIt = parsedScriptModTimes_.find(pathKey);
              if (modIt == parsedScriptModTimes_.end() || modIt->second != modTime)
              {
                needsParse = true;
                parsedScriptModTimes_[pathKey] = modTime;
              }
            }
            else
            {
              parsedScriptCache_.erase(pathKey);
              parsedScriptModTimes_.erase(pathKey);
            }

            if (needsParse)
            {
              parsedScriptCache_[pathKey] = parse_script_classes(resolvedPath);
            }

            const auto cacheIt = parsedScriptCache_.find(pathKey);
            if (cacheIt != parsedScriptCache_.end())
            {
              parsedClasses = &cacheIt->second;
            }
          }

          if (previousScriptPath != attachment.scriptPath)
          {
            if (parsedClasses != nullptr && !parsedClasses->empty())
            {
              attachment.className = parsedClasses->front().qualifiedName;
            }
            else
            {
              attachment.className.clear();
            }
          }
          else if (parsedClasses != nullptr && !parsedClasses->empty() &&
                   find_script_class(*parsedClasses, attachment.className) == nullptr)
          {
            attachment.className = parsedClasses->front().qualifiedName;
          }
          else if (parsedClasses != nullptr && parsedClasses->empty())
          {
            attachment.className.clear();
          }

          if (parsedClasses != nullptr && !parsedClasses->empty())
          {
            selectedClass = find_script_class(*parsedClasses, attachment.className);
            if (selectedClass == nullptr)
            {
              attachment.className = parsedClasses->front().qualifiedName;
              selectedClass = &parsedClasses->front();
            }
          }

          if (attachment.scriptPath.empty())
          {
            ImGui::BeginDisabled();
            if (ImGui::BeginCombo("Class", "<Select a script first>"))
            {
              ImGui::EndCombo();
            }
            ImGui::EndDisabled();
          }
          else if (parsedClasses == nullptr || parsedClasses->empty())
          {
            ImGui::BeginDisabled();
            if (ImGui::BeginCombo("Class", "<No script classes found>"))
            {
              ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            ImGui::TextDisabled("No HadesScript classes found in this file.");
          }
          else if (selectedClass != nullptr &&
                   ImGui::BeginCombo("Class", selectedClass->qualifiedName.c_str()))
          {
            for (const auto &parsedClass : *parsedClasses)
            {
              const bool selected = (selectedClass == &parsedClass);
              if (ImGui::Selectable(parsedClass.qualifiedName.c_str(), selected))
              {
                attachment.className = parsedClass.qualifiedName;
                selectedClass = find_script_class(*parsedClasses, attachment.className);
              }
              if (selected)
              {
                ImGui::SetItemDefaultFocus();
              }
            }
            ImGui::EndCombo();
          }

          // Model Path — path to a trained `.pt` policy that drives this
          // attachment at play time. Empty = legacy onUpdate (or training-owned
          // when the script is the subject of the Neural Training panel).
          {
            std::array<char, 512> modelBuffer{};
            std::snprintf(modelBuffer.data(), modelBuffer.size(), "%s",
                          attachment.modelPath.c_str());
            if (ImGui::InputText("Model Path", modelBuffer.data(), modelBuffer.size()))
            {
              attachment.modelPath = modelBuffer.data();
            }
            ImGui::TextDisabled(
                "Empty = legacy onUpdate, or training-owned. "
                "Set to e.g. .hades/policies/<run>/policy.pt after training.");
          }

          if (ImGui::Button("Remove Script Component"))
          {
            removeAttachmentIndex = index;
          }

          if (selectedClass != nullptr && !selectedClass->publicFields.empty())
          {
            const auto &fields = selectedClass->publicFields;

            std::set<std::string> currentFieldNames;
            for (const auto &[type, name] : fields)
            {
              currentFieldNames.insert(name);
            }
            for (auto it = attachment.publicFieldValues.begin(); it != attachment.publicFieldValues.end();)
            {
              if (currentFieldNames.find(it->first) == currentFieldNames.end())
              {
                it = attachment.publicFieldValues.erase(it);
              }
              else
              {
                ++it;
              }
            }

            ImGui::Separator();
            ImGui::TextDisabled("Public Fields:");
            for (const auto &[type, name] : fields)
            {
              auto &value = attachment.publicFieldValues[name];
              std::array<char, 256> fieldBuffer{};
              std::snprintf(fieldBuffer.data(), fieldBuffer.size(), "%s", value.c_str());

              const std::string fieldLabel = name + " (" + type + ")";
              if (ImGui::InputText(fieldLabel.c_str(), fieldBuffer.data(), fieldBuffer.size()))
              {
                value = fieldBuffer.data();
              }
            }
          }
        }

        ImGui::PopID();
      }

      if (removeAttachmentIndex.has_value())
      {
        scriptComponent.attachments.erase(scriptComponent.attachments.begin() + static_cast<std::ptrdiff_t>(*removeAttachmentIndex));
      }
    }

    if (componentManager.hasComponent<BlueprintComponent>(entity))
    {
      auto &blueprintComponent = componentManager.getComponent<BlueprintComponent>(entity);
      std::optional<std::size_t> removeBlueprintIndex;

      if (blueprintComponent.attachments.empty() && ImGui::CollapsingHeader("Blueprints"))
      {
        ImGui::TextDisabled("No Blueprints attached.");
      }

      for (std::size_t index = 0; index < blueprintComponent.attachments.size(); ++index)
      {
        auto &attachment = blueprintComponent.attachments[index];
        ImGui::PushID(static_cast<int>(index) + 9000);

        const std::string suffix = attachment.assetPath.empty()
                                       ? std::string("<none>")
                                       : std::filesystem::path(attachment.assetPath).stem().string();
        const std::string header = "Blueprint: " + suffix + "##blueprint_component_panel";

        if (ImGui::CollapsingHeader(header.c_str()))
        {
          ImGui::Checkbox("Enabled", &attachment.enabled);

          std::vector<std::string> options = workspaceBlueprintFiles_;
          if (!attachment.assetPath.empty() &&
              std::find(options.begin(), options.end(), attachment.assetPath) == options.end())
          {
            options.push_back(attachment.assetPath);
            std::sort(options.begin(), options.end());
          }

          const std::string previousPath = attachment.assetPath;
          const std::string preview =
              attachment.assetPath.empty() ? "<Select a Blueprint>" : attachment.assetPath;

          if (ImGui::BeginCombo("Blueprint", preview.c_str()))
          {
            if (ImGui::Selectable("<None>", attachment.assetPath.empty()))
            {
              attachment.assetPath.clear();
            }
            for (const auto &option : options)
            {
              const bool selected = attachment.assetPath == option;
              if (ImGui::Selectable(option.c_str(), selected))
              {
                attachment.assetPath = option;
              }
              if (selected)
              {
                ImGui::SetItemDefaultFocus();
              }
            }
            ImGui::EndCombo();
          }

          // Overrides are keyed by variable name, so a different asset starts
          // from a clean slate.
          if (previousPath != attachment.assetPath)
          {
            attachment.variableOverrides.clear();
          }

          if (options.empty())
          {
            ImGui::TextDisabled("No .hbp assets in this workspace yet.");
          }

          if (!attachment.assetPath.empty())
          {
            if (ImGui::Button("Open in Blueprint Editor"))
            {
              request_blueprint_editor_open(attachment.assetPath);
              show_plugin(kBlueprintEditorPluginId);
            }

            if (const Blueprint *blueprint = inspector_blueprint(attachment.assetPath))
            {
              std::vector<const BlueprintVariable *> exposed;
              for (const auto &variable : blueprint->variables)
              {
                if (variable.exposed)
                {
                  exposed.push_back(&variable);
                }
              }

              // Drop overrides for variables that no longer exist.
              for (auto it = attachment.variableOverrides.begin(); it != attachment.variableOverrides.end();)
              {
                const bool stillExists = std::any_of(
                    exposed.begin(),
                    exposed.end(),
                    [&it](const BlueprintVariable *variable)
                    { return variable->name == it->first; });
                it = stillExists ? std::next(it) : attachment.variableOverrides.erase(it);
              }

              if (exposed.empty())
              {
                ImGui::TextDisabled("This Blueprint exposes no variables.");
              }
              else
              {
                ImGui::Separator();
                ImGui::TextDisabled("Instance Variables:");

                for (const BlueprintVariable *variable : exposed)
                {
                  const auto existing = attachment.variableOverrides.find(variable->name);
                  const bool overridden = existing != attachment.variableOverrides.end();

                  BlueprintValue value =
                      overridden
                          ? BlueprintValue::parse(existing->second, variable->type)
                          : variable->defaultValue.coerced_to(variable->type);

                  ImGui::PushID(variable->name.c_str());
                  bool changed = false;

                  switch (variable->type)
                  {
                  case ValueType::Bool:
                  {
                    bool raw = value.as_bool();
                    changed = ImGui::Checkbox(variable->name.c_str(), &raw);
                    if (changed)
                    {
                      value = BlueprintValue::from_bool(raw);
                    }
                    break;
                  }
                  case ValueType::Int:
                  {
                    int raw = value.as_int();
                    changed = ImGui::DragInt(variable->name.c_str(), &raw);
                    if (changed)
                    {
                      value = BlueprintValue::from_int(raw);
                    }
                    break;
                  }
                  case ValueType::Vector:
                  {
                    math::Vec3 raw = value.as_vector();
                    float components[3] = {raw.x, raw.y, raw.z};
                    changed = ImGui::DragFloat3(variable->name.c_str(), components, 0.05f);
                    if (changed)
                    {
                      value = BlueprintValue::from_vector(
                          math::Vec3(components[0], components[1], components[2]));
                    }
                    break;
                  }
                  case ValueType::String:
                  {
                    std::array<char, 256> buffer{};
                    std::snprintf(buffer.data(), buffer.size(), "%s", value.as_string().c_str());
                    changed = ImGui::InputText(variable->name.c_str(), buffer.data(), buffer.size());
                    if (changed)
                    {
                      value = BlueprintValue::from_string(buffer.data());
                    }
                    break;
                  }
                  case ValueType::Entity:
                  {
                    // Entity handles are runtime ids; there is nothing stable to
                    // pin down here, and routing them through the float editor
                    // below would turn None into entity 0.
                    ImGui::TextDisabled("%s (entity, set at runtime)", variable->name.c_str());
                    break;
                  }
                  case ValueType::Float:
                  default:
                  {
                    float raw = value.as_float();
                    changed = ImGui::DragFloat(variable->name.c_str(), &raw, 0.05f);
                    if (changed)
                    {
                      value = BlueprintValue::from_float(raw);
                    }
                    break;
                  }
                  }

                  if (changed)
                  {
                    attachment.variableOverrides[variable->name] = value.to_storage_string();
                  }

                  if (overridden)
                  {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reset"))
                    {
                      attachment.variableOverrides.erase(variable->name);
                    }
                  }

                  ImGui::PopID();
                }
              }
            }
            else
            {
              ImGui::TextColored(
                  ImVec4(0.88f, 0.42f, 0.42f, 1.0f),
                  "Could not read %s",
                  attachment.assetPath.c_str());
            }
          }

          if (ImGui::Button("Remove Blueprint"))
          {
            removeBlueprintIndex = index;
          }
        }

        ImGui::PopID();
      }

      if (removeBlueprintIndex.has_value())
      {
        blueprintComponent.attachments.erase(
            blueprintComponent.attachments.begin() + static_cast<std::ptrdiff_t>(*removeBlueprintIndex));
      }
    }

    ImGui::End();
  }

  const Blueprint *Editor::inspector_blueprint(const std::string &relativePath)
  {
    if (relativePath.empty() || activeWorkspacePath_.empty())
    {
      return nullptr;
    }

    const auto resolved = activeWorkspacePath_ / relativePath;

    std::error_code errorCode;
    const auto modTime = std::filesystem::last_write_time(resolved, errorCode);
    if (errorCode)
    {
      blueprintCache_.erase(relativePath);
      blueprintModTimes_.erase(relativePath);
      return nullptr;
    }

    const auto cachedTime = blueprintModTimes_.find(relativePath);
    const auto cached = blueprintCache_.find(relativePath);
    if (cached != blueprintCache_.end() && cachedTime != blueprintModTimes_.end() &&
        cachedTime->second == modTime)
    {
      return &cached->second;
    }

    Blueprint blueprint;
    if (!load_blueprint(resolved, blueprint, nullptr))
    {
      blueprintCache_.erase(relativePath);
      blueprintModTimes_.erase(relativePath);
      return nullptr;
    }

    blueprintModTimes_[relativePath] = modTime;
    auto &stored = blueprintCache_[relativePath];
    stored = std::move(blueprint);
    return &stored;
  }
}
