#include "editor.hpp"

#include <chrono>
#include <memory>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
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
    lastCompileSucceeded_ = true;
    backgroundCompileInProgress_ = false;
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
          scriptsChanged = true;
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

    if (backgroundCompileInProgress_ && backgroundCompileResult_.valid() &&
        backgroundCompileResult_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
      lastCompileError_ = backgroundCompileResult_.get();
      lastCompileSucceeded_ = lastCompileError_.empty();
      backgroundCompileInProgress_ = false;
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

    file.children_menu_items.push_back(newWorld);
    file.children_menu_items.push_back(exit);
    gui->menu_bar_items.push_back(file);

    MenuBarItem worlds;
    worlds.title = "Worlds";

    const auto worldEntities = find_world_entities(entityManager, componentManager);
    if (worldEntities.empty())
    {
      MenuBarItem emptyWorlds;
      emptyWorlds.title = "No Worlds Available";
      worlds.children_menu_items.push_back(emptyWorlds);
    }
    else
    {
      for (const Entity::EntityId world : worldEntities)
      {
        MenuBarItem worldItem;
        worldItem.title = entity_label(world, componentManager);
        worldItem.selected = state.loadedWorld.has_value() && *state.loadedWorld == world;
        worldItem.on_activate = [this, &componentManager, world]()
        {
          load_world(world, componentManager);
        };
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
    gui->menu_bar_items.push_back(std::move(windowsMenu));

    MenuBarItem pluginsMenu;
    pluginsMenu.title = "Plugins";

    for (const auto &plugin : plugins_)
    {
      if (!plugin->listed_in_menu() ||
          plugin->id() == "settings" ||
          plugin->id() == "script-editor-window")
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
    ImGui::DockBuilderDockWindow(WORKSPACE_WINDOW_TITLE, workspaceDockId);
    ImGui::DockBuilderDockWindow(ENTITY_WINDOW_TITLE, entitiesDockId);
    ImGui::DockBuilderDockWindow(PROPERTIES_WINDOW_TITLE, inspectorDockId);
    ImGui::DockBuilderDockWindow(COMPONENTS_WINDOW_TITLE, componentsDockId);
    ImGui::DockBuilderDockWindow(SCENE_WINDOW_TITLE, mainDockId);
    ImGui::DockBuilderFinish(dockspaceId);
  }
}
