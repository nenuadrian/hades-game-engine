#include "editor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>

#include "imgui.h"
#include "imgui_internal.h"
#include "../engine/components/name_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/core/ecs/scene_serializer.hpp"
#include "../engine/core/ecs/world_utils.hpp"
#include "../engine/gui/imgui.hpp"
#include "../engine/runtime/script_runtime.hpp"

namespace hades
{
  namespace
  {
    constexpr char ENTITY_WINDOW_TITLE[] = "Entities";
    constexpr char WORKSPACE_WINDOW_TITLE[] = "Workspace";
    constexpr char PROPERTIES_WINDOW_TITLE[] = "Properties";
    constexpr char COMPONENTS_WINDOW_TITLE[] = "Components";
    constexpr char SCENE_WINDOW_TITLE[] = "World";
  }

  Editor::Editor() : gui(std::make_unique<ImGui_GUI>())
  {
    reset_scene_camera();
    register_builtin_plugins();
  }

  Editor::~Editor() = default;

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

    activeWorkspacePath_.clear();
    workspaceTreeRoot_.reset();
    workspaceScriptFiles_.clear();
    workspaceScanError_.clear();
    openScriptEditorTabs_.clear();
    activeScriptEditorTabIndex_.reset();
    scriptEditorStatusMessage_.clear();
    scriptEditorStatusIsError_ = false;
    openScriptEditorWindow_ = false;
    focusScriptEditorWindow_ = false;
    openScriptEditorUnsavedChangesDialog_ = false;
    pendingScriptEditorClosePath_.reset();
    nextWorkspaceScanTime_ = 0.0;
    workspaceScriptListDirty_ = false;
    scriptModTimes_.clear();
    parsedFieldsCache_.clear();
    parsedFieldsModTimes_.clear();
    lastCompileError_.clear();
    scriptCompileStatus_ = ScriptCompileStatus::Unknown;
    backgroundCompileInProgress_ = false;
    currentCompileRequestId_ = 0;
    nextCompileRequestId_ = 0;
    state.debugConsoleMessages.clear();
    openDebugConsoleWindow_ = false;
    focusDebugConsoleWindow_ = false;
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

    state.debugConsoleMessages.push_front(
        DebugMessage{level, text, std::chrono::steady_clock::now()});

    constexpr std::size_t MAX_DEBUG_MESSAGES = 500;
    while (state.debugConsoleMessages.size() > MAX_DEBUG_MESSAGES)
    {
      state.debugConsoleMessages.pop_back();
    }

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

  void Editor::render(
      float deltaTime,
      const std::filesystem::path &workspacePath,
      EntityManager &entityManager,
      ComponentManager &componentManager,
      ScriptRuntime &scriptRuntime)
  {
    refresh_workspace_cache(deltaTime, workspacePath);
    ensure_world_state(entityManager, componentManager);
    sync_menu_bar(entityManager, componentManager);
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

    bool scriptsChanged = workspaceScriptListDirty_;
    if (!activeWorkspacePath_.empty())
    {
      for (const auto &relPath : workspaceScriptFiles_)
      {
        const auto fullPath = activeWorkspacePath_ / relPath;
        std::error_code ec;
        const auto modTime = std::filesystem::last_write_time(fullPath, ec);
        if (ec)
        {
          continue;
        }
        auto it = scriptModTimes_.find(relPath);
        if (it == scriptModTimes_.end())
        {
          scriptModTimes_[relPath] = modTime;
        }
        else if (it->second != modTime)
        {
          it->second = modTime;
          scriptsChanged = true;
        }
      }

      if (scriptsChanged)
      {
        queue_workspace_script_compile();
      }
    }

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
    render_plugins(EditorPluginPhase::PreEntityDeletion, pluginContext);
    handle_entity_deletion_requests(entityManager, componentManager, scriptRuntime);
    render_plugins(EditorPluginPhase::PostEntityDeletion, pluginContext);
    render_workspace_dialogs(entityManager, componentManager);
  }

