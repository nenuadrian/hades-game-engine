#include "animation_editor_plugin.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>

#include "imgui.h"

#include "../../engine/animation/animation_clip_cache.hpp"
#include "../../engine/animation/animation_runtime.hpp"
#include "../../engine/assets/model_asset.hpp"
#include "../../engine/assets/model_asset_cache.hpp"
#include "../../engine/components/model_component.hpp"
#include "../../engine/components/name_component.hpp"
#include "../../engine/core/ecs/component_manager.hpp"
#include "../../engine/core/ecs/entity_factory.hpp"
#include "../../engine/core/ecs/entity_manager.hpp"
#include "../../engine/core/ecs/world_utils.hpp"
#include "../IconsFontAwesome6.h"
#include "../animation_edit_state.hpp"
#include "../editor.hpp"
#include "../types.h"

namespace hades
{
  namespace
  {
    constexpr const char *kPanelTitle = ICON_FA_FILM "  Animation";
    constexpr const char *kNewClipPopup = "New Animation Clip";
    constexpr const char *kRenameClipPopup = "Rename Animation Clip";
    constexpr const char *kDeleteClipPopup = "Delete Animation Clip";
    constexpr const char *kSwitchClipPopup = "Unsaved Clip Changes";

    /// Rig joints get within this many entries of the shader palette limit
    /// before the panel starts warning: past it, apply_rig refuses the rig
    /// outright, and finding that out at save time is too late.
    constexpr std::size_t kBoneWarningMargin = 16;
    constexpr std::size_t kMaxUndoEntries = 64;
    constexpr float kTimeEpsilon = 1e-4f;

    ImVec4 error_color() { return ImVec4(0.88f, 0.42f, 0.42f, 1.0f); }
    ImVec4 warning_color() { return ImVec4(0.93f, 0.76f, 0.33f, 1.0f); }
    ImVec4 dim_color() { return ImVec4(0.55f, 0.56f, 0.60f, 1.0f); }
    ImVec4 accent_color() { return ImVec4(0.45f, 0.78f, 0.55f, 1.0f); }

    template <std::size_t Size>
    void set_buffer_text(std::array<char, Size> &buffer, const std::string &value)
    {
      buffer.fill('\0');
      const std::size_t copyLength = std::min(value.size(), Size - 1);
      std::copy_n(value.data(), copyLength, buffer.data());
      buffer[copyLength] = '\0';
    }

