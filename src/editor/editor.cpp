#include "editor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>

#include "imgui.h"
#include "imgui_internal.h"
#include "IconsFontAwesome6.h"
#include "../engine/components/name_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/scene_serializer.hpp"
#include "../engine/core/ecs/world_utils.hpp"
#include "../engine/gui/imgui.hpp"
#include "../engine/profiling/frame_metrics.hpp"
#include "../engine/runtime/script_runtime.hpp"

namespace hades
{
  namespace
  {
    constexpr char ENTITY_WINDOW_TITLE[] = "Entities";
    constexpr char WORKSPACE_WINDOW_TITLE[] = "Workspace";
    constexpr char PROPERTIES_WINDOW_TITLE[] = "Properties";
    constexpr char SCENE_WINDOW_TITLE[] = "World";
  }

  Editor::Editor() : gui(std::make_unique<ImGui_GUI>())
  {
    reset_scene_camera();
    register_builtin_plugins();
  }

  Editor::~Editor()
  {
    if (exportBuildThread_.joinable())
    {
      exportBuildThread_.join();
    }
  }

  void Editor::reset_workspace_session()
  {
    state.selectedEntity.reset();
    state.pendingEntityPreset = EditorEntityPreset::None;
    state.pendingPlayAction = EditorPlayAction::None;
    state.isPlaying = false;
    state.loadedWorld.reset();
    state.activeWorld.reset();
    state.activeCamera.reset();
    state.playModeMessage.clear();
    pendingEntityDeletion_.reset();
    openAddEntityDialog_ = false;
    focusAddEntitySearch_ = false;
    pendingAddEntityParent_.reset();
    addEntitySearchBuffer_[0] = '\0';
    reset_scene_camera();
    activeSceneGizmoAxis_ = SceneGizmoAxis::None;
    activeSceneGizmoEntity_ = Entity::INVALID;
    sceneGizmoDragStartMouseX_ = 0.0f;
    sceneGizmoDragStartMouseY_ = 0.0f;
    sceneGizmoDragStartPositionX_ = 0.0f;
    sceneGizmoDragStartPositionY_ = 0.0f;
    sceneGizmoDragStartPositionZ_ = 0.0f;
    sceneGizmoAxisScreenDirectionX_ = 0.0f;
    sceneGizmoAxisScreenDirectionY_ = 0.0f;
    sceneGizmoPixelsPerWorldUnit_ = 1.0f;
    pendingSavedWorldRestore_ = false;

    scriptAutoComplete_.reset();

    activeWorkspacePath_.clear();
    workspaceTreeRoot_.reset();
    workspaceScriptFiles_.clear();
    workspaceScanError_.clear();
    openScriptEditorTabs_.clear();
    activeScriptEditorTabIndex_.reset();
    pendingScriptEditorTabSelectionIndex_.reset();
    scriptEditorStatusMessage_.clear();
    scriptEditorStatusIsError_ = false;
    openScriptEditorWindow_ = false;
    focusScriptEditorWindow_ = false;
    openScriptEditorUnsavedChangesDialog_ = false;
    pendingScriptEditorClosePath_.reset();
    pendingCloseScriptEditorWindow_ = false;
    cachedDiskWorlds_.clear();
    workspaceScriptListDirty_ = false;
    parsedScriptCache_.clear();
    parsedScriptModTimes_.clear();
    lastCompileError_.clear();
    scriptCompileStatus_ = ScriptCompileStatus::Unknown;
    backgroundCompileInProgress_ = false;
    currentCompileRequestId_ = 0;
    nextCompileRequestId_ = 0;
    selectedSettingsCategory_ = SettingsCategory::Editor;
    mainDebugConsole_.clear();
    scriptEditorDebugConsole_.clear();
    openDebugConsoleWindow_ = false;
    focusDebugConsoleWindow_ = false;
    openAboutWindow_ = false;
    focusAboutWindow_ = false;
    openExportWindow_ = false;
    focusExportWindow_ = false;
    if (exportBuildThread_.joinable())
    {
      exportBuildThread_.join();
    }
    exportBuildInProgress_ = false;
    exportBuildState_.reset();
    exportBuildLog_.clear();
    exportBuildError_.clear();
    exportBuildSucceeded_ = false;
    exportBuildFinished_ = false;
  }

  void Editor::log_message(DebugMessageLevel level, const std::string &text)
  {
    const char *prefix = "INFO";
    if (level == DebugMessageLevel::Warning)
    {
      prefix = "WARNING";
    }
    else if (level == DebugMessageLevel::Error)
    {
      prefix = "ERROR";
    }
    std::fprintf(stderr, "[%s] %s\n", prefix, text.c_str());

    mainDebugConsole_.add_message(level, text);

    if (level == DebugMessageLevel::Error)
    {
      openDebugConsoleWindow_ = true;
      focusDebugConsoleWindow_ = true;
    }
  }

