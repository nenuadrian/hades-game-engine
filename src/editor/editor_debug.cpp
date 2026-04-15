#include "editor.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <sstream>
#include <string>

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "../engine/components/audio_listener_component.hpp"
#include "../engine/components/audio_source_component.hpp"
#include "../engine/components/camera_component.hpp"
#include "../engine/components/collider_component.hpp"
#include "../engine/components/light_component.hpp"
#include "../engine/components/name_component.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/components/primitive_component.hpp"
#include "../engine/components/rigid_body_component.hpp"
#include "../engine/components/script_component.hpp"
#include "../engine/components/text_component.hpp"
#include "../engine/components/transform_hierarchy_component.hpp"
#include "../engine/components/world_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/world_utils.hpp"
#include "../engine/profiling/frame_metrics.hpp"
#include "../engine/runtime/script_runtime.hpp"

namespace hades
{
  namespace
  {
    constexpr char SETTINGS_WINDOW_TITLE[] = "Settings";
    constexpr char DEBUG_CONSOLE_WINDOW_TITLE[] = "Debug Console";
    constexpr std::size_t DEBUG_FRAME_HISTORY_LIMIT = 240;

    constexpr float EDITOR_SCENE_CAMERA_MIN_DISTANCE = 1.0f;
    constexpr float EDITOR_SCENE_CAMERA_MAX_DISTANCE = 250.0f;
    constexpr float EDITOR_SCENE_CAMERA_MIN_PITCH = -89.0f;
    constexpr float EDITOR_SCENE_CAMERA_MAX_PITCH = 89.0f;

    struct DebugEntityStats
    {
      std::size_t activeEntities = 0;
      std::size_t worldEntities = 0;
      std::size_t selectedEntityComponents = 0;
      std::size_t worlds = 0;
      std::size_t defaultWorlds = 0;
      std::size_t hierarchyRoots = 0;
      std::size_t namedEntities = 0;
      std::size_t cameras = 0;
      std::size_t mainCameras = 0;
      std::size_t lights = 0;
      std::size_t primitives = 0;
      std::size_t textEntities = 0;
      std::size_t scriptedEntities = 0;
      std::size_t rigidBodies = 0;
      std::size_t colliders = 0;
      std::size_t audioSources = 0;
      std::size_t audioListeners = 0;
      std::size_t totalComponentBits = 0;
    };

    std::string format_pair(float x, float y)
    {
      std::ostringstream stream;
      stream << static_cast<int>(x) << ", " << static_cast<int>(y);
      return stream.str();
    }

    std::string format_pair(int x, int y)
    {
      std::ostringstream stream;
      stream << x << " x " << y;
      return stream.str();
    }

    void add_stat_row_label_value(const char *label, const std::string &value)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(value.c_str());
    }