    std::string lower_copy(const std::string &value)
    {
      std::string result = value;
      std::transform(
          result.begin(),
          result.end(),
          result.begin(),
          [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
      return result;
    }

    bool contains_ci(const std::string &haystack, const std::string &needle)
    {
      if (needle.empty())
      {
        return true;
      }
      return lower_copy(haystack).find(lower_copy(needle)) != std::string::npos;
    }

    /// Imported clip names carry spaces, pipes and mixer artefacts
    /// ("Armature|Run"), none of which belong in a file name.
    std::string sanitize_asset_name(const std::string &value)
    {
      std::string result;
      result.reserve(value.size());
      for (const char character : value)
      {
        const unsigned char raw = static_cast<unsigned char>(character);
        if (std::isalnum(raw) != 0 || character == '_' || character == '-')
        {
          result.push_back(character);
        }
        else if (!result.empty() && result.back() != '_')
        {
          result.push_back('_');
        }
      }
      while (!result.empty() && result.back() == '_')
      {
        result.pop_back();
      }
      return result.empty() ? std::string("clip") : result;
    }

    std::string unique_name(const std::string &base, const std::vector<std::string> &taken)
    {
      const auto used = [&taken](const std::string &candidate)
      {
        return std::find(taken.begin(), taken.end(), candidate) != taken.end();
      };

      if (!used(base))
      {
        return base;
      }

      for (int suffix = 1; suffix < 1000; ++suffix)
      {
        const std::string candidate = base + "_" + std::to_string(suffix);
        if (!used(candidate))
        {
          return candidate;
        }
      }
      return base;
    }

    bool is_model_extension(const std::filesystem::path &path)
    {
      const std::string extension = lower_copy(path.extension().string());
      return extension == ".fbx" || extension == ".obj" || extension == ".gltf" ||
             extension == ".glb" || extension == ".dae";
    }

    std::string entity_label(Entity::EntityId entity, ComponentManager &componentManager)
    {
      if (componentManager.hasComponent<NameComponent>(entity))
      {
        const std::string &name = componentManager.getComponent<NameComponent>(entity).value;
        if (!name.empty())
        {
          return name + " (#" + std::to_string(entity) + ")";
        }
      }
      return "Entity #" + std::to_string(entity);
    }

    /// A small icon button that renders greyed out and swallows its click
    /// when disabled — ImGui::BeginDisabled around every transport button
    /// would be four extra lines each.
    bool tool_button(const char *label, const char *tooltip, bool enabled = true)
    {
      ImGui::BeginDisabled(!enabled);
      const bool pressed = ImGui::Button(label);
      ImGui::EndDisabled();
      if (tooltip != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
        ImGui::SetTooltip("%s", tooltip);
      }
      return pressed && enabled;
    }
  }

  AnimationEditorPlugin::AnimationEditorPlugin()
  {
    timeline_.viewStart = 0.0f;
    timeline_.viewEnd = clip_.duration;
    undoBaseline_ = clip_.to_json();
  }

  AnimationEditorPlugin::~AnimationEditorPlugin()
  {
    // The runtime outlives the editor's plugin list on shutdown, so a preview
    // left published here would keep a dead entity's palette alive.
    release_preview();
  }

  // ---- Frame flow ----------------------------------------------------------

  void AnimationEditorPlugin::render(EditorPluginContext &context)
  {
    // render() runs every frame regardless of visibility, which is what makes
    // it the right place to notice that the panel should stop previewing.
    if (!visible_)
    {
      release_preview();
      return;
    }

    if (focusRequested_)
    {
      ImGui::SetNextWindowFocus();
      focusRequested_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(1100.0f, 700.0f), ImGuiCond_FirstUseEver);
    bool open = visible_;
    if (!ImGui::Begin(kPanelTitle, &open))
    {
      ImGui::End();
      visible_ = open;
      // Collapsed counts as "not rendering": the preview would otherwise
      // hold the character at whatever frame the user last scrubbed to.
      release_preview();
      return;
    }
    visible_ = open;

    if (context.editor.state.isPlaying)
    {
      // The animator owns the character while the game runs. A preview
      // palette published here would override it and freeze the character
      // at whatever frame the play head sits on.
      release_preview();
      playing_ = false;
      ImGui::TextDisabled("Stop play mode to author animations.");
    }
    else
    {
      handle_shortcuts();
      draw_panel(context);
    }

    ImGui::End();

    if (!visible_)
    {
      release_preview();
    }
  }

  void AnimationEditorPlugin::draw_panel(EditorPluginContext &context)
  {
    if (context.workspacePath.empty())
    {
      ImGui::TextDisabled("Open a workspace to author animations.");
      release_preview();
      return;
    }

    sync_asset_roots(context);
    draw_target_bar(context);
    ImGui::Separator();

    const ModelAsset *asset = resolve_target_model(context);
    if (asset == nullptr)
    {
      release_preview();
      if (targetModelPath_.empty())
      {
        ImGui::TextDisabled("Pick an entity with a model, or a model file, to begin.");
      }
      draw_status_lines();
      return;
    }

    sync_skeleton(*asset);
    consume_viewport_input(*asset);
    advance_playback(context.deltaTime, *asset);
    update_preview_pose();

    if (ImGui::BeginTabBar("##animation-tabs", ImGuiTabBarFlags_None))
    {
      if (ImGui::BeginTabItem(ICON_FA_PERSON_RUNNING "  Animate"))
      {
        activeTab_ = 0;
        draw_animate_tab(context, *asset);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem(ICON_FA_BONE "  Rig"))
      {
        activeTab_ = 1;
        draw_rig_tab(context, *asset);
        ImGui::EndTabItem();
      }

      const std::string clipsLabel =
          std::string(ICON_FA_CLAPPERBOARD "  Clips") + (clipDirty_ ? " *" : "") + "###clips-tab";
      if (ImGui::BeginTabItem(clipsLabel.c_str()))
      {
        activeTab_ = 2;
        draw_clips_tab(context, *asset);
        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }

    // "Save rig" in the Rig tab calls ModelAssetCache::invalidate(), which
    // erases the cache entry and destroys the ModelAsset `asset` points at.
    // Everything past the tab bar therefore has to work from a freshly
    // resolved pointer, or it reads through a dangling reference.
    const ModelAsset *live = ModelAssetCache::instance().get(targetModelPath_);
    if (live == nullptr)
    {
      // The re-import failed (a rig that no longer applies, say). The target
      // bar reports the error next frame; there is nothing to preview now.
      release_preview();
      draw_status_lines();
      refresh_undo_baseline();
      return;
    }

    if (live != asset || skeletonNodeCount_ != skeleton_.size())
    {
      // Re-imported underneath us: the skeleton built at the top of this
      // frame describes the joints the *old* asset had.
      sync_skeleton(*live);
    }

    draw_clip_dialogs(context, *live);

    // Published last so the viewport draws exactly what this frame's edits
    // produced, with no one-frame lag behind the panel.
    publish_preview(*live);

    // Every mutation for this frame has landed, so a snapshot taken now is
    // the state the next edit should undo back to.
    refresh_undo_baseline();
  }

  void AnimationEditorPlugin::sync_asset_roots(EditorPluginContext &context)
  {
    // Plugins are constructed once and outlive every workspace switch, so the
    // panel has to notice the switch itself. It cannot do that by watching
    // the clip cache's root: Editor::refresh_workspace_cache re-roots that
    // cache before any plugin renders, so the root already matches here.
    const bool workspaceChanged =
        !configuredWorkspace_.empty() && configuredWorkspace_ != context.workspacePath;
    configuredWorkspace_ = context.workspacePath;

    // Defensive only, now that the editor owns the root: setAssetRoot drops
    // every entry, hence the guard.
    if (AnimationClipCache::instance().assetRoot() != context.workspacePath)
    {
      AnimationClipCache::instance().setAssetRoot(context.workspacePath);
      clipListLoaded_ = false;
    }

    if (!workspaceChanged)
    {
      return;
    }

    // Everything below names an asset by a workspace-relative stem, so
    // carrying any of it into the new project means writing project A's
    // clips and rig over project B's files under the same names.
    clear_target();
    clip_ = AnimationClipAsset{};
    clipName_.clear();
    clipLoaded_ = false;
    clipDirty_ = false;
    // A stale clip list is what would defeat new_clip()'s unique_name()
    // guard: a name that is free in A but taken in B would look free.
    clipListLoaded_ = false;
    clipList_.clear();
    reset_undo();
    timeline_.selection.clear();
    timeline_.time = 0.0f;
    timeline_.viewStart = 0.0f;
    timeline_.viewEnd = clip_.duration;
    clear_pose_override();
    errorMessage_.clear();
    clipListError_.clear();
    rigError_.clear();
    saveWorkingClipOnCreate_ = false;
    openNewClipPopup_ = false;
    openRenameClipPopup_ = false;
    openDeleteClipPopup_ = false;
    openSwitchClipPopup_ = false;
    pendingClipSwitch_.clear();
    pendingClipTarget_.clear();
    statusMessage_ = "Workspace changed; the open clip was closed.";
  }

  void AnimationEditorPlugin::handle_shortcuts()
  {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
      return;
    }

    const ImGuiIO &io = ImGui::GetIO();
    if (io.WantTextInput)
    {
      return;
    }

    const bool commandDown = io.KeyCtrl || io.KeySuper;
    if (!commandDown || !ImGui::IsKeyPressed(ImGuiKey_Z, false))
    {
      return;
    }

    if (io.KeyShift)
    {
      redo();
    }
    else
    {
      undo();
    }
  }

  void AnimationEditorPlugin::draw_status_lines()
  {
    if (!errorMessage_.empty())
    {
      ImGui::TextColored(error_color(), ICON_FA_TRIANGLE_EXCLAMATION "  %s", errorMessage_.c_str());
    }
    if (!statusMessage_.empty())
    {
      ImGui::TextColored(dim_color(), "%s", statusMessage_.c_str());
    }
  }

  // ---- Target --------------------------------------------------------------

  void AnimationEditorPlugin::draw_target_bar(EditorPluginContext &context)
  {
    targetCandidates_.clear();
    const auto activeWorld = context.editor.state.activeWorld;
    for (const Entity::EntityId entity : context.entityManager.getActiveEntities())
    {
      if (!context.componentManager.hasComponent<ModelComponent>(entity))
      {
        continue;
      }
      if (activeWorld.has_value() &&
          !entity_belongs_to_world(entity, *activeWorld, context.componentManager))
      {
        continue;
      }
      targetCandidates_.push_back(entity);
    }

    // Following the selection is what makes the panel feel attached to the
    // viewport: click a character, its rig is what you are editing.
    if (followSelection_ && context.editor.state.selectedEntity.has_value())
    {
      const Entity::EntityId selected = *context.editor.state.selectedEntity;
      const bool selectable =
          std::find(targetCandidates_.begin(), targetCandidates_.end(), selected) !=
          targetCandidates_.end();
      if (selectable && (!targetEntity_.has_value() || *targetEntity_ != selected))
      {
        set_target_entity(context, selected);
      }
    }

    std::string preview = "<no target>";
    if (targetEntity_.has_value())
    {
      preview = entity_label(*targetEntity_, context.componentManager);
    }
    else if (!targetModelPath_.empty())
    {
      preview = targetModelPath_;
    }

    ImGui::SetNextItemWidth(300.0f);
    if (ImGui::BeginCombo(ICON_FA_CUBE "  Target", preview.c_str()))
    {
      if (ImGui::Selectable("<no target>", !targetEntity_.has_value() && targetModelPath_.empty()))
      {
        clear_target();
      }

      for (const Entity::EntityId entity : targetCandidates_)
      {
        ImGui::PushID(static_cast<int>(entity));
        const bool selected = targetEntity_.has_value() && *targetEntity_ == entity;
        if (ImGui::Selectable(entity_label(entity, context.componentManager).c_str(), selected))
        {
          set_target_entity(context, entity);
        }
        if (selected)
        {
          ImGui::SetItemDefaultFocus();
        }
        ImGui::PopID();
      }

      // Models straight off disk, so a rig can be built before anything in
      // the scene references it.
      refresh_model_list(context);
      if (!modelFiles_.empty())
      {
        ImGui::Separator();
        ImGui::TextDisabled("Workspace models");
        for (const std::string &file : modelFiles_)
        {
          ImGui::PushID(file.c_str());
          const bool selected = !targetEntity_.has_value() && targetModelPath_ == file;
          if (ImGui::Selectable(file.c_str(), selected))
          {
            release_preview();
            targetEntity_.reset();
            targetModelPath_ = file;
          }
          ImGui::PopID();
        }
      }

      ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::Checkbox("Follow selection", &followSelection_);

    if (targetModelPath_.empty())
    {
      ImGui::TextDisabled("No model bound.");
      return;
    }

    ImGui::TextColored(dim_color(), ICON_FA_LINK "  %s", targetModelPath_.c_str());

    const std::string loadError = ModelAssetCache::instance().errorFor(targetModelPath_);
    if (!loadError.empty())
    {
      ImGui::TextColored(
          error_color(), ICON_FA_TRIANGLE_EXCLAMATION "  Model failed to load: %s",
          loadError.c_str());
    }

    // Previewing poses an entity in the viewport, so a bare model file off
    // disk has nothing to show. Rigging and keyframing still work — say so
    // rather than leaving the viewport mysteriously empty.
    if (!targetEntity_.has_value())
    {
      ImGui::TextColored(
          dim_color(),
          ICON_FA_CIRCLE_INFO "  Rig and key this model here, but the viewport has nothing to pose.");
      ImGui::SameLine();
      if (ImGui::SmallButton(ICON_FA_PLUS "  Add to world"))
      {
        spawn_preview_entity(context);
      }
      if (ImGui::IsItemHovered())
      {
        ImGui::SetTooltip(
            "Creates an entity in the open world with this model, and targets it,\n"
            "so the timeline previews on the mesh. Delete it like any other entity.");
      }
    }
  }

  void AnimationEditorPlugin::spawn_preview_entity(EditorPluginContext &context)
  {
    if (targetModelPath_.empty())
    {
      return;
    }

    const std::optional<Entity::EntityId> world = context.editor.state.loadedWorld;
    if (!world.has_value())
    {
      errorMessage_ = "Open a world first — a preview entity has to live in one.";
      return;
    }

    const Entity::EntityId entity =
        EntityFactory::createModel(context.entityManager, context.componentManager, world);
    context.componentManager.getComponent<ModelComponent>(entity).assetPath = targetModelPath_;

    // Name it after the file so it is obvious in the hierarchy — this is a
    // normal entity the user owns, not hidden editor state.
    std::string label = std::filesystem::path(targetModelPath_).stem().string();
    if (label.empty())
    {
      label = "Model";
    }
    if (context.componentManager.hasComponent<NameComponent>(entity))
    {
      context.componentManager.getComponent<NameComponent>(entity).value = label;
    }

    release_preview();
    targetEntity_ = entity;
    context.editor.state.selectedEntity = entity;
    errorMessage_.clear();
    statusMessage_ = "Added '" + label + "' to the world.";
  }

  const ModelAsset *AnimationEditorPlugin::resolve_target_model(EditorPluginContext &context)
  {
    if (targetEntity_.has_value())
    {
      const Entity::EntityId entity = *targetEntity_;
      const bool alive =
          std::find(targetCandidates_.begin(), targetCandidates_.end(), entity) !=
          targetCandidates_.end();
      if (!alive || !context.componentManager.hasComponent<ModelComponent>(entity))
      {
        // The entity was deleted, moved to another world, or lost its model.
        clear_target();
      }
      else
      {
        targetModelPath_ = context.componentManager.getComponent<ModelComponent>(entity).assetPath;
      }
    }

    if (targetModelPath_.empty())
    {
      return nullptr;
    }

    if (boundModelPath_ != targetModelPath_)
    {
      // A different model means a different skeleton: nothing indexed by
      // joint survives the switch.
      boundModelPath_ = targetModelPath_;
      selectedJoint_ = -1;
      selectedRigJoint_ = -1;
      selectedMeshIndex_ = 0;
      rigLoaded_ = false;
      rigDirty_ = false;
      rig_ = RigAsset{};
      rigError_.clear();
      clear_pose_override();
    }

    // Resolved every frame on purpose: the cache is cleared wholesale on a
    // workspace change, so a pointer kept across frames dangles.
    return ModelAssetCache::instance().get(targetModelPath_);
  }

  void AnimationEditorPlugin::set_target_entity(EditorPluginContext &context, Entity::EntityId entity)
  {
    if (!context.componentManager.hasComponent<ModelComponent>(entity))
    {
      return;
    }

    release_preview();
    targetEntity_ = entity;
    targetModelPath_ = context.componentManager.getComponent<ModelComponent>(entity).assetPath;
    selectedJoint_ = -1;
    selectedRigJoint_ = -1;
    clear_pose_override();
    errorMessage_.clear();
  }

  void AnimationEditorPlugin::clear_target()
  {
    release_preview();
    targetEntity_.reset();
    targetModelPath_.clear();
    boundModelPath_.clear();
    skeleton_ = Skeleton{};
    skeletonSource_ = nullptr;
    skeletonNodeCount_ = 0;
    selectedJoint_ = -1;
    selectedRigJoint_ = -1;
    rigLoaded_ = false;
    rig_ = RigAsset{};
    clear_pose_override();
  }

  void AnimationEditorPlugin::refresh_model_list(EditorPluginContext &context)
  {
    if (scannedWorkspace_ == context.workspacePath)
    {
      return;
    }

    scannedWorkspace_ = context.workspacePath;
    modelFiles_.clear();

    std::error_code errorCode;
    std::filesystem::recursive_directory_iterator iterator(
        context.workspacePath, std::filesystem::directory_options::skip_permission_denied, errorCode);
    if (errorCode)
    {
      return;
    }

    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(errorCode))
    {
      if (errorCode)
      {
        break;
      }

      const std::filesystem::path &path = iterator->path();
      const std::string filename = path.filename().string();
      if (!filename.empty() && filename.front() == '.')
      {
        // Skips .hades, .git and friends without stat-ing their contents.
        if (iterator->is_directory(errorCode))
        {
          iterator.disable_recursion_pending();
        }
        continue;
      }

      if (!iterator->is_regular_file(errorCode) || !is_model_extension(path))
      {
        continue;
      }

      const std::filesystem::path relative = path.lexically_relative(context.workspacePath);
      modelFiles_.push_back(relative.generic_string());
      if (modelFiles_.size() >= 512)
      {
        break;
      }
    }

    std::sort(modelFiles_.begin(), modelFiles_.end());
  }

  void AnimationEditorPlugin::sync_skeleton(const ModelAsset &asset)
  {
    if (selectedJoint_ < -1)
    {
      selectedJoint_ = -1;
    }

    // Pointer identity alone is not enough: the cache can drop an asset and
    // re-import its replacement onto the same address, so the node count is
    // checked too — and the two sites that destroy the asset (clear_target,
    // save_rig) null the pointer as well. Rebuilding unconditionally costs
    // 810 allocations a frame at 200 joints and repairs nothing: skeleton_ is
    // only ever assigned here and in clear_target.
    if (skeletonSource_ == &asset && skeletonNodeCount_ == asset.nodes.size())
    {
      return;
    }

    skeleton_ = Skeleton::from_model(asset);
    skeletonSource_ = &asset;

    if (skeletonNodeCount_ != skeleton_.size())
    {
      skeletonNodeCount_ = skeleton_.size();
      // Every joint index the panel holds refers to the old numbering.
      if (selectedJoint_ >= static_cast<int>(skeleton_.size()))
      {
        selectedJoint_ = skeleton_.empty() ? -1 : static_cast<int>(skeleton_.size()) - 1;
      }
      clear_pose_override();
    }
  }

  // ---- Preview -------------------------------------------------------------

  void AnimationEditorPlugin::release_preview()
  {
    if (!previewPublished_)
    {
      return;
    }

    if (previewEntity_ != Entity::INVALID)
    {
      AnimationRuntime::instance().clear_preview(previewEntity_);
    }
    animation_edit_state().deactivate();
    previewEntity_ = Entity::INVALID;
    previewPublished_ = false;
  }

  Pose AnimationEditorPlugin::pose_at(float time) const
  {
    Pose pose = skeleton_.rest_pose();
    clip_.sample(skeleton_, time, pose);
    return pose;
  }

  void AnimationEditorPlugin::update_preview_pose()
  {
    previewPose_ = pose_at(timeline_.time);

    if (!hasOverride_ || std::fabs(overrideTime_ - timeline_.time) > kTimeEpsilon)
    {
      // The override belongs to one instant on the timeline; scrubbing away
      // from it is the user saying they are done with that pose.
      clear_pose_override();
      return;
    }

    const std::size_t count = std::min(overrideMask_.size(), previewPose_.size());
    for (std::size_t i = 0; i < count; ++i)
    {
      if (!overrideMask_[i])
      {
        continue;
      }
      previewPose_.translations[i] = overridePose_.translations[i];
      previewPose_.rotations[i] = overridePose_.rotations[i];
      previewPose_.scales[i] = overridePose_.scales[i];
    }
  }

  void AnimationEditorPlugin::clear_pose_override()
  {
    hasOverride_ = false;
    overrideMask_.clear();
    overridePose_.clear();
  }

  void AnimationEditorPlugin::publish_preview(const ModelAsset &asset)
  {
    if (skeleton_.empty())
    {
      release_preview();
      return;
    }

    update_preview_pose();
    skeleton_.local_to_global(previewPose_, previewGlobals_);
    Skeleton::globals_to_palette(asset, previewGlobals_, previewPalette_);

    const Entity::EntityId entity =
        targetEntity_.has_value() ? *targetEntity_ : Entity::INVALID;

    if (previewPublished_ && previewEntity_ != entity && previewEntity_ != Entity::INVALID)
    {
      AnimationRuntime::instance().clear_preview(previewEntity_);
    }

    if (entity != Entity::INVALID)
    {
      AnimationRuntime::instance().set_preview_palette(entity, previewPalette_);
    }

    previewEntity_ = entity;
    previewPublished_ = true;

    AnimationEditState &editState = animation_edit_state();
    editState.active = true;
    editState.entity = entity;
    editState.modelPath = targetModelPath_;

    // The mesh is drawn at `globalInverseTransform * nodeGlobal * offset`
    // (ModelAsset::paletteFromNodeGlobals), so raw node globals live in a
    // different space than the vertices they drive — a 90 degree error on
    // every Z-up Collada and any export with a transformed scene root. The
    // overlay gets its own corrected copy; previewGlobals_ stays raw because
    // globals_to_palette above applies globalInverseTransform itself and
    // correcting it in place would apply it twice.
    editState.modelGlobalInverse = asset.globalInverseTransform;
    editState.jointGlobals.resize(previewGlobals_.size());
    for (std::size_t i = 0; i < previewGlobals_.size(); ++i)
    {
      editState.jointGlobals[i] = asset.globalInverseTransform * previewGlobals_[i];
    }

    const std::size_t jointCount = skeleton_.size();
    editState.jointParents.resize(jointCount);
    editState.jointNames.resize(jointCount);
    editState.jointSkinned.resize(jointCount);
    for (std::size_t i = 0; i < jointCount; ++i)
    {
      const SkeletonJoint &joint = skeleton_.joint(i);
      editState.jointParents[i] = joint.parent;
      editState.jointNames[i] = joint.name;
      editState.jointSkinned[i] = joint.skinned;
    }

    editState.selectedJoint = selectedJoint_;
    editState.poseEditing = selectedJoint_ >= 0 && activeTab_ == 0;

    if (selectedJoint_ >= 0 && static_cast<std::size_t>(selectedJoint_) < previewPose_.size())
    {
      const std::size_t index = static_cast<std::size_t>(selectedJoint_);
      editState.selectedLocalTranslation = previewPose_.translations[index];
      editState.selectedLocalRotation = previewPose_.rotations[index];
      editState.selectedLocalScale = previewPose_.scales[index];
    }
  }

  void AnimationEditorPlugin::advance_playback(float deltaTime, const ModelAsset &asset)
  {
    if (!playing_)
    {
      return;
    }

    if (asset.nodes.empty() || clip_.duration <= 0.0f)
    {
      playing_ = false;
      return;
    }

    clear_pose_override();
    timeline_.time += deltaTime * playbackSpeed_;

    if (timeline_.time > clip_.duration)
    {
      if (loopPlayback_)
      {
        timeline_.time = std::fmod(timeline_.time, clip_.duration);
      }
      else
      {
        timeline_.time = clip_.duration;
        playing_ = false;
      }
    }
    else if (timeline_.time < 0.0f)
    {
      // Reachable with a negative speed, which is a legitimate way to review
      // a transition backwards.
      if (loopPlayback_)
      {
        timeline_.time = clip_.duration + std::fmod(timeline_.time, clip_.duration);
      }
      else
      {
        timeline_.time = 0.0f;
        playing_ = false;
      }
    }
  }

  void AnimationEditorPlugin::consume_viewport_input(const ModelAsset &asset)
  {
    AnimationEditState &editState = animation_edit_state();

    if (editState.pickedJoint >= 0)
    {
      if (static_cast<std::size_t>(editState.pickedJoint) < asset.nodes.size() &&
          static_cast<std::size_t>(editState.pickedJoint) < skeleton_.size())
      {
        if (showOnlySelectedTrack_ && selectedJoint_ != editState.pickedJoint)
        {
          timeline_.selection.clear();
        }
        selectedJoint_ = editState.pickedJoint;
      }
      // One-shot: clearing it is what tells the viewport the pick landed.
      editState.pickedJoint = -1;
    }

    if (!editState.jointEdited)
    {
      return;
    }

    const bool finished = editState.jointEditFinished;
    editState.jointEdited = false;
    editState.jointEditFinished = false;

    if (selectedJoint_ < 0 || static_cast<std::size_t>(selectedJoint_) >= skeleton_.size())
    {
      return;
    }

    apply_joint_edit(
        selectedJoint_,
        editState.editedTranslation,
        editState.editedRotation,
        editState.editedScale,
        finished);
  }

  bool AnimationEditorPlugin::joint_is_keyable(std::size_t index) const
  {
    // The same rule AnimationClipAsset::from_rest_pose applies: a clip track
    // is bound by name and AnimationClipAsset::sample resolves it through
    // Skeleton::find, which answers with the FIRST joint carrying that name.
    // Keying any later carrier writes into the first one's track, so the
    // wrong joint animates and the edited one never moves.
    return index < skeleton_.size() &&
           skeleton_.find(skeleton_.joint(index).name) == static_cast<int>(index);
  }

  void AnimationEditorPlugin::apply_joint_edit(
      int joint,
      const math::Vec3 &translation,
      const math::Quat &rotation,
      const math::Vec3 &scale,
      bool finished)
  {
    if (joint < 0 || static_cast<std::size_t>(joint) >= skeleton_.size())
    {
      return;
    }

    const std::size_t index = static_cast<std::size_t>(joint);

    if (!hasOverride_ || overrideMask_.size() != skeleton_.size())
    {
      overridePose_ = previewPose_;
      overridePose_.resize(skeleton_.size());
      overrideMask_.assign(skeleton_.size(), false);
      hasOverride_ = true;
    }
    overrideTime_ = timeline_.time;
    overrideMask_[index] = true;
    overridePose_.translations[index] = translation;
    overridePose_.rotations[index] = rotation;
    overridePose_.scales[index] = scale;

    if (index < previewPose_.size())
    {
      previewPose_.translations[index] = translation;
      previewPose_.rotations[index] = rotation;
      previewPose_.scales[index] = scale;
    }

    // The override above is index-based and stays: the user should still be
    // able to pose-preview a duplicate-named joint. Only the clip write has
    // to be refused, because it would land on the joint that owns the name.
    const bool keyable = joint_is_keyable(index);
    if (autoKey_ && !keyable)
    {
      const std::string &name = skeleton_.joint(index).name;
      statusMessage_ = "Joint '" + name + "' shares its name with joint " +
                       std::to_string(skeleton_.find(name)) + " and cannot be keyed.";
    }

    if (autoKey_ && keyable)
    {
      clip_.set_pose_key(skeleton_.joint(index).name, timeline_.time, translation, rotation, scale);
      mark_dirty();
    }

    // Only an auto-keyed edit changed the clip; a pose held in the override
    // has nothing to undo, and an empty entry would make Ctrl+Z look broken.
    if (finished && autoKey_ && keyable)
    {
      // One entry per drag, not per mouse-move: the baseline still holds the
      // clip from before the drag started.
      push_undo_recorded("Key joint");
    }
  }

  // ---- Animate tab ---------------------------------------------------------

  void AnimationEditorPlugin::draw_animate_tab(EditorPluginContext &context, const ModelAsset &asset)
  {
    if (skeleton_.empty())
    {
      ImGui::TextColored(
          warning_color(), ICON_FA_TRIANGLE_EXCLAMATION "  This model has no skeleton. "
          "Build one in the Rig tab first.");
      return;
    }

    ImGui::BeginChild(
        "##animate-left", ImVec2(300.0f, 0.0f),
        ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    draw_skeleton_tree(asset);
    ImGui::Separator();
    draw_key_inspector();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##animate-right", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);

    const std::string clipTitle = clipName_.empty() ? std::string("<unsaved clip>") : clipName_;
    ImGui::Text(ICON_FA_CLAPPERBOARD "  %s%s", clipTitle.c_str(), clipDirty_ ? " *" : "");
    ImGui::SameLine();
    if (tool_button(ICON_FA_FLOPPY_DISK "  Save", "Write the clip to .hades/animations", clipDirty_ || !clipLoaded_))
    {
      save_clip(context);
    }
    ImGui::SameLine();
    if (tool_button(ICON_FA_ARROW_ROTATE_LEFT, "Undo (Ctrl/Cmd+Z)", !undoStack_.empty()))
    {
      undo();
    }
    ImGui::SameLine();
    if (tool_button(ICON_FA_ARROW_ROTATE_RIGHT, "Redo (Ctrl/Cmd+Shift+Z)", !redoStack_.empty()))
    {
      redo();
    }

    ImGui::Separator();
    draw_transport(asset);
    ImGui::Separator();

    build_timeline_rows(
        clip_, skeleton_, jointFilter_, selectedJoint_, showOnlySelectedTrack_, rows_);

    const float rowsHeight = static_cast<float>(rows_.size() + 2) * timeline_.rowHeight;
    const float timelineHeight = std::clamp(rowsHeight, 140.0f, 340.0f);
    AnimationTimelineResult result =
        draw_animation_timeline("##dope-sheet", clip_, rows_, timeline_, timelineHeight);

    if (timeline_.showCurves)
    {
      ImGui::Separator();
      draw_animation_curves("##curve-editor", clip_, rows_, timeline_, 200.0f, result);
    }

    handle_timeline_result(result, asset);

    if (ImGui::CollapsingHeader(ICON_FA_PEN_TO_SQUARE "  Clip Properties"))
    {
      draw_clip_properties();
    }

    if (ImGui::CollapsingHeader(ICON_FA_KEY "  Events"))
    {
      draw_events_editor();
    }

    if (ImGui::CollapsingHeader(ICON_FA_BONE "  Viewport Overlay"))
    {
      draw_overlay_options();
    }

    draw_status_lines();
    ImGui::EndChild();
  }

  void AnimationEditorPlugin::draw_skeleton_tree(const ModelAsset &asset)
  {
    // Counted from the skeleton rather than asset.bones: a node can own more
    // than one palette entry, so the bone count is not a joint count.
    std::size_t skinnedCount = 0;
    for (const SkeletonJoint &joint : skeleton_.joints())
    {
      skinnedCount += joint.skinned ? 1u : 0u;
    }

    ImGui::TextDisabled(
        "Skeleton — %zu joints, %zu skinned, %zu palette entries", skeleton_.size(), skinnedCount,
        asset.bones.size());

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint(
            "##joint-filter", ICON_FA_MAGNIFYING_GLASS "  Filter joints",
            jointFilterBuffer_.data(), jointFilterBuffer_.size()))
    {
      jointFilter_.assign(jointFilterBuffer_.data());
      // The timeline selection is keyed on row *index*, and filtering
      // renumbers the rows: a key selected before the filter changed would
      // resolve to some other track, and deleting it would take that
      // track's key instead.
      timeline_.selection.clear();
    }

    if (ImGui::Checkbox("Only selected track", &showOnlySelectedTrack_))
    {
      timeline_.selection.clear();
    }

    const float treeHeight = std::max(160.0f, ImGui::GetContentRegionAvail().y * 0.55f);
    ImGui::BeginChild("##joint-tree", ImVec2(0.0f, treeHeight), ImGuiChildFlags_Borders);
    for (const int root : skeleton_.roots())
    {
      draw_skeleton_tree_node(root);
    }
    ImGui::EndChild();
  }

  bool AnimationEditorPlugin::subtree_matches_filter(int joint) const
  {
    if (joint < 0 || static_cast<std::size_t>(joint) >= skeleton_.size())
    {
      return false;
    }

    if (contains_ci(skeleton_.joint(static_cast<std::size_t>(joint)).name, jointFilter_))
    {
      return true;
    }

    for (const int child : skeleton_.children(static_cast<std::size_t>(joint)))
    {
      if (subtree_matches_filter(child))
      {
        return true;
      }
    }
    return false;
  }

  void AnimationEditorPlugin::draw_skeleton_tree_node(int joint)
  {
    if (joint < 0 || static_cast<std::size_t>(joint) >= skeleton_.size())
    {
      return;
    }

    const std::size_t index = static_cast<std::size_t>(joint);
    if (!jointFilter_.empty() && !subtree_matches_filter(joint))
    {
      return;
    }

    const SkeletonJoint &jointData = skeleton_.joint(index);
    const std::vector<int> &children = skeleton_.children(index);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_DefaultOpen;
    if (children.empty())
    {
      flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (joint == selectedJoint_)
    {
      flags |= ImGuiTreeNodeFlags_Selected;
    }

    // Joints that skin nothing still drive their children, so they are shown
    // — just dimmed, so a rigger can tell influence from pure hierarchy.
    const bool dimmed = !jointData.skinned;
    if (dimmed)
    {
      ImGui::PushStyleColor(ImGuiCol_Text, dim_color());
    }

    ImGui::PushID(joint);
    const bool opened = ImGui::TreeNodeEx("##joint", flags, "%s", jointData.name.c_str());
    if (dimmed)
    {
      ImGui::PopStyleColor();
    }

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
      if (showOnlySelectedTrack_ && selectedJoint_ != joint)
      {
        // Same reason as the filter: with only the selected track listed,
        // picking another joint renumbers every row under the selection.
        timeline_.selection.clear();
      }
      selectedJoint_ = joint;
    }

    if (opened && !children.empty())
    {
      for (const int child : children)
      {
        draw_skeleton_tree_node(child);
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }

  void AnimationEditorPlugin::draw_key_inspector()
  {
    if (selectedJoint_ < 0 || static_cast<std::size_t>(selectedJoint_) >= skeleton_.size())
    {
      ImGui::TextDisabled("Select a joint to edit its pose.");
      return;
    }

    const std::size_t index = static_cast<std::size_t>(selectedJoint_);
    const SkeletonJoint &jointData = skeleton_.joint(index);

    ImGui::Text(ICON_FA_BONE "  %s", jointData.name.c_str());
    if (!jointData.skinned)
    {
      ImGui::TextColored(dim_color(), "Skins no vertices.");
    }
    if (!joint_is_keyable(index))
    {
      // Shown here because this panel is open whenever a joint is being
      // posed, so the user learns the edit will not stick before they drag.
      ImGui::TextColored(
          warning_color(),
          ICON_FA_TRIANGLE_EXCLAMATION "  Shares its name with joint %d; clips bind by name, "
          "so this joint cannot be keyed.",
          skeleton_.find(jointData.name));
    }

    if (index >= previewPose_.size())
    {
      ImGui::TextDisabled("Pose not sampled yet.");
      return;
    }

    math::Vec3 translation = previewPose_.translations[index];
    math::Vec3 euler = previewPose_.rotations[index].toEulerDegrees();
    math::Vec3 scale = previewPose_.scales[index];

    bool changed = false;
    bool finished = false;

    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::DragFloat3("##joint-translation", &translation.x, 0.01f);
    finished |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::TextDisabled("Translation");

    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::DragFloat3("##joint-rotation", &euler.x, 0.5f);
    finished |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::TextDisabled("Rotation (degrees)");

    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::DragFloat3("##joint-scale", &scale.x, 0.01f);
    finished |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::TextDisabled("Scale");

    if (changed || finished)
    {
      apply_joint_edit(
          selectedJoint_, translation, math::Quat::fromEulerDegrees(euler), scale, finished);
    }

    if (ImGui::Button("Reset to rest"))
    {
      apply_joint_edit(
          selectedJoint_, jointData.restTranslation, jointData.restRotation, jointData.restScale,
          true);
    }
  }

  int AnimationEditorPlugin::frame_count() const
  {
    const float rate = clip_.frameRate > 0.0f ? clip_.frameRate : 30.0f;
    return std::max(1, static_cast<int>(std::lround(clip_.duration * rate)));
  }

  int AnimationEditorPlugin::current_frame() const
  {
    const float rate = clip_.frameRate > 0.0f ? clip_.frameRate : 30.0f;
    return std::max(0, static_cast<int>(std::lround(timeline_.time * rate)));
  }

  void AnimationEditorPlugin::set_frame(int frame)
  {
    const float rate = clip_.frameRate > 0.0f ? clip_.frameRate : 30.0f;
    const int clamped = std::clamp(frame, 0, frame_count());
    timeline_.time = std::clamp(static_cast<float>(clamped) / rate, 0.0f, clip_.duration);
    clear_pose_override();
  }

  void AnimationEditorPlugin::draw_transport(const ModelAsset &asset)
  {
    const bool enabled = !asset.nodes.empty() && !skeleton_.empty();

    if (tool_button(ICON_FA_BACKWARD_FAST, "First frame", enabled))
    {
      playing_ = false;
      set_frame(0);
    }
    ImGui::SameLine();
    if (tool_button(ICON_FA_BACKWARD_STEP, "Previous frame", enabled))
    {
      playing_ = false;
      set_frame(current_frame() - 1);
    }
    ImGui::SameLine();
    if (tool_button(playing_ ? ICON_FA_PAUSE : ICON_FA_PLAY, "Play / pause", enabled))
    {
      playing_ = !playing_;
      if (playing_)
      {
        clear_pose_override();
      }
    }
    ImGui::SameLine();
    if (tool_button(ICON_FA_FORWARD_STEP, "Next frame", enabled))
    {
      playing_ = false;
      set_frame(current_frame() + 1);
    }
    ImGui::SameLine();
    if (tool_button(ICON_FA_FORWARD_FAST, "Last frame", enabled))
    {
      playing_ = false;
      set_frame(frame_count());
    }

    ImGui::SameLine();
    ImGui::Checkbox("Loop", &loopPlayback_);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::DragFloat("Speed", &playbackSpeed_, 0.01f, -4.0f, 4.0f, "%.2fx");

    ImGui::SameLine();
    ImGui::Checkbox("Auto-key", &autoKey_);
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip("Write a key at the play head whenever a joint is posed.");
    }

    ImGui::SameLine();
    ImGui::TextColored(
        accent_color(), "%d / %d @ %g fps", current_frame(), frame_count(),
        static_cast<double>(clip_.frameRate > 0.0f ? clip_.frameRate : 30.0f));

    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::SliderFloat("Time", &timeline_.time, 0.0f, std::max(clip_.duration, 0.001f), "%.3f s"))
    {
      playing_ = false;
      clear_pose_override();
    }

    ImGui::SameLine();
    if (tool_button(
            ICON_FA_KEY "  Key selected", "Key the selected joint at the play head",
            enabled && selectedJoint_ >= 0))
    {
      key_selected_joint(timeline_.time);
    }
    ImGui::SameLine();
    if (tool_button(ICON_FA_KEY "  Key whole pose", "Key every joint at the play head", enabled))
    {
      key_whole_pose(timeline_.time);
    }

    ImGui::SameLine();
    ImGui::Checkbox("Snap", &timeline_.snapToFrames);
    ImGui::SameLine();
    ImGui::Checkbox("Curves", &timeline_.showCurves);
  }

  void AnimationEditorPlugin::handle_timeline_result(
      const AnimationTimelineResult &result, const ModelAsset &asset)
  {
    if (asset.nodes.empty())
    {
      return;
    }

    if (result.timeChanged)
    {
      playing_ = false;
      clear_pose_override();
      update_preview_pose();
    }

    if (result.clipChanged)
    {
      // The widget already mutated the clip, so the snapshot to keep is the
      // baseline captured before it started.
      push_undo_recorded(result.changeLabel.empty() ? std::string("Timeline edit") : result.changeLabel);
      mark_dirty();
    }

    if (result.requestInsertKey)
    {
      insert_key_from_row(result.contextRow, result.contextTime);
    }

    if (result.requestDeleteSelection)
    {
      delete_selected_keys();
    }
  }

  void AnimationEditorPlugin::insert_key_from_row(int row, float time)
  {
    if (skeleton_.empty())
    {
      return;
    }

    const float keyTime = std::clamp(time, 0.0f, std::max(clip_.duration, 0.0f));
    const Pose pose = pose_at(keyTime);

    const bool wholePose =
        row < 0 || static_cast<std::size_t>(row) >= rows_.size() ||
        rows_[static_cast<std::size_t>(row)].kind == TimelineRow::Kind::Summary;
    if (wholePose)
    {
      push_undo("Insert keys");
      for (std::size_t i = 0; i < skeleton_.size() && i < pose.size(); ++i)
      {
        // See key_whole_pose(): a duplicate-named joint would key over the
        // track the first carrier of that name owns.
        if (!joint_is_keyable(i))
        {
          continue;
        }
        clip_.set_pose_key(
            skeleton_.joint(i).name, keyTime, pose.translations[i], pose.rotations[i],
            pose.scales[i]);
      }
      mark_dirty();
      return;
    }

    const TimelineRow &timelineRow = rows_[static_cast<std::size_t>(row)];
    if (timelineRow.kind == TimelineRow::Kind::Events)
    {
      push_undo("Add event");
      AnimationEventKey event;
      event.time = keyTime;
      event.name = "event";
      clip_.events.push_back(event);
      mark_dirty();
      return;
    }

    const int joint = timelineRow.joint;
    if (joint < 0 || static_cast<std::size_t>(joint) >= pose.size())
    {
      return;
    }
    const std::size_t index = static_cast<std::size_t>(joint);

    push_undo("Insert key");
    if (timelineRow.kind == TimelineRow::Kind::Channel)
    {
      switch (timelineRow.channel)
      {
      case TrackChannel::Translation:
        clip_.set_translation_key(timelineRow.bone, keyTime, pose.translations[index]);
        break;
      case TrackChannel::Rotation:
        clip_.set_rotation_key(timelineRow.bone, keyTime, pose.rotations[index]);
        break;
      case TrackChannel::Scale:
        clip_.set_scale_key(timelineRow.bone, keyTime, pose.scales[index]);
        break;
      }
    }
    else
    {
      clip_.set_pose_key(
          timelineRow.bone, keyTime, pose.translations[index], pose.rotations[index],
          pose.scales[index]);
    }
    mark_dirty();
  }

  void AnimationEditorPlugin::draw_clip_properties()
  {
    std::array<char, 128> nameBuffer{};
    set_buffer_text(nameBuffer, clip_.name);
    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size()))
    {
      clip_.name.assign(nameBuffer.data());
      mark_dirty();
    }
    // Every property widget closes its own undo entry on release: leaving
    // the baseline open here would fold these edits into whatever key the
    // user happens to set next.
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
      push_undo_recorded("Rename clip");
    }

    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::DragFloat("Duration", &clip_.duration, 0.01f, 0.01f, 600.0f, "%.3f s"))
    {
      clip_.duration = std::max(0.01f, clip_.duration);
      timeline_.time = std::clamp(timeline_.time, 0.0f, clip_.duration);
      mark_dirty();
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
      push_undo_recorded("Set duration");
    }
    ImGui::SameLine();
    if (ImGui::Button("Fit to keys"))
    {
      push_undo("Fit duration");
      clip_.recompute_duration();
      mark_dirty();
    }

    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::DragFloat("Frame rate", &clip_.frameRate, 1.0f, 1.0f, 240.0f, "%.0f fps"))
    {
      clip_.frameRate = std::clamp(clip_.frameRate, 1.0f, 240.0f);
      mark_dirty();
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
      push_undo_recorded("Set frame rate");
    }

    // Checkbox() has already flipped the flag by the time it returns true,
    // so the baseline (pre-toggle) is the snapshot worth keeping.
    if (ImGui::Checkbox("Looping", &clip_.looping))
    {
      mark_dirty();
      push_undo_recorded("Toggle looping");
    }

    if (ImGui::Checkbox("Additive", &clip_.additive))
    {
      mark_dirty();
      push_undo_recorded("Toggle additive");
    }
    if (clip_.additive)
    {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(140.0f);
      if (ImGui::DragFloat(
              "Reference time", &clip_.additiveReferenceTime, 0.01f, 0.0f,
              std::max(clip_.duration, 0.01f), "%.3f s"))
      {
        clip_.additiveReferenceTime =
            std::clamp(clip_.additiveReferenceTime, 0.0f, std::max(clip_.duration, 0.0f));
        mark_dirty();
      }
      if (ImGui::IsItemDeactivatedAfterEdit())
      {
        push_undo_recorded("Set additive reference");
      }
      ImGui::TextColored(
          dim_color(), "Applied as a delta against the pose at the reference time.");
    }

    ImGui::TextDisabled(
        "Source model: %s", clip_.sourceModel.empty() ? "<none>" : clip_.sourceModel.c_str());
    ImGui::TextDisabled(
        "%zu tracks, %zu keys", clip_.tracks.size(), clip_.total_key_count());
  }

  void AnimationEditorPlugin::draw_events_editor()
  {
    if (ImGui::Button(ICON_FA_PLUS "  Add event"))
    {
      push_undo("Add event");
      AnimationEventKey event;
      event.time = timeline_.time;
      event.name = "event";
      clip_.events.push_back(event);
      mark_dirty();
    }

    if (clip_.events.empty())
    {
      ImGui::TextDisabled("No events on this clip.");
      return;
    }

    int removeIndex = -1;
    for (std::size_t i = 0; i < clip_.events.size(); ++i)
    {
      AnimationEventKey &event = clip_.events[i];
      ImGui::PushID(static_cast<int>(i));

      ImGui::SetNextItemWidth(90.0f);
      if (ImGui::DragFloat("##event-time", &event.time, 0.01f, 0.0f, std::max(clip_.duration, 0.0f), "%.3f s"))
      {
        event.time = std::clamp(event.time, 0.0f, std::max(clip_.duration, 0.0f));
        mark_dirty();
      }
      if (ImGui::IsItemDeactivatedAfterEdit())
      {
        push_undo_recorded("Move event");
      }

      ImGui::SameLine();
      std::array<char, 96> nameBuffer{};
      set_buffer_text(nameBuffer, event.name);
      ImGui::SetNextItemWidth(140.0f);
      if (ImGui::InputText("##event-name", nameBuffer.data(), nameBuffer.size()))
      {
        event.name.assign(nameBuffer.data());
        mark_dirty();
      }
      if (ImGui::IsItemDeactivatedAfterEdit())
      {
        push_undo_recorded("Rename event");
      }

      ImGui::SameLine();
      std::array<char, 160> stringBuffer{};
      set_buffer_text(stringBuffer, event.stringValue);
      ImGui::SetNextItemWidth(160.0f);
      if (ImGui::InputText("##event-string", stringBuffer.data(), stringBuffer.size()))
      {
        event.stringValue.assign(stringBuffer.data());
        mark_dirty();
      }
      if (ImGui::IsItemDeactivatedAfterEdit())
      {
        push_undo_recorded("Edit event payload");
      }

      ImGui::SameLine();
      ImGui::SetNextItemWidth(90.0f);
      if (ImGui::DragFloat("##event-float", &event.floatValue, 0.01f))
      {
        mark_dirty();
      }
      if (ImGui::IsItemDeactivatedAfterEdit())
      {
        push_undo_recorded("Edit event payload");
      }

      ImGui::SameLine();
      if (ImGui::Button(ICON_FA_TRASH))
      {
        removeIndex = static_cast<int>(i);
      }

      ImGui::PopID();
    }

    if (removeIndex >= 0)
    {
      push_undo("Remove event");
      clip_.events.erase(clip_.events.begin() + removeIndex);
      mark_dirty();
    }
  }

  void AnimationEditorPlugin::draw_overlay_options()
  {
    AnimationEditState &editState = animation_edit_state();
    ImGui::Checkbox("Draw skeleton", &editState.showSkeleton);
    ImGui::SameLine();
    ImGui::Checkbox("Joint names", &editState.showJointNames);
    ImGui::SameLine();
    ImGui::Checkbox("Unskinned joints", &editState.showUnskinnedJoints);
    if (editState.hoveredJoint >= 0 &&
        static_cast<std::size_t>(editState.hoveredJoint) < skeleton_.size())
    {
      ImGui::TextColored(
          dim_color(), "Hovered: %s",
          skeleton_.joint(static_cast<std::size_t>(editState.hoveredJoint)).name.c_str());
    }
  }

  // ---- Clip editing --------------------------------------------------------

  void AnimationEditorPlugin::key_selected_joint(float time)
  {
    if (selectedJoint_ < 0 || static_cast<std::size_t>(selectedJoint_) >= skeleton_.size())
    {
      return;
    }

    const std::size_t index = static_cast<std::size_t>(selectedJoint_);
    const Pose &pose =
        std::fabs(time - timeline_.time) <= kTimeEpsilon ? previewPose_ : pose_at(time);
    if (index >= pose.size())
    {
      return;
    }

    if (!joint_is_keyable(index))
    {
      const std::string &name = skeleton_.joint(index).name;
      statusMessage_ = "Joint '" + name + "' shares its name with joint " +
                       std::to_string(skeleton_.find(name)) + " and cannot be keyed.";
      // Returned before push_undo so Ctrl+Z does not offer an entry that
      // changed nothing.
      return;
    }

    push_undo("Key joint");
    clip_.set_pose_key(
        skeleton_.joint(index).name, time, pose.translations[index], pose.rotations[index],
        pose.scales[index]);
    mark_dirty();
    statusMessage_ = "Keyed " + skeleton_.joint(index).name;
  }

  void AnimationEditorPlugin::key_whole_pose(float time)
  {
    if (skeleton_.empty())
    {
      return;
    }

    // Bind the sampled pose to a local so the temporary from pose_at()
    // outlives the loop below.
    const Pose sampled = pose_at(time);
    const bool useLive = std::fabs(time - timeline_.time) <= kTimeEpsilon;
    const Pose &pose = useLive ? previewPose_ : sampled;

    push_undo("Key pose");
    std::size_t skipped = 0;
    for (std::size_t i = 0; i < skeleton_.size() && i < pose.size(); ++i)
    {
      // Silently, exactly like AnimationClipAsset::from_rest_pose: this is a
      // bulk operation, and without the skip the second carrier of a name
      // would overwrite the first one's track — corrupting the clip with no
      // editing at all, straight off the rest pose.
      if (!joint_is_keyable(i))
      {
        ++skipped;
        continue;
      }
      clip_.set_pose_key(
          skeleton_.joint(i).name, time, pose.translations[i], pose.rotations[i], pose.scales[i]);
    }
    mark_dirty();
    statusMessage_ = "Keyed the whole pose";
    if (skipped > 0)
    {
      statusMessage_ += " (" + std::to_string(skipped) + " joints skipped: duplicate names)";
    }
  }

  void AnimationEditorPlugin::delete_selected_keys()
  {
    if (timeline_.selection.empty())
    {
      return;
    }

    push_undo("Delete keys");

    for (const TimelineKeyRef &key : timeline_.selection)
    {
      if (key.row < 0 || static_cast<std::size_t>(key.row) >= rows_.size())
      {
        continue;
      }

      const TimelineRow &row = rows_[static_cast<std::size_t>(key.row)];
      switch (row.kind)
      {
      case TimelineRow::Kind::Channel:
        clip_.remove_key(row.bone, row.channel, key.time);
        break;
      case TimelineRow::Kind::BoneSummary:
        clip_.remove_key(row.bone, TrackChannel::Translation, key.time);
        clip_.remove_key(row.bone, TrackChannel::Rotation, key.time);
        clip_.remove_key(row.bone, TrackChannel::Scale, key.time);
        break;
      case TimelineRow::Kind::Summary:
      {
        // Names are copied first: remove_key looks tracks up by name, and
        // iterating the vector it searches is a trap waiting to be sprung.
        std::vector<std::string> bones;
        bones.reserve(clip_.tracks.size());
        for (const AnimationBoneTrack &track : clip_.tracks)
        {
          bones.push_back(track.bone);
        }
        for (const std::string &bone : bones)
        {
          clip_.remove_key(bone, TrackChannel::Translation, key.time);
          clip_.remove_key(bone, TrackChannel::Rotation, key.time);
          clip_.remove_key(bone, TrackChannel::Scale, key.time);
        }
        break;
      }
      case TimelineRow::Kind::Events:
        clip_.events.erase(
            std::remove_if(
                clip_.events.begin(),
                clip_.events.end(),
                [&key](const AnimationEventKey &event)
                {
                  return std::fabs(event.time - key.time) <= AnimationClipAsset::kKeyEpsilon;
                }),
            clip_.events.end());
        break;
      }
    }

    timeline_.selection.clear();
    mark_dirty();
  }

  void AnimationEditorPlugin::new_clip(EditorPluginContext &context, const ModelAsset &asset)
  {
    (void)asset;

    std::string name = sanitize_asset_name(std::string(newClipNameBuffer_.data()));
    refresh_clip_list(context);
    name = unique_name(name, clipList_);

    clip_ = AnimationClipAsset::from_rest_pose(skeleton_, name);
    clip_.sourceModel = targetModelPath_;
    clipName_ = name;
    clipLoaded_ = true;
    clipDirty_ = true;
    timeline_.time = 0.0f;
    timeline_.viewStart = 0.0f;
    timeline_.viewEnd = clip_.duration;
    timeline_.selection.clear();
    clear_pose_override();
    reset_undo();

    // Written straight away so the new clip shows up in the browser and in
    // any animator graph the user opens next.
    save_clip_as(context, name);
  }

  void AnimationEditorPlugin::load_clip(EditorPluginContext &context, const std::string &name)
  {
    const AnimationClipAsset *loaded = AnimationClipCache::instance().clip(name);
    if (loaded == nullptr)
    {
      const std::string error = AnimationClipCache::instance().errorFor(name);
      errorMessage_ = error.empty() ? ("could not load clip '" + name + "'") : error;
      return;
    }

    clip_ = *loaded;
    clipName_ = name;
    clipLoaded_ = true;
    clipDirty_ = false;
    errorMessage_.clear();
    statusMessage_ = "Loaded clip '" + name + "'";
    timeline_.time = 0.0f;
    timeline_.viewStart = 0.0f;
    timeline_.viewEnd = std::max(clip_.duration, 0.01f);
    timeline_.selection.clear();
    clear_pose_override();
    reset_undo();
    refresh_clip_list(context);
  }

  void AnimationEditorPlugin::save_clip(EditorPluginContext &context)
  {
    if (clipName_.empty())
    {
      // Ask for a name and then save THIS clip under it. Handing the dialog
      // to new_clip() would replace the working clip with a fresh rest-pose
      // one, so hitting Save on an unnamed clip would throw away everything
      // the user just authored.
      refresh_clip_list(context);
      set_buffer_text(newClipNameBuffer_, clip_.name.empty() ? std::string("clip") : clip_.name);
      saveWorkingClipOnCreate_ = true;
      openNewClipPopup_ = true;
      return;
    }

    save_clip_as(context, clipName_);
  }

  void AnimationEditorPlugin::save_clip_as(EditorPluginContext &context, const std::string &name)
  {
    if (name.empty())
    {
      return;
    }

    if (clip_.name.empty())
    {
      clip_.name = name;
    }
    if (clip_.sourceModel.empty())
    {
      clip_.sourceModel = targetModelPath_;
    }
    clip_.sort_keys();

    std::string error;
    if (!AnimationClipCache::instance().saveClip(name, clip_, &error))
    {
      errorMessage_ = error;
      return;
    }

    clipName_ = name;
    clipLoaded_ = true;
    clipDirty_ = false;
    errorMessage_.clear();
    statusMessage_ = "Saved clip '" + name + "'";
    clipListLoaded_ = false;
    refresh_clip_list(context);
  }

  void AnimationEditorPlugin::bake_imported_clip(
      EditorPluginContext &context, const ModelAsset &asset, int clipIndex)
  {
    if (clipIndex < 0 || static_cast<std::size_t>(clipIndex) >= asset.clips.size())
    {
      return;
    }

    AnimationClipAsset baked;
    if (!AnimationClipAsset::bake_from_model(asset, clipIndex, baked))
    {
      errorMessage_ = "could not bake imported clip";
      return;
    }

    refresh_clip_list(context);
    const std::string base = sanitize_asset_name(asset.clips[static_cast<std::size_t>(clipIndex)].name);
    const std::string name = unique_name(base, clipList_);

    baked.name = name;
    baked.sourceModel = targetModelPath_;

    std::string error;
    if (!AnimationClipCache::instance().saveClip(name, baked, &error))
    {
      errorMessage_ = error;
      return;
    }

    clipListLoaded_ = false;
    refresh_clip_list(context);
    load_clip(context, name);
    statusMessage_ = "Baked '" + asset.clips[static_cast<std::size_t>(clipIndex)].name +
                     "' into clip '" + name + "'";
  }

  void AnimationEditorPlugin::refresh_clip_list(EditorPluginContext &context)
  {
    (void)context;
    if (clipListLoaded_)
    {
      return;
    }

    clipList_ = AnimationClipCache::instance().listClips();
    clipListLoaded_ = true;
  }

  // ---- Undo ----------------------------------------------------------------

  void AnimationEditorPlugin::push_undo(const std::string &label)
  {
    UndoEntry entry;
    entry.label = label;
    entry.clip = clip_.to_json();

    undoStack_.push_back(std::move(entry));
    while (undoStack_.size() > kMaxUndoEntries)
    {
      undoStack_.pop_front();
    }
    redoStack_.clear();
    // The mutation this snapshot guards has not happened yet, so the clip
    // cannot serve as the next baseline here: refresh_undo_baseline() takes
    // it at the end of the frame, once the edit has landed. Refreshing it
    // now would leave the baseline one edit behind and make the next
    // recorded entry roll back two user actions at once.
    undoBaselineStale_ = true;
  }

  void AnimationEditorPlugin::push_undo_recorded(const std::string &label)
  {
    if (undoBaselineStale_ && !undoStack_.empty())
    {
      // An entry for this gesture is already open. The curve editor reports
      // a change on every frame of a drag, and one entry per mouse-move
      // would flush the whole history in about a second; the open entry
      // already holds the clip from before the drag started, so only the
      // label still needs saying.
      undoStack_.back().label = label;
      return;
    }

    UndoEntry entry;
    entry.label = label;
    entry.clip = undoBaseline_;

    undoStack_.push_back(std::move(entry));
    while (undoStack_.size() > kMaxUndoEntries)
    {
      undoStack_.pop_front();
    }
    redoStack_.clear();
    undoBaselineStale_ = true;
  }

  void AnimationEditorPlugin::refresh_undo_baseline()
  {
    if (!undoBaselineStale_)
    {
      return;
    }

    // A drag is still running: the entry it opened has to keep pointing at
    // the clip from before it started, so the baseline stays frozen until
    // the gesture ends.
    if (ImGui::IsAnyItemActive() || timeline_.draggingKeys)
    {
      return;
    }

    undoBaseline_ = clip_.to_json();
    undoBaselineStale_ = false;
  }

  void AnimationEditorPlugin::undo()
  {
    if (undoStack_.empty())
    {
      return;
    }

    UndoEntry entry = std::move(undoStack_.back());
    undoStack_.pop_back();

    UndoEntry current;
    current.label = entry.label;
    current.clip = clip_.to_json();
    redoStack_.push_back(std::move(current));
    while (redoStack_.size() > kMaxUndoEntries)
    {
      redoStack_.pop_front();
    }

    AnimationClipAsset restored;
    std::string error;
    if (!AnimationClipAsset::from_json(entry.clip, restored, &error))
    {
      errorMessage_ = error;
      return;
    }

    clip_ = std::move(restored);
    undoBaseline_ = clip_.to_json();
    undoBaselineStale_ = false;
    timeline_.selection.clear();
    timeline_.time = std::clamp(timeline_.time, 0.0f, std::max(clip_.duration, 0.0f));
    clear_pose_override();
    clipDirty_ = true;
    statusMessage_ = "Undo: " + entry.label;
  }

  void AnimationEditorPlugin::redo()
  {
    if (redoStack_.empty())
    {
      return;
    }

    UndoEntry entry = std::move(redoStack_.back());
    redoStack_.pop_back();

    UndoEntry current;
    current.label = entry.label;
    current.clip = clip_.to_json();
    undoStack_.push_back(std::move(current));
    while (undoStack_.size() > kMaxUndoEntries)
    {
      undoStack_.pop_front();
    }

    AnimationClipAsset restored;
    std::string error;
    if (!AnimationClipAsset::from_json(entry.clip, restored, &error))
    {
      errorMessage_ = error;
      return;
    }

    clip_ = std::move(restored);
    undoBaseline_ = clip_.to_json();
    undoBaselineStale_ = false;
    timeline_.selection.clear();
    timeline_.time = std::clamp(timeline_.time, 0.0f, std::max(clip_.duration, 0.0f));
    clear_pose_override();
    clipDirty_ = true;
    statusMessage_ = "Redo: " + entry.label;
  }

  void AnimationEditorPlugin::mark_dirty()
  {
    clipDirty_ = true;
  }

  void AnimationEditorPlugin::reset_undo()
  {
    undoStack_.clear();
    redoStack_.clear();
    undoBaseline_ = clip_.to_json();
    undoBaselineStale_ = false;
  }

  // ---- Rig tab -------------------------------------------------------------

  void AnimationEditorPlugin::load_or_seed_rig(EditorPluginContext &context, const ModelAsset &asset)
  {
    rig_ = RigAsset{};
    rigError_.clear();

    const std::filesystem::path path =
        rig_path_for_model(context.workspacePath, targetModelPath_);

    std::error_code errorCode;
    if (std::filesystem::exists(path, errorCode))
    {
      std::string error;
      if (!load_rig_asset(path, rig_, &error))
      {
        rigError_ = error;
        // Fall through to the seeded rig: an unreadable overlay should not
        // block rigging, and saving will overwrite it anyway.
        rig_ = rig_from_model(asset, targetModelPath_);
      }
    }
    else
    {
      rig_ = rig_from_model(asset, targetModelPath_);
    }

    if (rig_.sourceModel.empty())
    {
      rig_.sourceModel = targetModelPath_;
    }

    rigLoaded_ = true;
    rigDirty_ = false;
    selectedRigJoint_ = rig_.joints.empty() ? -1 : 0;
  }

  void AnimationEditorPlugin::save_rig(EditorPluginContext &context)
  {
    if (targetModelPath_.empty())
    {
      return;
    }

    rig_.sourceModel = targetModelPath_;
    if (!rig_.topological_sort())
    {
      rigError_ = "the rig hierarchy contains a cycle";
      return;
    }

    const std::filesystem::path path =
        rig_path_for_model(context.workspacePath, targetModelPath_);

    std::string error;
    if (!save_rig_asset(path, rig_, &error))
    {
      rigError_ = error;
      return;
    }

    rigDirty_ = false;

    // The cache applies the rig overlay at import time, so the model has to
    // be re-imported before the viewport shows the new skeleton.
    ModelAssetCache::instance().invalidate(targetModelPath_);
    // The asset skeleton_ was built from has just been destroyed, so the
    // sync guard must not be able to match it against the replacement the
    // next get() allocates — possibly at the very same address.
    skeletonSource_ = nullptr;
    skeletonNodeCount_ = 0;

    // Force the re-import here rather than letting draw_panel do it a few
    // lines later, so the panel can report what apply_rig actually made of
    // the rig it just wrote. Reporting "Saved rig to ..." unconditionally is
    // how a refusal (bone budget, unknown parent, bad mesh index) ends up
    // visible only as a log line, with an unchanged viewport and no remedy.
    ModelAssetCache::instance().get(targetModelPath_);
    rigError_ = ModelAssetCache::instance().rigErrorFor(targetModelPath_);
    statusMessage_ = rigError_.empty()
                         ? "Saved rig to " + path.string()
                         : "Saved rig to " + path.string() + ", but it could not be applied.";
  }

  void AnimationEditorPlugin::add_joint(const ModelAsset &asset)
  {
    RigJoint joint;

    std::string base(newJointNameBuffer_.data());
    if (base.empty())
    {
      base = "bone";
    }
    base = sanitize_asset_name(base);

    // Unique within the rig *and* against the imported nodes: apply_rig folds
    // both into one node list, where a duplicate name would shadow a joint.
    std::string name = rig_.unique_joint_name(base);
    const auto clashesWithNode = [&asset](const std::string &candidate)
    {
      return std::any_of(
          asset.nodes.begin(),
          asset.nodes.end(),
          [&candidate](const ModelNode &node) { return node.name == candidate; });
    };
    for (int attempt = 1; clashesWithNode(name) && attempt < 1000; ++attempt)
    {
      name = rig_.unique_joint_name(base + "_" + std::to_string(attempt));
    }

    joint.name = name;
    if (selectedRigJoint_ >= 0 && static_cast<std::size_t>(selectedRigJoint_) < rig_.joints.size())
    {
      joint.parent = rig_.joints[static_cast<std::size_t>(selectedRigJoint_)].name;
    }
    else if (selectedJoint_ >= 0 && static_cast<std::size_t>(selectedJoint_) < skeleton_.size())
    {
      joint.parent = skeleton_.joint(static_cast<std::size_t>(selectedJoint_)).name;
    }

    rig_.joints.push_back(joint);
    selectedRigJoint_ = static_cast<int>(rig_.joints.size()) - 1;
    rigDirty_ = true;
  }

  void AnimationEditorPlugin::delete_joint(int rigJoint)
  {
    if (rigJoint < 0 || static_cast<std::size_t>(rigJoint) >= rig_.joints.size())
    {
      return;
    }

    const std::size_t index = static_cast<std::size_t>(rigJoint);
    const std::string removed = rig_.joints[index].name;
    const std::string newParent = rig_.joints[index].parent;

    for (RigJoint &joint : rig_.joints)
    {
      if (joint.name == removed)
      {
        continue;
      }
      if (joint.parent == removed)
      {
        // Children are adopted by the grandparent rather than orphaned, so
        // deleting a mid-chain joint does not tear the hierarchy apart.
        joint.parent = newParent;
      }
    }

    rig_.joints.erase(rig_.joints.begin() + static_cast<std::ptrdiff_t>(index));

    // Skin weights index into `joints`, so every binding above the removed
    // joint shifts down and every reference to it becomes unused.
    const std::int32_t removedIndex = static_cast<std::int32_t>(index);
    for (RigMeshBinding &binding : rig_.meshes)
    {
      for (std::int32_t &jointIndex : binding.jointIndices)
      {
        if (jointIndex == removedIndex)
        {
          jointIndex = -1;
        }
        else if (jointIndex > removedIndex)
        {
          --jointIndex;
        }
      }
    }

    if (rig_.joints.empty())
    {
      selectedRigJoint_ = -1;
    }
    else
    {
      selectedRigJoint_ = std::min(rigJoint, static_cast<int>(rig_.joints.size()) - 1);
    }
    rigDirty_ = true;
  }

  bool AnimationEditorPlugin::rig_joint_is_descendant(int candidate, int ancestor) const
  {
    if (candidate < 0 || ancestor < 0)
    {
      return false;
    }
    if (candidate == ancestor)
    {
      return true;
    }

    // Bounded by the joint count so a hierarchy that is already cyclic (a
    // hand-edited file) cannot spin here forever.
    int current = candidate;
    for (std::size_t step = 0; step <= rig_.joints.size(); ++step)
    {
      if (current < 0 || static_cast<std::size_t>(current) >= rig_.joints.size())
      {
        return false;
      }
      const int parent = rig_.find_joint(rig_.joints[static_cast<std::size_t>(current)].parent);
      if (parent < 0)
      {
        return false;
      }
      if (parent == ancestor)
      {
        return true;
      }
      current = parent;
    }
    return false;
  }

  void AnimationEditorPlugin::reparent_joint(int rigJoint, const std::string &newParent)
  {
    if (rigJoint < 0 || static_cast<std::size_t>(rigJoint) >= rig_.joints.size())
    {
      return;
    }

    RigJoint &joint = rig_.joints[static_cast<std::size_t>(rigJoint)];
    if (newParent == joint.name)
    {
      rigError_ = "a joint cannot parent itself";
      return;
    }

    const int parentIndex = rig_.find_joint(newParent);
    if (parentIndex >= 0 && rig_joint_is_descendant(parentIndex, rigJoint))
    {
      rigError_ = "'" + newParent + "' is below '" + joint.name + "' — that would make a cycle";
      return;
    }

    joint.parent = newParent;
    rigError_.clear();
    rigDirty_ = true;
  }

  void AnimationEditorPlugin::rename_rig_joint(int rigJoint, const std::string &newName)
  {
    if (rigJoint < 0 || static_cast<std::size_t>(rigJoint) >= rig_.joints.size())
    {
      return;
    }

    const std::string sanitized = sanitize_asset_name(newName);
    if (sanitized.empty())
    {
      return;
    }

    const std::size_t index = static_cast<std::size_t>(rigJoint);
    const std::string previous = rig_.joints[index].name;
    if (previous == sanitized)
    {
      return;
    }

    if (rig_.find_joint(sanitized) >= 0)
    {
      rigError_ = "a joint named '" + sanitized + "' already exists";
      return;
    }

    rig_.joints[index].name = sanitized;
    // Parents are stored by name, so children have to follow the rename.
    for (RigJoint &joint : rig_.joints)
    {
      if (joint.parent == previous)
      {
        joint.parent = sanitized;
      }
    }

    rigError_.clear();
    rigDirty_ = true;
  }

  void AnimationEditorPlugin::auto_weight(EditorPluginContext &context, const ModelAsset &asset)
  {
    (void)context;

    if (rig_.joints.empty())
    {
      rigError_ = "the rig has no joints to bind to";
      return;
    }

    compute_auto_weights(asset, rig_, weightMode_, weightFalloff_, weightInfluences_);
    rigDirty_ = true;
    rigError_.clear();
    statusMessage_ = "Bound " + std::to_string(rig_.meshes.size()) + " mesh(es)";
  }

  std::size_t AnimationEditorPlugin::rig_required_bones(const ModelAsset &asset) const
  {
    // Mirrors apply_rig's own predicate (src/engine/animation/rig_asset.cpp,
    // `requiredBones = retainedBones + appendedBones`). Counting joints
    // instead is what made this banner red out on the ordinary "seed a rig
    // from a 64-bone character and bind it" flow, which applies fine.
    const std::size_t importedBones =
        ModelAssetCache::instance().importedBoneCount(targetModelPath_);

    // apply_rig returns early on an empty rig and leaves the import alone.
    if (rig_.joints.empty())
    {
      return importedBones;
    }

    // Replace mode clears asset.bones and gives every joint a slot, so the
    // joint count IS the palette size there.
    if (rig_.replaceImportedSkeleton)
    {
      return rig_.joints.size();
    }

    // Append mode with nothing bound: no vertex points at a rig joint, so
    // apply_rig adds no palette entry at all and keeps the import's.
    if (rig_.meshes.empty())
    {
      return importedBones;
    }

    // A joint whose name is an imported node re-points the entry that node
    // already owns. `asset` is the overlaid asset, but apply_rig only ever
    // appends to the palette, so its first `importedBones` entries are still
    // the imported ones — which is what makes this readable without the raw
    // import in hand. (A model with two same-named nodes where only the later
    // one is skinned resolves to the earlier one in apply_rig and is counted
    // as re-pointing here; that costs at most one entry per such pair.)
    const std::size_t importedEnd = std::min(importedBones, asset.bones.size());
    std::vector<std::string_view> boneNodeNames;
    std::vector<int> boneNodeIndices;
    boneNodeNames.reserve(importedEnd);
    boneNodeIndices.reserve(importedEnd);
    for (std::size_t bone = 0; bone < importedEnd; ++bone)
    {
      const int node = asset.bones[bone].nodeIndex;
      if (node >= 0 && static_cast<std::size_t>(node) < asset.nodes.size())
      {
        boneNodeNames.emplace_back(asset.nodes[static_cast<std::size_t>(node)].name);
        boneNodeIndices.push_back(node);
      }
    }
    std::sort(boneNodeNames.begin(), boneNodeNames.end());
    std::sort(boneNodeIndices.begin(), boneNodeIndices.end());

    std::vector<std::size_t> appending;
    for (std::size_t i = 0; i < rig_.joints.size(); ++i)
    {
      // Match apply_rig's precedence: the node a joint was seeded from wins
      // over its name. assimp emits unnamed and duplicate-named nodes, whose
      // joints get renamed to stay addressable within the rig ("" -> "joint_1")
      // and so match nothing by name — counting those as appending is what
      // made this banner cry wolf on exactly the models the provenance field
      // was added to rescue.
      const int recorded = rig_.joints[i].sourceNode;
      const bool mergesByNode =
          recorded >= 0 && static_cast<std::size_t>(recorded) < asset.nodes.size() &&
          std::binary_search(boneNodeIndices.begin(), boneNodeIndices.end(), recorded);
      const bool mergesByName = std::binary_search(
          boneNodeNames.begin(), boneNodeNames.end(), std::string_view(rig_.joints[i].name));

      if (!mergesByNode && !mergesByName)
      {
        appending.push_back(i);
      }
    }
    if (appending.empty())
    {
      return importedBones;
    }

    // Every appending joint claiming an entry is the worst case. When even
    // that is clear of the banner there is nothing to report, and the scan
    // below — which is O(4 * vertices) over every binding — is skipped: it
    // only pays for itself when a number is actually about to be drawn.
    const std::size_t upperBound = importedBones + appending.size();
    if (upperBound + kBoneWarningMargin <= static_cast<std::size_t>(kMaxModelBones))
    {
      return upperBound;
    }

    // apply_rig gives a slot only to joints something is actually weighted
    // to, so an appending joint no vertex reaches costs nothing.
    std::vector<bool> weighted(rig_.joints.size(), false);
    bool anyWeighted = false;
    for (const RigMeshBinding &binding : rig_.meshes)
    {
      if (binding.meshIndex >= asset.meshes.size())
      {
        continue;
      }
      // apply_rig stops at the mesh's vertex count, so a binding left over
      // from a larger mesh must not be counted past it.
      const std::size_t slots = std::min(
          std::min(binding.jointIndices.size(), binding.weights.size()),
          asset.meshes[binding.meshIndex].vertices.size() * 4);
      for (std::size_t k = 0; k < slots; ++k)
      {
        const std::int32_t joint = binding.jointIndices[k];
        if (joint >= 0 && joint < static_cast<std::int32_t>(rig_.joints.size()) &&
            binding.weights[k] > 0.0f)
        {
          weighted[static_cast<std::size_t>(joint)] = true;
          anyWeighted = true;
        }
      }
    }

    std::size_t appended = 0;
    for (const std::size_t joint : appending)
    {
      if (weighted[joint])
      {
        ++appended;
      }
    }

    // A rig that binds a mesh but weighted nothing still needs one slot to
    // anchor the stray vertices on, which apply_rig takes from joint 0.
    if (!anyWeighted && appending.front() == 0)
    {
      appended = 1;
    }

    return importedBones + appended;
  }

  void AnimationEditorPlugin::draw_rig_tab(EditorPluginContext &context, const ModelAsset &asset)
  {
    if (!rigLoaded_)
    {
      load_or_seed_rig(context, asset);
    }

    // The same quantity apply_rig measures: palette entries, not hierarchy
    // nodes and not joints, counted against the import with no overlay, and
    // refused on `>` rather than `>=` — a rig sitting exactly on the budget
    // applies fine. See rig_required_bones().
    const std::size_t requiredBones = rig_required_bones(asset);
    if (requiredBones + kBoneWarningMargin > static_cast<std::size_t>(kMaxModelBones))
    {
      ImGui::TextColored(
          requiredBones > static_cast<std::size_t>(kMaxModelBones) ? error_color() : warning_color(),
          ICON_FA_TRIANGLE_EXCLAMATION "  %zu of %u bones used. The shader palette holds %u — "
          "past that the rig cannot be applied.",
          requiredBones, kMaxModelBones, kMaxModelBones);
    }

    ImGui::BeginChild(
        "##rig-left", ImVec2(300.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

    if (ImGui::Button(ICON_FA_PLUS "  Add joint"))
    {
      add_joint(asset);
    }
    ImGui::SameLine();
    if (tool_button(ICON_FA_TRASH "  Delete", "Delete the selected joint", selectedRigJoint_ >= 0))
    {
      delete_joint(selectedRigJoint_);
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint(
        "##new-joint-name", "New joint name", newJointNameBuffer_.data(), newJointNameBuffer_.size());

    ImGui::Separator();
    ImGui::BeginChild("##rig-joints", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (rig_.joints.empty())
    {
      ImGui::TextDisabled("No authored joints yet.");
    }
    for (std::size_t i = 0; i < rig_.joints.size(); ++i)
    {
      const RigJoint &joint = rig_.joints[i];
      ImGui::PushID(static_cast<int>(i));
      const bool selected = selectedRigJoint_ == static_cast<int>(i);
      // Sized rather than full-width: a default Selectable eats the whole
      // row and pushes the parent label off the edge of the list.
      const float nameWidth = std::max(90.0f, ImGui::GetContentRegionAvail().x * 0.55f);
      if (ImGui::Selectable(joint.name.c_str(), selected, ImGuiSelectableFlags_None,
                            ImVec2(nameWidth, 0.0f)))
      {
        selectedRigJoint_ = static_cast<int>(i);
        set_buffer_text(jointRenameBuffer_, joint.name);
      }
      if (!joint.parent.empty())
      {
        ImGui::SameLine();
        ImGui::TextColored(dim_color(), "< %s", joint.parent.c_str());
      }
      ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##rig-right", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);

    if (selectedRigJoint_ >= 0 && static_cast<std::size_t>(selectedRigJoint_) < rig_.joints.size())
    {
      RigJoint &joint = rig_.joints[static_cast<std::size_t>(selectedRigJoint_)];
      ImGui::SeparatorText(ICON_FA_BONE "  Joint");

      ImGui::SetNextItemWidth(200.0f);
      ImGui::InputText("##joint-rename", jointRenameBuffer_.data(), jointRenameBuffer_.size());
      ImGui::SameLine();
      if (ImGui::Button("Rename"))
      {
        rename_rig_joint(selectedRigJoint_, std::string(jointRenameBuffer_.data()));
      }

      const std::string parentPreview = joint.parent.empty() ? "(root)" : joint.parent;
      ImGui::SetNextItemWidth(240.0f);
      if (ImGui::BeginCombo("Parent", parentPreview.c_str()))
      {
        if (ImGui::Selectable("(root)", joint.parent.empty()))
        {
          reparent_joint(selectedRigJoint_, std::string());
        }

        for (std::size_t i = 0; i < rig_.joints.size(); ++i)
        {
          if (static_cast<int>(i) == selectedRigJoint_ ||
              rig_joint_is_descendant(static_cast<int>(i), selectedRigJoint_))
          {
            // Offering these at all would just be a cycle waiting to happen.
            continue;
          }
          ImGui::PushID(static_cast<int>(i));
          if (ImGui::Selectable(rig_.joints[i].name.c_str(), joint.parent == rig_.joints[i].name))
          {
            reparent_joint(selectedRigJoint_, rig_.joints[i].name);
          }
          ImGui::PopID();
        }

        if (!asset.nodes.empty())
        {
          ImGui::Separator();
          ImGui::TextDisabled("Imported nodes");
          for (std::size_t i = 0; i < asset.nodes.size(); ++i)
          {
            const std::string &nodeName = asset.nodes[i].name;
            if (nodeName.empty() || rig_.find_joint(nodeName) >= 0)
            {
              continue;
            }
            ImGui::PushID(static_cast<int>(i) + 100000);
            if (ImGui::Selectable(nodeName.c_str(), joint.parent == nodeName))
            {
              reparent_joint(selectedRigJoint_, nodeName);
            }
            ImGui::PopID();
          }
        }

        ImGui::EndCombo();
      }

      math::Vec3 euler = joint.rotation.toEulerDegrees();
      ImGui::SetNextItemWidth(240.0f);
      if (ImGui::DragFloat3("Rest translation", &joint.translation.x, 0.01f))
      {
        rigDirty_ = true;
      }
      ImGui::SetNextItemWidth(240.0f);
      if (ImGui::DragFloat3("Rest rotation", &euler.x, 0.5f))
      {
        joint.rotation = math::Quat::fromEulerDegrees(euler);
        rigDirty_ = true;
      }
      ImGui::SetNextItemWidth(240.0f);
      if (ImGui::DragFloat3("Rest scale", &joint.scale.x, 0.01f))
      {
        rigDirty_ = true;
      }
    }
    else
    {
      ImGui::TextDisabled("Select a joint to edit it.");
    }

    ImGui::SeparatorText(ICON_FA_LINK "  Weighting");

    int mode = static_cast<int>(weightMode_);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::Combo("Mode", &mode, "Rigid\0Envelope\0Smooth\0"))
    {
      weightMode_ = static_cast<AutoWeightMode>(std::clamp(mode, 0, 2));
    }
    ImGui::TextColored(dim_color(), "%s", auto_weight_mode_name(weightMode_));

    ImGui::SetNextItemWidth(200.0f);
    ImGui::DragFloat("Falloff", &weightFalloff_, 0.01f, 0.01f, 20.0f, "%.2f");
    ImGui::SetNextItemWidth(200.0f);
    ImGui::SliderInt("Max influences", &weightInfluences_, 1, 4);

    if (!asset.meshes.empty())
    {
      selectedMeshIndex_ = std::clamp(selectedMeshIndex_, 0, static_cast<int>(asset.meshes.size()) - 1);
      ImGui::SetNextItemWidth(200.0f);
      const std::string meshPreview = "Mesh " + std::to_string(selectedMeshIndex_);
      if (ImGui::BeginCombo("Mesh", meshPreview.c_str()))
      {
        for (std::size_t i = 0; i < asset.meshes.size(); ++i)
        {
          ImGui::PushID(static_cast<int>(i));
          const std::string label =
              "Mesh " + std::to_string(i) + "  (" +
              std::to_string(asset.meshes[i].vertices.size()) + " verts)";
          if (ImGui::Selectable(label.c_str(), selectedMeshIndex_ == static_cast<int>(i)))
          {
            selectedMeshIndex_ = static_cast<int>(i);
          }
          ImGui::PopID();
        }
        ImGui::EndCombo();
      }
    }

    const bool canBind = !rig_.joints.empty() && !asset.meshes.empty();
    if (tool_button(ICON_FA_LINK "  Bind all meshes", "Recompute skin weights for every mesh", canBind))
    {
      auto_weight(context, asset);
    }
    ImGui::SameLine();
    if (tool_button(ICON_FA_LINK "  Bind selected mesh", "Recompute skin weights for one mesh", canBind))
    {
      compute_auto_weights_for_mesh(
          asset, rig_, static_cast<std::uint32_t>(selectedMeshIndex_), weightMode_, weightFalloff_,
          weightInfluences_);
      rigDirty_ = true;
      statusMessage_ = "Bound mesh " + std::to_string(selectedMeshIndex_);
    }

    if (ImGui::Checkbox("Replace imported skeleton", &rig_.replaceImportedSkeleton))
    {
      rigDirty_ = true;
    }

    if (tool_button(
            ICON_FA_FLOPPY_DISK "  Save rig", "Write the rig and re-import the model",
            !targetModelPath_.empty()))
    {
      save_rig(context);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ROTATE "  Reload rig"))
    {
      rigLoaded_ = false;
    }

    if (rigDirty_)
    {
      ImGui::TextColored(warning_color(), "Rig has unsaved changes.");
    }
    if (!rigError_.empty())
    {
      ImGui::TextColored(error_color(), ICON_FA_TRIANGLE_EXCLAMATION "  %s", rigError_.c_str());
    }

    ImGui::TextDisabled(
        "%zu authored joints, %zu bound meshes", rig_.joints.size(), rig_.meshes.size());

    ImGui::EndChild();
  }

  // ---- Clips tab -----------------------------------------------------------

  void AnimationEditorPlugin::draw_clips_tab(EditorPluginContext &context, const ModelAsset &asset)
  {
    refresh_clip_list(context);

    if (ImGui::Button(ICON_FA_PLUS "  New"))
    {
      set_buffer_text(newClipNameBuffer_, std::string("clip"));
      saveWorkingClipOnCreate_ = false;
      openNewClipPopup_ = true;
    }
    ImGui::SameLine();
    if (tool_button(ICON_FA_FLOPPY_DISK "  Save", "Write the working clip", clipLoaded_ || clipDirty_))
    {
      save_clip(context);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ROTATE "  Refresh"))
    {
      clipListLoaded_ = false;
      refresh_clip_list(context);
    }

    if (clipDirty_)
    {
      ImGui::SameLine();
      ImGui::TextColored(warning_color(), "Unsaved changes in '%s'",
                         clipName_.empty() ? "<unsaved clip>" : clipName_.c_str());
    }

    ImGui::SeparatorText("Authored clips");
    ImGui::BeginChild("##clip-list", ImVec2(0.0f, 200.0f), ImGuiChildFlags_Borders);
    if (clipList_.empty())
    {
      ImGui::TextDisabled("No clips under .hades/animations yet.");
    }

    for (std::size_t i = 0; i < clipList_.size(); ++i)
    {
      const std::string &name = clipList_[i];
      ImGui::PushID(static_cast<int>(i));

      const bool isCurrent = name == clipName_;
      // Leaves room for the three row buttons; a full-width Selectable would
      // push them past the right edge of the list.
      const float nameWidth = std::max(120.0f, ImGui::GetContentRegionAvail().x - 130.0f);
      if (ImGui::Selectable(name.c_str(), isCurrent, ImGuiSelectableFlags_AllowDoubleClick,
                            ImVec2(nameWidth, 0.0f)))
      {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || !isCurrent)
        {
          if (clipDirty_ && !isCurrent)
          {
            // Refuse to drop unsaved work silently; the modal decides.
            pendingClipSwitch_ = name;
            openSwitchClipPopup_ = true;
          }
          else if (!isCurrent)
          {
            load_clip(context, name);
          }
        }
      }

      ImGui::SameLine();
      if (ImGui::SmallButton(ICON_FA_COPY))
      {
        const AnimationClipAsset *source = AnimationClipCache::instance().clip(name);
        if (source == nullptr)
        {
          errorMessage_ = AnimationClipCache::instance().errorFor(name);
        }
        else
        {
          AnimationClipAsset duplicate = *source;
          const std::string copyName = unique_name(name + "_copy", clipList_);
          duplicate.name = copyName;
          std::string error;
          if (!AnimationClipCache::instance().saveClip(copyName, duplicate, &error))
          {
            errorMessage_ = error;
          }
          else
          {
            clipListLoaded_ = false;
            statusMessage_ = "Duplicated to '" + copyName + "'";
          }
        }
      }
      if (ImGui::IsItemHovered())
      {
        ImGui::SetTooltip("Duplicate");
      }

      ImGui::SameLine();
      if (ImGui::SmallButton(ICON_FA_PEN))
      {
        pendingClipTarget_ = name;
        set_buffer_text(renameClipBuffer_, name);
        openRenameClipPopup_ = true;
      }
      if (ImGui::IsItemHovered())
      {
        ImGui::SetTooltip("Rename");
      }

      ImGui::SameLine();
      if (ImGui::SmallButton(ICON_FA_TRASH))
      {
        pendingClipTarget_ = name;
        openDeleteClipPopup_ = true;
      }
      if (ImGui::IsItemHovered())
      {
        ImGui::SetTooltip("Delete");
      }

      ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SeparatorText("Imported clips");
    if (asset.clips.empty())
    {
      ImGui::TextDisabled("This model carries no imported animations.");
    }

    for (std::size_t i = 0; i < asset.clips.size(); ++i)
    {
      const AnimationClip &imported = asset.clips[i];
      ImGui::PushID(static_cast<int>(i) + 200000);
      ImGui::Text(ICON_FA_FILM "  %s", imported.name.c_str());
      ImGui::SameLine();
      ImGui::TextColored(
          dim_color(), "%.2fs, %zu channels", static_cast<double>(imported.duration),
          imported.channels.size());
      ImGui::SameLine();
      if (ImGui::SmallButton("Bake to editable clip"))
      {
        bake_imported_clip(context, asset, static_cast<int>(i));
      }
      ImGui::PopID();
    }

    if (!clipListError_.empty())
    {
      ImGui::TextColored(error_color(), "%s", clipListError_.c_str());
    }
    draw_status_lines();
  }

  void AnimationEditorPlugin::draw_clip_dialogs(
      EditorPluginContext &context, const ModelAsset &asset)
  {
    if (openNewClipPopup_)
    {
      ImGui::OpenPopup(kNewClipPopup);
      openNewClipPopup_ = false;
    }
    if (ImGui::BeginPopupModal(kNewClipPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::TextUnformatted(
          saveWorkingClipOnCreate_ ? "Save the working clip as" : "Clip name");
      ImGui::SetNextItemWidth(280.0f);
      ImGui::InputText("##new-clip-name", newClipNameBuffer_.data(), newClipNameBuffer_.size());

      if (!errorMessage_.empty())
      {
        ImGui::TextColored(error_color(), "%s", errorMessage_.c_str());
      }

      ImGui::BeginDisabled(newClipNameBuffer_[0] == '\0');
      if (ImGui::Button(saveWorkingClipOnCreate_ ? "Save" : "Create"))
      {
        if (saveWorkingClipOnCreate_)
        {
          // Uniquified like new_clip does, so naming an unsaved clip after
          // one that already exists never silently overwrites it.
          refresh_clip_list(context);
          save_clip_as(
              context,
              unique_name(
                  sanitize_asset_name(std::string(newClipNameBuffer_.data())), clipList_));
        }
        else
        {
          new_clip(context, asset);
        }
        saveWorkingClipOnCreate_ = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();

      ImGui::SameLine();
      if (ImGui::Button("Cancel"))
      {
        saveWorkingClipOnCreate_ = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    if (openRenameClipPopup_)
    {
      ImGui::OpenPopup(kRenameClipPopup);
      openRenameClipPopup_ = false;
    }
    if (ImGui::BeginPopupModal(kRenameClipPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::Text("Rename '%s'", pendingClipTarget_.c_str());
      ImGui::SetNextItemWidth(280.0f);
      ImGui::InputText("##rename-clip", renameClipBuffer_.data(), renameClipBuffer_.size());

      if (!clipListError_.empty())
      {
        ImGui::TextColored(error_color(), "%s", clipListError_.c_str());
      }

      if (ImGui::Button("Rename"))
      {
        const std::string newName = sanitize_asset_name(std::string(renameClipBuffer_.data()));
        const AnimationClipAsset *source = AnimationClipCache::instance().clip(pendingClipTarget_);
        if (source == nullptr)
        {
          clipListError_ = AnimationClipCache::instance().errorFor(pendingClipTarget_);
        }
        else if (newName.empty() || newName == pendingClipTarget_)
        {
          ImGui::CloseCurrentPopup();
        }
        else
        {
          // Rename is copy-then-delete: the cache has no move, and a failed
          // write this way leaves the original intact.
          AnimationClipAsset renamed = *source;
          renamed.name = newName;
          std::string error;
          if (!AnimationClipCache::instance().saveClip(newName, renamed, &error))
          {
            clipListError_ = error;
          }
          else if (!AnimationClipCache::instance().deleteClip(pendingClipTarget_, &error))
          {
            clipListError_ = error;
          }
          else
          {
            if (clipName_ == pendingClipTarget_)
            {
              clipName_ = newName;
              clip_.name = newName;
            }
            clipListError_.clear();
            clipListLoaded_ = false;
            refresh_clip_list(context);
            ImGui::CloseCurrentPopup();
          }
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel"))
      {
        clipListError_.clear();
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    if (openDeleteClipPopup_)
    {
      ImGui::OpenPopup(kDeleteClipPopup);
      openDeleteClipPopup_ = false;
    }
    if (ImGui::BeginPopupModal(kDeleteClipPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::Text("Delete clip '%s'? This cannot be undone.", pendingClipTarget_.c_str());
      if (!clipListError_.empty())
      {
        ImGui::TextColored(error_color(), "%s", clipListError_.c_str());
      }

      if (ImGui::Button("Delete"))
      {
        std::string error;
        if (!AnimationClipCache::instance().deleteClip(pendingClipTarget_, &error))
        {
          clipListError_ = error;
        }
        else
        {
          if (clipName_ == pendingClipTarget_)
          {
            clipName_.clear();
            clipLoaded_ = false;
            clipDirty_ = true;
          }
          clipListError_.clear();
          clipListLoaded_ = false;
          refresh_clip_list(context);
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel"))
      {
        clipListError_.clear();
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    if (openSwitchClipPopup_)
    {
      ImGui::OpenPopup(kSwitchClipPopup);
      openSwitchClipPopup_ = false;
    }
    if (ImGui::BeginPopupModal(kSwitchClipPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::Text(
          "'%s' has unsaved changes. Load '%s' anyway?",
          clipName_.empty() ? "<unsaved clip>" : clipName_.c_str(), pendingClipSwitch_.c_str());

      if (ImGui::Button("Save and switch"))
      {
        if (clipName_.empty())
        {
          // Naming it here rather than opening the New Clip dialog: a modal
          // stacked on a modal is a trap for the user and for ImGui's id
          // stack alike.
          refresh_clip_list(context);
          const std::string derived =
              sanitize_asset_name(clip_.name.empty() ? std::string("clip") : clip_.name);
          save_clip_as(context, unique_name(derived, clipList_));
        }
        else
        {
          save_clip(context);
        }

        if (!clipDirty_)
        {
          load_clip(context, pendingClipSwitch_);
          pendingClipSwitch_.clear();
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Discard changes"))
      {
        load_clip(context, pendingClipSwitch_);
        pendingClipSwitch_.clear();
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel"))
      {
        pendingClipSwitch_.clear();
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }

  HADES_REGISTER_EDITOR_PLUGIN(AnimationEditorPlugin, 20)
}