  void Editor::log_info(const std::string &text)
  {
    log_message(DebugMessageLevel::Info, text);
  }

  void Editor::log_warning(const std::string &text)
  {
    log_message(DebugMessageLevel::Warning, text);
  }

  void Editor::log_error(const std::string &text)
  {
    log_message(DebugMessageLevel::Error, text);
  }

  bool Editor::load_workspace_settings(const std::filesystem::path &workspacePath, std::string *errorMessage)
  {
    WorkspaceEditorSettings settings = capture_workspace_settings();
    if (!hades::load_workspace_settings(workspacePath, settings, errorMessage))
    {
      return false;
    }

    apply_workspace_settings(settings);
    return true;
  }

  bool Editor::save_workspace_settings(const std::filesystem::path &workspacePath, std::string *errorMessage) const
  {
    return hades::save_workspace_settings(workspacePath, capture_workspace_settings(), errorMessage);
  }

  void Editor::render(
      float deltaTime,
      const std::filesystem::path &workspacePath,
      EntityManager &entityManager,
      ComponentManager &componentManager,
      ScriptRuntime &scriptRuntime)
  {
    {
      HADES_FRAME_METRIC_SCOPE("workspace_cache");
      refresh_workspace_cache(workspacePath);
    }
    restore_saved_worlds_if_needed(entityManager, componentManager);
    ensure_world_state(entityManager, componentManager);
    {
      HADES_FRAME_METRIC_SCOPE("menu_bar");
      sync_menu_bar(entityManager, componentManager);
    }
    configure_default_dock_layout(gui->render_frame());

    if (backgroundCompileInProgress_ && backgroundCompileResult_.valid() &&
        backgroundCompileResult_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
      BackgroundCompileTaskResult result = backgroundCompileResult_.get();
      backgroundCompileInProgress_ = false;
      if (result.requestId == currentCompileRequestId_)
      {
        lastCompileError_ = std::move(result.error);
        scriptCompileStatus_ = lastCompileError_.empty()
                                   ? ScriptCompileStatus::Succeeded
                                   : ScriptCompileStatus::Failed;
      }
    }

    render_add_entity_dialog(entityManager, componentManager);
    handle_entity_creation_requests(entityManager, componentManager);
    import_model(entityManager, componentManager);
    handle_play_mode_requests(entityManager, componentManager, scriptRuntime);

    EditorPluginContext pluginContext{
        *this,
        deltaTime,
        workspacePath,
        entityManager,
        componentManager,
        scriptRuntime,
    };
    {
      HADES_FRAME_METRIC_SCOPE("plugins_pre");
      render_plugins(EditorPluginPhase::PreEntityDeletion, pluginContext);
    }
    handle_entity_deletion_requests(entityManager, componentManager, scriptRuntime);
    {
      HADES_FRAME_METRIC_SCOPE("plugins_post");
      render_plugins(EditorPluginPhase::PostEntityDeletion, pluginContext);
    }
    render_workspace_dialogs(entityManager, componentManager);
    render_about_window();
  }