  void Editor::sync_menu_bar(EntityManager &entityManager, ComponentManager &componentManager)
  {
    gui->menu_bar_items.clear();

    MenuBarItem file;
    file.title = "File";

    MenuBarItem newWorld;
    newWorld.title = "New World";
    newWorld.on_activate = [this, &entityManager, &componentManager]()
    {
      create_world(entityManager, componentManager);
    };

    MenuBarItem exit;
    exit.title = "Exit";
    exit.on_activate = [this]()
    {
      state.events.push(EDITOR_QUIT);
    };

    MenuBarItem save;
    save.title = "Save";
    save.on_activate = [this, &entityManager, &componentManager]()
    {
      save_worlds(entityManager, componentManager);
    };

    file.children_menu_items.push_back(newWorld);
    file.children_menu_items.push_back(save);
    file.children_menu_items.push_back(exit);
    gui->menu_bar_items.push_back(file);

    MenuBarItem worlds;
    worlds.title = "Worlds";

    // Collect in-memory world names so we can mark them as loaded.
    const auto memoryWorlds = find_world_entities(entityManager, componentManager);
    std::unordered_map<std::string, Entity::EntityId> memoryWorldsByName;
    for (Entity::EntityId world : memoryWorlds)
    {
      memoryWorldsByName[entity_label(world, componentManager)] = world;
    }

    // List worlds saved on disk.
    const auto diskWorlds = list_saved_worlds(activeWorkspacePath_);

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
      emptyWorlds.title = "No Worlds Available";
      worlds.children_menu_items.push_back(emptyWorlds);
    }
    else
    {
      const bool hasDiskWorlds = !diskWorlds.empty();
      for (const auto &worldName : allWorldNames)
      {
        MenuBarItem worldItem;
        worldItem.title = worldName;

        auto memIt = memoryWorldsByName.find(worldName);
        const bool inMemory = memIt != memoryWorldsByName.end();
        const bool onDisk = hasDiskWorlds &&
                            std::find(diskWorlds.begin(), diskWorlds.end(), worldName) != diskWorlds.end();

        if (inMemory)
        {
          worldItem.selected = state.loadedWorld.has_value() && *state.loadedWorld == memIt->second;
        }

        if (onDisk)
        {
          // Always load from disk when selected.
          worldItem.on_activate = [this, worldName, &entityManager, &componentManager]()
          {
            open_world_from_disk(worldName, entityManager, componentManager);
          };
        }
        else if (inMemory)
        {
          // Unsaved world: just switch to it.
          Entity::EntityId worldId = memIt->second;
          worldItem.on_activate = [this, &componentManager, worldId]()
          {
            load_world(worldId, componentManager);
          };
        }

        worlds.children_menu_items.push_back(std::move(worldItem));
      }
    }

    gui->menu_bar_items.push_back(worlds);

    MenuBarItem game;
    game.title = "Game";

    MenuBarItem play;
    play.title = "Play";
    play.on_activate = [this]()
    {
      state.pendingPlayAction = EditorPlayAction::Start;
    };

    MenuBarItem stop;
    stop.title = "Stop";
    stop.on_activate = [this]()
    {
      state.pendingPlayAction = EditorPlayAction::Stop;
    };

    game.children_menu_items.push_back(play);
    game.children_menu_items.push_back(stop);
    gui->menu_bar_items.push_back(game);

    MenuBarItem windowsMenu;
    windowsMenu.title = "Windows";

    MenuBarItem editorWindow;
    editorWindow.title = "Editor";
    editorWindow.selected = is_script_editor_window_open();
    editorWindow.on_activate = [this]()
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
    windowsMenu.children_menu_items.push_back(std::move(editorWindow));

    MenuBarItem settingsWindow;
    settingsWindow.title = "Settings";
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
    windowsMenu.children_menu_items.push_back(std::move(settingsWindow));

    MenuBarItem debugConsoleWindow;
    debugConsoleWindow.title = "Debug Console";
    debugConsoleWindow.selected = is_plugin_visible("debug-console");
    debugConsoleWindow.on_activate = [this]()
    {
      if (is_plugin_visible("debug-console"))
      {
        if (EditorPlugin *plugin = find_plugin("debug-console"))
        {
          plugin->set_visible(*this, false);
        }
      }
      else
      {
        show_plugin("debug-console");
      }
    };
    windowsMenu.children_menu_items.push_back(std::move(debugConsoleWindow));
    gui->menu_bar_items.push_back(std::move(windowsMenu));

    MenuBarItem pluginsMenu;
    pluginsMenu.title = "Plugins";

    for (const auto &plugin : plugins_)
    {
      if (!plugin->listed_in_menu() ||
          plugin->id() == "settings" ||
          plugin->id() == "script-editor-window" ||
          plugin->id() == "debug-console")
      {
        continue;
      }

      MenuBarItem pluginItem;
      pluginItem.title = std::string(plugin->display_name());
      pluginItem.selected = plugin->visible(*this);
      pluginItem.on_activate = [this, plugin = plugin.get()]()
      {
        if (plugin->visible(*this))
        {
          plugin->set_visible(*this, false);
        }
        else
        {
          plugin->activate(*this);
        }
      };
      pluginsMenu.children_menu_items.push_back(std::move(pluginItem));
    }

    if (pluginsMenu.children_menu_items.empty())
    {
      MenuBarItem emptyPlugins;
      emptyPlugins.title = "No Plugins Registered";
      emptyPlugins.enabled = false;
      pluginsMenu.children_menu_items.push_back(std::move(emptyPlugins));
    }

    gui->menu_bar_items.push_back(std::move(pluginsMenu));
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
    ImGuiID inspectorDockId = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Right, 0.34f, nullptr, &mainDockId);
    const ImGuiID componentsDockId = ImGui::DockBuilderSplitNode(inspectorDockId, ImGuiDir_Right, 0.45f, nullptr, &inspectorDockId);
    const ImGuiID consoleDockId = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Down, 0.25f, nullptr, &mainDockId);
    ImGui::DockBuilderDockWindow(WORKSPACE_WINDOW_TITLE, workspaceDockId);
    ImGui::DockBuilderDockWindow(ENTITY_WINDOW_TITLE, entitiesDockId);
    ImGui::DockBuilderDockWindow(PROPERTIES_WINDOW_TITLE, inspectorDockId);
    ImGui::DockBuilderDockWindow(COMPONENTS_WINDOW_TITLE, componentsDockId);
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
    }
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
}