    void add_stat_row_label_value(const char *label, const char *value)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(value);
    }

    void add_stat_row_label_value(const char *label, std::size_t value)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      ImGui::TableNextColumn();
      ImGui::Text("%zu", value);
    }

    void add_stat_row_label_value(const char *label, double value, const char *suffix = "")
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f%s", value, suffix);
    }

    void add_stat_row_label_value(const char *label, float value, const char *suffix = "")
    {
      add_stat_row_label_value(label, static_cast<double>(value), suffix);
    }

    DebugEntityStats collect_debug_entity_stats(
        EntityManager &entityManager,
        ComponentManager &componentManager,
        std::optional<Entity::EntityId> worldFilter,
        std::optional<Entity::EntityId> selectedEntity)
    {
      DebugEntityStats stats;
      const auto &activeEntities = entityManager.getActiveEntities();
      stats.activeEntities = activeEntities.size();

      for (Entity::EntityId entity : activeEntities)
      {
        if (!worldFilter.has_value() || entity_belongs_to_world(entity, *worldFilter, componentManager))
        {
          stats.worldEntities += 1;
        }

        const auto &signature = entityManager.getComponentSignature(entity);
        stats.totalComponentBits += signature.count();
        if (selectedEntity.has_value() && entity == *selectedEntity)
        {
          stats.selectedEntityComponents = signature.count();
        }

        if (componentManager.hasComponent<WorldComponent>(entity))
        {
          stats.worlds += 1;
          if (componentManager.getComponent<WorldComponent>(entity).isDefault)
          {
            stats.defaultWorlds += 1;
          }
        }
        if (componentManager.hasComponent<NameComponent>(entity))
        {
          stats.namedEntities += 1;
        }
        if (componentManager.hasComponent<TransformHierarchyComponent>(entity) &&
            !componentManager.getComponent<TransformHierarchyComponent>(entity).parent.has_value())
        {
          stats.hierarchyRoots += 1;
        }
        if (componentManager.hasComponent<CameraComponent>(entity))
        {
          stats.cameras += 1;
          if (componentManager.getComponent<CameraComponent>(entity).isMainCamera)
          {
            stats.mainCameras += 1;
          }
        }
        if (componentManager.hasComponent<LightComponent>(entity))
        {
          stats.lights += 1;
        }
        if (componentManager.hasComponent<PrimitiveComponent>(entity))
        {
          stats.primitives += 1;
        }
        if (componentManager.hasComponent<TextComponent>(entity))
        {
          stats.textEntities += 1;
        }
        if (componentManager.hasComponent<ScriptComponent>(entity))
        {
          stats.scriptedEntities += 1;
        }
        if (componentManager.hasComponent<RigidBodyComponent>(entity))
        {
          stats.rigidBodies += 1;
        }
        if (componentManager.hasComponent<ColliderComponent>(entity))
        {
          stats.colliders += 1;
        }
        if (componentManager.hasComponent<AudioSourceComponent>(entity))
        {
          stats.audioSources += 1;
        }
        if (componentManager.hasComponent<AudioListenerComponent>(entity))
        {
          stats.audioListeners += 1;
        }
      }

      return stats;
    }
  }

  void Editor::render_settings_window()
  {
    if (!openSettingsWindow_)
    {
      return;
    }

    ImGui::SetNextWindowSize(ImVec2(720.0f, 460.0f), ImGuiCond_FirstUseEver);
    if (focusSettingsWindow_)
    {
      ImGui::SetNextWindowFocus();
      focusSettingsWindow_ = false;
    }

    if (!ImGui::Begin(SETTINGS_WINDOW_TITLE, &openSettingsWindow_))
    {
      ImGui::End();
      return;
    }

    if (ImGui::BeginTable(
            "SettingsLayout",
            2,
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
    {
      ImGui::TableSetupColumn("Categories", ImGuiTableColumnFlags_WidthFixed, 180.0f);
      ImGui::TableSetupColumn("Values", ImGuiTableColumnFlags_WidthStretch);

      ImGui::TableNextColumn();
      ImGui::BeginChild("SettingsCategories", ImVec2(0.0f, 0.0f), true);
      if (ImGui::Selectable("Editor", selectedSettingsCategory_ == SettingsCategory::Editor))
      {
        selectedSettingsCategory_ = SettingsCategory::Editor;
      }
      if (ImGui::Selectable("Game Preview", selectedSettingsCategory_ == SettingsCategory::GamePreview))
      {
        selectedSettingsCategory_ = SettingsCategory::GamePreview;
      }
      if (ImGui::Selectable("Plugins", selectedSettingsCategory_ == SettingsCategory::Plugins))
      {
        selectedSettingsCategory_ = SettingsCategory::Plugins;
      }
      ImGui::EndChild();

      ImGui::TableNextColumn();
      ImGui::BeginChild("SettingsValues", ImVec2(0.0f, 0.0f), false);

      if (selectedSettingsCategory_ == SettingsCategory::Editor)
      {
        ImGui::TextDisabled("Editor");
        ImGui::Separator();
        ImGui::Checkbox("Show Stats for Nerds", &state.showDebugInfo);

        ImGui::Spacing();
        ImGui::TextDisabled("Scene Camera");
        ImGui::Separator();

        float target[3] = {sceneCameraTargetX_, sceneCameraTargetY_, sceneCameraTargetZ_};
        if (ImGui::DragFloat3("Target", target, 0.1f))
        {
          sceneCameraTargetX_ = target[0];
          sceneCameraTargetY_ = target[1];
          sceneCameraTargetZ_ = target[2];
        }

        ImGui::SliderFloat(
            "Distance",
            &sceneCameraDistance_,
            EDITOR_SCENE_CAMERA_MIN_DISTANCE,
            EDITOR_SCENE_CAMERA_MAX_DISTANCE,
            "%.1f");
        ImGui::SliderFloat("Yaw", &sceneCameraYawDegrees_, -180.0f, 180.0f, "%.1f deg");
        ImGui::SliderFloat(
            "Pitch",
            &sceneCameraPitchDegrees_,
            EDITOR_SCENE_CAMERA_MIN_PITCH,
            EDITOR_SCENE_CAMERA_MAX_PITCH,
            "%.1f deg");

        if (ImGui::Button("Reset Scene Camera"))
        {
          reset_scene_camera();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Scripting");
        ImGui::Separator();

        static const char *editorNames[] = {"Visual Studio Code", "Rider", "Visual Studio", "System Default"};
        int editorIndex = static_cast<int>(externalEditor_);
        if (ImGui::Combo("External Editor", &editorIndex, editorNames, IM_ARRAYSIZE(editorNames)))
        {
          externalEditor_ = static_cast<ExternalEditor>(editorIndex);
        }
      }
      else if (selectedSettingsCategory_ == SettingsCategory::GamePreview)
      {
        ImGui::TextDisabled("Game Preview");
        ImGui::Separator();

#ifdef HADES_ENABLE_API
        ImGui::Checkbox("Enable HadesAPI while playing", &gamePreviewEnableHadesAPI_);
        ImGui::TextDisabled("When enabled, clicking Play starts the preview with HadesAPI on port 7777.");
#else
        bool disabledToggle = false;
        ImGui::BeginDisabled();
        ImGui::Checkbox("Enable HadesAPI while playing", &disabledToggle);
        ImGui::EndDisabled();
        ImGui::TextDisabled("This editor build does not include HadesAPI support.");
#endif
      }
      else
      {
        ImGui::TextDisabled("Plugins");
        ImGui::Separator();

        bool hasPluginSettings = false;
        for (const auto &plugin : plugins_)
        {
          if (!should_expose_plugin_setting(*plugin))
          {
            continue;
          }

          hasPluginSettings = true;
          bool visible = plugin->visible(*this);
          const std::string label(plugin->display_name());
          if (ImGui::Checkbox(label.c_str(), &visible))
          {
            plugin->set_visible(*this, visible);
          }
        }

        if (!hasPluginSettings)
        {
          ImGui::TextDisabled("No plugins available.");
        }
      }

      ImGui::EndChild();
      ImGui::EndTable();
    }

    ImGui::End();
  }

  void Editor::render_debug_console_window()
  {
    if (!openDebugConsoleWindow_)
    {
      return;
    }

    ImGui::SetNextWindowSize(ImVec2(600.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (focusDebugConsoleWindow_)
    {
      ImGui::SetNextWindowFocus();
      focusDebugConsoleWindow_ = false;
    }

    if (!ImGui::Begin(DEBUG_CONSOLE_WINDOW_TITLE, &openDebugConsoleWindow_))
    {
      ImGui::End();
      return;
    }

    mainDebugConsole_.render("##DebugConsoleText");

    ImGui::End();
  }

  void Editor::render_about_window()
  {
    if (!openAboutWindow_)
    {
      return;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 220.0f), ImGuiCond_FirstUseEver);
    if (focusAboutWindow_)
    {
      ImGui::SetNextWindowFocus();
      focusAboutWindow_ = false;
    }

    if (!ImGui::Begin("About", &openAboutWindow_))
    {
      ImGui::End();
      return;
    }

    ImGui::TextUnformatted("Hades Game Engine");
    ImGui::Separator();
    ImGui::TextUnformatted("An open-source game engine editor and runtime.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Use Help -> Stats for Nerds to show performance stats.");

    ImGui::End();
  }

  void Editor::debug(float deltaTime, EntityManager &entityManager, ComponentManager &componentManager, ScriptRuntime &scriptRuntime)
  {
    if (!state.showDebugInfo)
    {
      return;
    }

    debugFrameTimeHistory_.push_back(deltaTime);
    debugFrameTimeHistoryTotal_ += deltaTime;
    while (debugFrameTimeHistory_.size() > DEBUG_FRAME_HISTORY_LIMIT)
    {
      debugFrameTimeHistoryTotal_ -= debugFrameTimeHistory_.front();
      debugFrameTimeHistory_.pop_front();
    }

    if (!ImGui::Begin("Stats for Nerds", &state.showDebugInfo))
    {
      ImGui::End();
      return;
    }

    const auto [minFrameIt, maxFrameIt] = std::minmax_element(
        debugFrameTimeHistory_.begin(),
        debugFrameTimeHistory_.end());
    const double averageFrameTimeMs =
        debugFrameTimeHistory_.empty() ? 0.0 : (debugFrameTimeHistoryTotal_ / static_cast<double>(debugFrameTimeHistory_.size())) * 1000.0;
    const double currentFrameTimeMs = deltaTime * 1000.0;
    const double minFrameTimeMs = debugFrameTimeHistory_.empty() ? 0.0 : (*minFrameIt * 1000.0);
    const double maxFrameTimeMs = debugFrameTimeHistory_.empty() ? 0.0 : (*maxFrameIt * 1000.0);
    const double frameBudgetPercent = currentFrameTimeMs / 16.6666666667 * 100.0;
    const std::optional<Entity::EntityId> statsWorld = state.isPlaying ? state.activeWorld : state.loadedWorld;
    const DebugEntityStats entityStats =
        collect_debug_entity_stats(entityManager, componentManager, statsWorld, state.selectedEntity);
    std::size_t warningCount = 0;
    std::size_t errorCount = 0;
    for (const auto &message : mainDebugConsole_.messages())
    {
      if (message.level == DebugMessageLevel::Warning)
      {
        warningCount += 1;
      }
      else if (message.level == DebugMessageLevel::Error)
      {
        errorCount += 1;
      }
    }

    double newestMessageAgeSeconds = 0.0;
    if (!mainDebugConsole_.empty())
    {
      newestMessageAgeSeconds = std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() - mainDebugConsole_.messages().back().timestamp)
                                    .count();
    }

    ImDrawData *drawData = ImGui::GetDrawData();
    const ImGuiIO &io = ImGui::GetIO();

    ImGui::Text("FPS: %.1f  (%.2f ms)", deltaTime > 0.0f ? (1.0f / deltaTime) : 0.0f, currentFrameTimeMs);
    ImGui::SameLine();
    ImGui::TextDisabled("| %.0f%% of 60 FPS budget", frameBudgetPercent);
    if (!state.playModeMessage.empty())
    {
      ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Play error: %s", state.playModeMessage.c_str());
    }

    if (ImGui::CollapsingHeader("Frame Pacing", ImGuiTreeNodeFlags_DefaultOpen) &&
        ImGui::BeginTable("##frame_pacing", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
      add_stat_row_label_value("Current frame", currentFrameTimeMs, " ms");
      add_stat_row_label_value("Rolling average", averageFrameTimeMs, " ms");
      add_stat_row_label_value("Rolling min", minFrameTimeMs, " ms");
      add_stat_row_label_value("Rolling max", maxFrameTimeMs, " ms");
      add_stat_row_label_value("History samples", debugFrameTimeHistory_.size());
      add_stat_row_label_value("Frame budget", frameBudgetPercent, "%");
      ImGui::EndTable();
    }

    if (ImGui::CollapsingHeader("Runtime State", ImGuiTreeNodeFlags_DefaultOpen) &&
        ImGui::BeginTable("##runtime_state", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
      add_stat_row_label_value("Play mode", state.isPlaying ? "Playing" : "Stopped");
      add_stat_row_label_value("Script runtime", scriptRuntime.is_running() ? "Running" : "Stopped");
      add_stat_row_label_value("Script faulted", scriptRuntime.faulted() ? "Yes" : "No");
      add_stat_row_label_value("Loaded world", state.loadedWorld.has_value() ? std::to_string(*state.loadedWorld) : std::string("None"));
      add_stat_row_label_value("Active world", state.activeWorld.has_value() ? std::to_string(*state.activeWorld) : std::string("None"));
      add_stat_row_label_value("Active camera", state.activeCamera.has_value() ? std::to_string(*state.activeCamera) : std::string("None"));
      add_stat_row_label_value("Selected entity", state.selectedEntity.has_value() ? std::to_string(*state.selectedEntity) : std::string("None"));
      add_stat_row_label_value("Selected entity components", entityStats.selectedEntityComponents);
      add_stat_row_label_value(
          "Scene gizmo mode",
          sceneGizmoMode_ == SceneGizmoMode::Translate ? "Translate"
          : sceneGizmoMode_ == SceneGizmoMode::Rotate   ? "Rotate"
                                                       : "Scale");
      add_stat_row_label_value(
          "Active gizmo axis",
          activeSceneGizmoAxis_ == SceneGizmoAxis::X   ? "X"
          : activeSceneGizmoAxis_ == SceneGizmoAxis::Y ? "Y"
          : activeSceneGizmoAxis_ == SceneGizmoAxis::Z ? "Z"
                                                       : "None");
      ImGui::EndTable();
    }

    if (ImGui::CollapsingHeader("World and ECS", ImGuiTreeNodeFlags_DefaultOpen) &&
        ImGui::BeginTable("##ecs_stats", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
      add_stat_row_label_value("Active entities", entityStats.activeEntities);
      add_stat_row_label_value("Entities in debug world", entityStats.worldEntities);
      add_stat_row_label_value("World entities", entityStats.worlds);
      add_stat_row_label_value("Default worlds", entityStats.defaultWorlds);
      add_stat_row_label_value("Hierarchy roots", entityStats.hierarchyRoots);
      add_stat_row_label_value("Named entities", entityStats.namedEntities);
      add_stat_row_label_value("Cameras", entityStats.cameras);
      add_stat_row_label_value("Main cameras", entityStats.mainCameras);
      add_stat_row_label_value("Lights", entityStats.lights);
      add_stat_row_label_value("Primitive entities", entityStats.primitives);
      add_stat_row_label_value("Text entities", entityStats.textEntities);
      add_stat_row_label_value("Scripted entities", entityStats.scriptedEntities);
      add_stat_row_label_value("Rigid bodies", entityStats.rigidBodies);
      add_stat_row_label_value("Colliders", entityStats.colliders);
      add_stat_row_label_value("Audio sources", entityStats.audioSources);
      add_stat_row_label_value("Audio listeners", entityStats.audioListeners);
      add_stat_row_label_value("Total component bits", entityStats.totalComponentBits);
      ImGui::EndTable();
    }

    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen) &&
        ImGui::BeginTable("##rendering_stats", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
      add_stat_row_label_value("Editor viewport lights", sceneRenderList_.lights.size());
      add_stat_row_label_value("Opaque draw items", sceneRenderList_.opaqueItems.size());
      add_stat_row_label_value("Transparent draw items", sceneRenderList_.transparentItems.size());
      add_stat_row_label_value("Visible entities", sceneRenderList_.totalVisibleEntities);
      add_stat_row_label_value("Culled entities", sceneRenderList_.totalCulledEntities);
      add_stat_row_label_value("Estimated triangles", sceneRenderList_.totalTriangles);
      add_stat_row_label_value("Camera distance", sceneCameraDistance_);
      add_stat_row_label_value("Camera yaw", sceneCameraYawDegrees_, " deg");
      add_stat_row_label_value("Camera pitch", sceneCameraPitchDegrees_, " deg");
      ImGui::EndTable();
    }

    if (ImGui::CollapsingHeader("Workspace and Scripts", ImGuiTreeNodeFlags_DefaultOpen) &&
        ImGui::BeginTable("##workspace_stats", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
      add_stat_row_label_value("Workspace loaded", activeWorkspacePath_.empty() ? "No" : "Yes");
      add_stat_row_label_value("Saved worlds on disk", cachedDiskWorlds_.size());
      add_stat_row_label_value("Workspace scripts", workspaceScriptFiles_.size());
      add_stat_row_label_value("Parsed script cache entries", parsedScriptCache_.size());
      add_stat_row_label_value("Restore saved worlds pending", pendingSavedWorldRestore_ ? "Yes" : "No");
      ImGui::EndTable();
    }

    if (ImGui::CollapsingHeader("Editor and ImGui", ImGuiTreeNodeFlags_DefaultOpen) &&
        ImGui::BeginTable("##imgui_stats", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
      add_stat_row_label_value("Display size", format_pair(static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y)));
      add_stat_row_label_value("Framebuffer scale", format_pair(io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y));
      add_stat_row_label_value("Mouse position", format_pair(io.MousePos.x, io.MousePos.y));
      add_stat_row_label_value("Want capture mouse", io.WantCaptureMouse ? "Yes" : "No");
      add_stat_row_label_value("Want capture keyboard", io.WantCaptureKeyboard ? "Yes" : "No");
      add_stat_row_label_value("Render vertices", drawData != nullptr ? static_cast<std::size_t>(drawData->TotalVtxCount) : 0);
      add_stat_row_label_value("Render indices", drawData != nullptr ? static_cast<std::size_t>(drawData->TotalIdxCount) : 0);
      add_stat_row_label_value("Command lists", drawData != nullptr ? static_cast<std::size_t>(drawData->CmdListsCount) : 0);
      add_stat_row_label_value("Debug console messages", mainDebugConsole_.size());
      add_stat_row_label_value("Warnings", warningCount);
      add_stat_row_label_value("Errors", errorCount);
      add_stat_row_label_value("Newest log age", newestMessageAgeSeconds, " s");
      ImGui::EndTable();
    }

#ifdef HADES_ENABLE_FRAME_METRICS
    if (ImGui::CollapsingHeader("Frame Metrics", ImGuiTreeNodeFlags_DefaultOpen) &&
        ImGui::BeginTable("##metrics", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
      ImGui::TableSetupColumn("Section");
      ImGui::TableSetupColumn("Last");
      ImGui::TableSetupColumn("Avg");
      ImGui::TableSetupColumn("Min");
      ImGui::TableSetupColumn("Max");
      ImGui::TableSetupColumn("Frame %");
      ImGui::TableSetupColumn("Calls");
      ImGui::TableSetupColumn("Lifetime Calls");
      ImGui::TableHeadersRow();

      const auto &entries = FrameMetrics::instance().entries();
      for (const auto &entry : entries)
      {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(entry.name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", entry.lastMs);
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", entry.frames > 0 ? entry.totalMs / entry.frames : 0.0);
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", entry.minMs);
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", entry.maxMs);
        ImGui::TableNextColumn();
        ImGui::Text("%.1f%%", currentFrameTimeMs > 0.0 ? (entry.lastMs / currentFrameTimeMs) * 100.0 : 0.0);
        ImGui::TableNextColumn();
        ImGui::Text("%u", entry.count);
        ImGui::TableNextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(entry.totalCount));
      }
      ImGui::EndTable();
    }
#endif

    ImGui::End();
  }
}