  void Editor::sync_menu_bar(EntityManager &entityManager, ComponentManager &componentManager)
  {
    gui->menu_bar_items.clear();

    const auto add_plugin_toggle_item = [this](MenuBarItem &menu, const char *title, std::string_view pluginId)
    {
      MenuBarItem item;
      item.title = title;
      item.selected = is_plugin_visible(pluginId);
      item.on_activate = [this, pluginId]()
      {
        if (EditorPlugin *plugin = find_plugin(pluginId))
        {
          plugin->set_visible(*this, !plugin->visible(*this));
        }
      };
      menu.children_menu_items.push_back(std::move(item));
    };

    MenuBarItem file;
    file.title = ICON_FA_FILE "  File";

    MenuBarItem newWorld;
    newWorld.title = ICON_FA_PLUS "  New World";
    newWorld.on_activate = [this, &entityManager, &componentManager]()
    {
      create_world(entityManager, componentManager);
    };

    MenuBarItem exit;
    exit.title = ICON_FA_DOOR_OPEN "  Exit";
    exit.on_activate = [this]()
    {
      state.events.push(EDITOR_QUIT);
    };

    MenuBarItem save;
    save.title = ICON_FA_FLOPPY_DISK "  Save";
    save.on_activate = [this, &entityManager, &componentManager]()
    {
      save_worlds(entityManager, componentManager);
    };

    MenuBarItem exportItem;
    exportItem.title = ICON_FA_FILE_EXPORT "  Export...";
    exportItem.on_activate = [this]()
    {
      show_plugin("export");
    };

    file.children_menu_items.push_back(newWorld);
    file.children_menu_items.push_back(save);
    file.children_menu_items.push_back(exportItem);

    MenuBarItem settingsWindow;
    settingsWindow.title = ICON_FA_GEAR "  Settings";
    settingsWindow.selected = is_plugin_visible("settings");
    settingsWindow.on_activate = [this]()
    {
      if (is_plugin_visible("settings"))
      {
        if (EditorPlugin *plugin = find_plugin("settings"))
        {
          plugin->set_visible(*this, false);
        }
      }
      else
      {
        show_plugin("settings");
      }
    };
    file.children_menu_items.push_back(std::move(settingsWindow));

    file.children_menu_items.push_back(exit);
    gui->menu_bar_items.push_back(file);

    MenuBarItem worlds;
    worlds.title = ICON_FA_LAYER_GROUP "  Worlds";

    // Collect in-memory world entities keyed by their plain name.
    const auto memoryWorlds = find_world_entities(entityManager, componentManager);
    std::unordered_map<std::string, Entity::EntityId> memoryWorldsByName;
    for (Entity::EntityId world : memoryWorlds)
    {
      std::string name = "World";
      if (componentManager.hasComponent<NameComponent>(world))
      {
        name = componentManager.getComponent<NameComponent>(world).value;
      }
      memoryWorldsByName[name] = world;
    }

    const auto &diskWorlds = cachedDiskWorlds_;

    // Build a combined list: disk worlds first, then any unsaved in-memory worlds.
    std::vector<std::string> allWorldNames = diskWorlds;
    for (const auto &[name, id] : memoryWorldsByName)
    {
      if (std::find(allWorldNames.begin(), allWorldNames.end(), name) == allWorldNames.end())
      {
        allWorldNames.push_back(name);
      }
    }

    if (allWorldNames.empty())
    {
      MenuBarItem emptyWorlds;
      emptyWorlds.title = ICON_FA_CIRCLE_INFO "  No Worlds Available";
      emptyWorlds.enabled = false;
      worlds.children_menu_items.push_back(emptyWorlds);
    }
    else
    {
      const bool hasDiskWorlds = !diskWorlds.empty();
      for (const auto &worldName : allWorldNames)
      {
        auto memIt = memoryWorldsByName.find(worldName);
        const bool inMemory = memIt != memoryWorldsByName.end();
        const bool onDisk = hasDiskWorlds &&
                            std::find(diskWorlds.begin(), diskWorlds.end(), worldName) != diskWorlds.end();

        const bool isLoaded = inMemory && state.loadedWorld.has_value() &&
                              *state.loadedWorld == memIt->second;
        const bool isDefault = inMemory &&
                               componentManager.hasComponent<WorldComponent>(memIt->second) &&
                               componentManager.getComponent<WorldComponent>(memIt->second).isDefault;

        // Build a submenu for each world.
        MenuBarItem worldItem;
        std::string displayName = std::string(ICON_FA_FILE) + "  " + worldName;
        if (isLoaded)
        {
          displayName += " (loaded)";
        }
        if (isDefault)
        {
          displayName += " [Default]";
        }
        worldItem.title = displayName;

        // "Load" action
        MenuBarItem loadItem;
        loadItem.title = ICON_FA_FOLDER_OPEN "  Load";
        loadItem.selected = false;
        if (onDisk)
        {
          loadItem.on_activate = [this, worldName, &entityManager, &componentManager]()
          {
            open_world_from_disk(worldName, entityManager, componentManager);
          };
        }
        else if (inMemory)
        {
          Entity::EntityId worldId = memIt->second;
          loadItem.on_activate = [this, &componentManager, worldId]()
          {
            load_world(worldId, componentManager);
          };
        }
        worldItem.children_menu_items.push_back(std::move(loadItem));

        // "Set as Default" action (only for in-memory worlds)
        if (inMemory)
        {
          MenuBarItem setDefaultItem;
          setDefaultItem.title = ICON_FA_STAR "  Set as Default";
          setDefaultItem.selected = isDefault;
          Entity::EntityId worldId = memIt->second;
          setDefaultItem.on_activate = [this, worldId, &entityManager, &componentManager]()
          {
            set_default_world(worldId, entityManager, componentManager);
          };
          worldItem.children_menu_items.push_back(std::move(setDefaultItem));
        }

        // "Delete" action (disabled for the default/startup world)
        if (inMemory)
        {
          Entity::EntityId worldId = memIt->second;
          MenuBarItem deleteItem;
          deleteItem.title = ICON_FA_TRASH "  Delete";
          deleteItem.enabled = !isDefault;
          deleteItem.on_activate = [this, worldId]()
          {
            request_entity_deletion(worldId);
          };
          worldItem.children_menu_items.push_back(std::move(deleteItem));
        }

        worlds.children_menu_items.push_back(std::move(worldItem));
      }
    }

    gui->menu_bar_items.push_back(worlds);

    MenuBarItem windows;
    windows.title = ICON_FA_WINDOW_MAXIMIZE "  Windows";

    MenuBarItem codeEditorWindow;
    codeEditorWindow.title = ICON_FA_CODE "  Code Editor";
    codeEditorWindow.selected = is_script_editor_window_open();
    codeEditorWindow.on_activate = [this]()
    {
      if (is_script_editor_window_open())
      {
        set_script_editor_window_open(false);
      }
      else
      {
        show_plugin("script-editor-window");
      }
    };
    windows.children_menu_items.push_back(std::move(codeEditorWindow));

    add_plugin_toggle_item(windows, ICON_FA_FOLDER_OPEN "  Workspace", "workspace");
    add_plugin_toggle_item(windows, ICON_FA_LAYER_GROUP "  Entities", "entities");
    add_plugin_toggle_item(windows, ICON_FA_GEAR "  Properties", "properties");
    add_plugin_toggle_item(windows, ICON_FA_FILE "  World", "world");
    add_plugin_toggle_item(windows, ICON_FA_CHART_LINE "  Debug Console", "debug-console");
    gui->menu_bar_items.push_back(std::move(windows));

    MenuBarItem help;
    help.title = ICON_FA_CIRCLE_QUESTION "  Help";

    MenuBarItem about;
    about.title = ICON_FA_CIRCLE_INFO "  About";
    about.on_activate = [this]()
    {
      openAboutWindow_ = true;
      focusAboutWindow_ = true;
    };
    help.children_menu_items.push_back(std::move(about));

    MenuBarItem statsForNerds;
    statsForNerds.title = ICON_FA_CHART_LINE "  Stats for Nerds";
    statsForNerds.selected = state.showDebugInfo;
    statsForNerds.on_activate = [this]()
    {
      state.showDebugInfo = !state.showDebugInfo;
    };
    help.children_menu_items.push_back(std::move(statsForNerds));

    gui->menu_bar_items.push_back(std::move(help));
  }

  void Editor::select_entity(Entity::EntityId entity)
  {
    state.selectedEntity = entity;
    show_plugin("properties");
  }

  void Editor::configure_default_dock_layout(std::uint32_t dockspaceId)
  {
    if (dockLayoutInitialized || dockspaceId == 0)
    {
      return;
    }

    dockLayoutInitialized = true;

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID mainDockId = dockspaceId;
    ImGuiID workspaceDockId = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Left, 0.22f, nullptr, &mainDockId);
    const ImGuiID entitiesDockId = ImGui::DockBuilderSplitNode(workspaceDockId, ImGuiDir_Down, 0.56f, nullptr, &workspaceDockId);
    const ImGuiID inspectorDockId = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Right, 0.34f, nullptr, &mainDockId);
    const ImGuiID consoleDockId = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Down, 0.25f, nullptr, &mainDockId);
    ImGui::DockBuilderDockWindow(WORKSPACE_WINDOW_TITLE, workspaceDockId);
    ImGui::DockBuilderDockWindow(ENTITY_WINDOW_TITLE, entitiesDockId);
    ImGui::DockBuilderDockWindow(PROPERTIES_WINDOW_TITLE, inspectorDockId);
    ImGui::DockBuilderDockWindow(SCENE_WINDOW_TITLE, mainDockId);
    ImGui::DockBuilderDockWindow("Debug Console", consoleDockId);
    ImGui::DockBuilderFinish(dockspaceId);
  }

  void Editor::save_worlds(
      EntityManager &entityManager,
      ComponentManager &componentManager)
  {
    if (activeWorkspacePath_.empty())
    {
      return;
    }

    std::string error;
    if (!hades::save_all_worlds(activeWorkspacePath_, entityManager, componentManager, &error))
    {
      log_error("Failed to save worlds: " + error);
      return;
    }

    cachedDiskWorlds_ = list_saved_worlds(activeWorkspacePath_);

    if (!save_workspace_settings(activeWorkspacePath_, &error))
    {
      log_error("Failed to save workspace settings: " + error);
    }
  }

  void Editor::restore_saved_worlds_if_needed(
      EntityManager &entityManager,
      ComponentManager &componentManager)
  {
    if (!pendingSavedWorldRestore_ || activeWorkspacePath_.empty())
    {
      return;
    }

    pendingSavedWorldRestore_ = false;

    if (!find_world_entities(entityManager, componentManager).empty())
    {
      return;
    }

    std::string error;
    const auto loadedWorlds = hades::load_all_worlds(activeWorkspacePath_, entityManager, componentManager, &error);
    if (!error.empty())
    {
      if (loadedWorlds.empty())
      {
        log_error(error);
      }
      else
      {
        log_warning(error);
      }
    }

    if (loadedWorlds.empty())
    {
      return;
    }

    if (const auto defaultWorld = normalize_default_world(entityManager, componentManager); defaultWorld.has_value())
    {
      load_world(*defaultWorld, componentManager);
      return;
    }

    load_world(loadedWorlds.front(), componentManager);
  }

  void Editor::open_world_from_disk(
      const std::string &worldName,
      EntityManager &entityManager,
      ComponentManager &componentManager)
  {
    if (activeWorkspacePath_.empty())
    {
      return;
    }

    const auto filePath = activeWorkspacePath_ / ".hades" / "worlds" / (worldName + ".json");

    // Destroy any existing in-memory world with the same name.
    for (Entity::EntityId entity : entityManager.getAllEntities())
    {
      if (!componentManager.hasComponent<WorldComponent>(entity))
      {
        continue;
      }
      if (!componentManager.hasComponent<NameComponent>(entity))
      {
        continue;
      }
      if (componentManager.getComponent<NameComponent>(entity).value == worldName)
      {
        if (state.loadedWorld.has_value() && *state.loadedWorld == entity)
        {
          state.loadedWorld.reset();
          state.activeCamera.reset();
        }
        if (state.selectedEntity.has_value())
        {
          state.selectedEntity.reset();
        }
        destroy_world_tree(entity, entityManager, componentManager);
        break;
      }
    }

    std::string error;
    auto worldEntity = hades::load_world_from_file(filePath, entityManager, componentManager, &error);
    if (!worldEntity.has_value())
    {
      log_error("Failed to load world '" + worldName + "': " + error);
      return;
    }

    load_world(*worldEntity, componentManager);
  }

  bool Editor::should_expose_plugin_setting(const EditorPlugin &plugin) const
  {
    return plugin.listed_in_menu() &&
        plugin.id() != "settings" &&
        plugin.id() != "script-editor-window" &&
        plugin.id() != "debug-console";
  }

  WorkspaceEditorSettings Editor::capture_workspace_settings() const
  {
    WorkspaceEditorSettings settings;
    settings.showDebugInfo = state.showDebugInfo;
    settings.sceneCameraTargetX = sceneCameraTargetX_;
    settings.sceneCameraTargetY = sceneCameraTargetY_;
    settings.sceneCameraTargetZ = sceneCameraTargetZ_;
    settings.sceneCameraDistance = sceneCameraDistance_;
    settings.sceneCameraYawDegrees = sceneCameraYawDegrees_;
    settings.sceneCameraPitchDegrees = sceneCameraPitchDegrees_;

    for (const auto &plugin : plugins_)
    {
      if (!should_expose_plugin_setting(*plugin))
      {
        continue;
      }

      settings.pluginVisibility.emplace(std::string(plugin->id()), plugin->visible(*this));
    }

    return settings;
  }

  void Editor::apply_workspace_settings(const WorkspaceEditorSettings &settings)
  {
    state.showDebugInfo = settings.showDebugInfo;
    sceneCameraTargetX_ = settings.sceneCameraTargetX;
    sceneCameraTargetY_ = settings.sceneCameraTargetY;
    sceneCameraTargetZ_ = settings.sceneCameraTargetZ;
    sceneCameraDistance_ = settings.sceneCameraDistance;
    sceneCameraYawDegrees_ = settings.sceneCameraYawDegrees;
    sceneCameraPitchDegrees_ = settings.sceneCameraPitchDegrees;

    for (const auto &plugin : plugins_)
    {
      if (!should_expose_plugin_setting(*plugin))
      {
        continue;
      }

      const auto visibleIt = settings.pluginVisibility.find(std::string(plugin->id()));
      if (visibleIt == settings.pluginVisibility.end())
      {
        continue;
      }

      plugin->set_visible(*this, visibleIt->second);
    }
  }
}
