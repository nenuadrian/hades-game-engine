#ifndef HADES_EDITOR_EDITOR_HPP
#define HADES_EDITOR_EDITOR_HPP

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "types.h"
#include "debug_console_panel.hpp"
#include "editor_settings.hpp"
#include "external_editor.hpp"
#include "script_analysis.hpp"
#include "plugins/editor_plugin.hpp"
#include "../engine/core/ecs/entity.hpp"
#include "../engine/rendering/render_types.hpp"
#include "../engine/rendering/scene_renderer.hpp"

namespace hades
{
  class ComponentManager;
  class EntityManager;
  class GUI;
  class ScriptRuntime;

  class Editor
  {
  public:
    enum class WorkspaceCreateKind
    {
      None,
      Folder,
      Script,
    };

    enum class SettingsCategory
    {
      Editor,
      GamePreview,
      Plugins,
    };

    struct WorkspaceTreeNode
    {
      std::filesystem::path path;
      bool directory = false;
      std::vector<WorkspaceTreeNode> children;
    };

    enum class ExportPlatform
    {
      macOS,
      Linux,
      Windows,
      Web,
    };

    struct ExportPlatformSettings
    {
      std::array<char, 512> outputPathBuffer{};
      std::array<char, 256> projectNameBuffer{};
      bool enableHeadless = false;
      bool enableHadesAPI = false;
    };

    struct ExportBuildState
    {
      std::mutex mutex;
      std::string log;
      std::string error;
      bool finished = false;
      bool succeeded = false;
    };

    EditorState state;
    std::unique_ptr<GUI> gui;

    Editor();
    ~Editor();

    void reset_workspace_session();

    void render(
        float deltaTime,
        const std::filesystem::path &workspacePath,
        EntityManager &entityManager,
        ComponentManager &componentManager,
        ScriptRuntime &scriptRuntime);

    void show_plugin(std::string_view pluginId);
    bool is_plugin_visible(std::string_view pluginId) const;
    void log_message(DebugMessageLevel level, const std::string &text);
    void log_info(const std::string &text);
    void log_warning(const std::string &text);
    void log_error(const std::string &text);
    bool game_preview_hades_api_enabled() const;
    bool load_workspace_settings(const std::filesystem::path &workspacePath, std::string *errorMessage = nullptr);
    bool save_workspace_settings(const std::filesystem::path &workspacePath, std::string *errorMessage = nullptr) const;
    void stop_play_mode(EntityManager &entityManager, ComponentManager &componentManager, ScriptRuntime &scriptRuntime);

  private:
    bool dockLayoutInitialized = false;
    std::filesystem::path activeWorkspacePath_;
    std::optional<WorkspaceTreeNode> workspaceTreeRoot_;
    std::vector<std::string> workspaceScriptFiles_;
    std::string workspaceScanError_;
    std::vector<std::string> cachedDiskWorlds_;
    bool workspaceScriptListDirty_ = false;
    bool openWorkspaceCreateDialog_ = false;
    WorkspaceCreateKind pendingWorkspaceCreateKind_ = WorkspaceCreateKind::None;
    std::filesystem::path pendingWorkspaceCreateParentPath_;
    std::array<char, 256> workspaceCreateNameBuffer_{};
    std::string workspaceCreateError_;
    bool openWorkspaceImportDialog_ = false;
    std::filesystem::path pendingWorkspaceImportParentPath_;
    std::array<char, 512> workspaceImportSourcePathBuffer_{};
    std::string workspaceImportError_;
    bool openWorkspaceDeleteDialog_ = false;
    bool openSettingsWindow_ = false;
    bool focusSettingsWindow_ = false;
    SettingsCategory selectedSettingsCategory_ = SettingsCategory::Editor;
    bool gamePreviewEnableHadesAPI_ = false;
    bool openDebugConsoleWindow_ = false;
    bool focusDebugConsoleWindow_ = false;
    DebugConsolePanel mainDebugConsole_{500};
    bool openAboutWindow_ = false;
    bool focusAboutWindow_ = false;

    // Export window state.
    bool openExportWindow_ = false;
    bool focusExportWindow_ = false;
#ifdef __APPLE__
    ExportPlatform selectedExportPlatform_ = ExportPlatform::macOS;
#elif defined(__linux__)
    ExportPlatform selectedExportPlatform_ = ExportPlatform::Linux;
#else
    ExportPlatform selectedExportPlatform_ = ExportPlatform::Windows;
#endif
  std::array<ExportPlatformSettings, 4> exportPlatformSettings_{};
    bool exportBuildInProgress_ = false;
    std::shared_ptr<ExportBuildState> exportBuildState_;
    std::thread exportBuildThread_;
    std::string exportBuildLog_;
    std::vector<char> exportBuildLogBuffer_;
    std::string exportBuildError_;
    bool exportBuildSucceeded_ = false;
    bool exportBuildFinished_ = false;

    std::filesystem::path pendingWorkspaceDeletePath_;
    std::string workspaceDeleteError_;

    // Workspace filter state.
    std::array<char, 256> workspaceFilterBuffer_{};

    // Workspace grid navigation state.
    std::filesystem::path workspaceGridCurrentDir_;

    // Workspace inline rename state.
    std::filesystem::path workspaceRenamePath_;
    std::array<char, 256> workspaceRenameBuffer_{};
    bool workspaceRenameFocusPending_ = false;

    // Play-mode state snapshot for restoring ECS state on stop.
    nlohmann::json playModeSnapshot_;
    std::optional<Entity::EntityId> prePlaySelectedEntity_;
    std::optional<Entity::EntityId> prePlayLoadedWorld_;

    std::optional<Entity::EntityId> pendingEntityDeletion_;
    bool openAddEntityDialog_ = false;
    bool focusAddEntitySearch_ = false;
    std::optional<Entity::EntityId> pendingAddEntityParent_;
    std::array<char, 256> addEntitySearchBuffer_{};
    float sceneCameraTargetX_ = 0.0f;
    float sceneCameraTargetY_ = 0.0f;
    float sceneCameraTargetZ_ = 0.0f;
    float sceneCameraDistance_ = 1.0f;
    float sceneCameraYawDegrees_ = 0.0f;
    float sceneCameraPitchDegrees_ = 0.0f;
    SceneGizmoMode sceneGizmoMode_ = SceneGizmoMode::Translate;
    SceneGizmoAxis activeSceneGizmoAxis_ = SceneGizmoAxis::None;
    Entity::EntityId activeSceneGizmoEntity_ = Entity::INVALID;
    float sceneGizmoDragStartMouseX_ = 0.0f;
    float sceneGizmoDragStartMouseY_ = 0.0f;
    float sceneGizmoDragStartPositionX_ = 0.0f;
    float sceneGizmoDragStartPositionY_ = 0.0f;
    float sceneGizmoDragStartPositionZ_ = 0.0f;
    float sceneGizmoAxisScreenDirectionX_ = 0.0f;
    float sceneGizmoAxisScreenDirectionY_ = 0.0f;
    float sceneGizmoPixelsPerWorldUnit_ = 1.0f;
    float sceneGizmoDragStartRotationQx_ = 0.0f;
    float sceneGizmoDragStartRotationQy_ = 0.0f;
    float sceneGizmoDragStartRotationQz_ = 0.0f;
    float sceneGizmoDragStartRotationQw_ = 1.0f;
    float sceneGizmoDragStartScaleX_ = 1.0f;
    float sceneGizmoDragStartScaleY_ = 1.0f;
    float sceneGizmoDragStartScaleZ_ = 1.0f;
    bool sceneCanvasKeyboardCapture_ = false;
    bool pendingSavedWorldRestore_ = false;

    // External editor preference for "Open in External Editor".
    ExternalEditor externalEditor_ = ExternalEditor::VSCode;

    // Render pipeline integration.
    SceneRenderer sceneRenderer_;
    RenderList sceneRenderList_;

    // Parsed script class cache (keyed by resolved script path).
    std::unordered_map<std::string, std::vector<ParsedScriptClass>> parsedScriptCache_;
    std::unordered_map<std::string, std::filesystem::file_time_type> parsedScriptModTimes_;
    std::vector<std::unique_ptr<EditorPlugin>> plugins_;
    std::deque<float> debugFrameTimeHistory_;
    double debugFrameTimeHistoryTotal_ = 0.0;

    void register_builtin_plugins();
    void register_plugin(std::unique_ptr<EditorPlugin> plugin);
    EditorPlugin *find_plugin(std::string_view pluginId);
    const EditorPlugin *find_plugin(std::string_view pluginId) const;
    bool should_expose_plugin_setting(const EditorPlugin &plugin) const;
    WorkspaceEditorSettings capture_workspace_settings() const;
    void apply_workspace_settings(const WorkspaceEditorSettings &settings);
    void render_plugins(EditorPluginPhase phase, EditorPluginContext &context);
    void sync_menu_bar(EntityManager &entityManager, ComponentManager &componentManager);
    void configure_default_dock_layout(std::uint32_t dockspaceId);
    void refresh_workspace_cache(const std::filesystem::path &workspacePath);
    void invalidate_workspace_cache();
    void request_workspace_item_creation(WorkspaceCreateKind kind, const std::filesystem::path &parentPath);
    void request_workspace_item_import(const std::filesystem::path &destinationDirectory);
    void request_workspace_item_deletion(const std::filesystem::path &targetPath);
    void render_workspace_create_dialog();
    void render_workspace_import_dialog();
    void render_workspace_delete_dialog(EntityManager &entityManager, ComponentManager &componentManager);
    void render_workspace_dialogs(EntityManager &entityManager, ComponentManager &componentManager);
    void render_settings_window();
    void render_debug_console_window();
    void render_about_window();
    void render_export_window(EntityManager &entityManager, ComponentManager &componentManager);
    void render_workspace_grid_cell(const WorkspaceTreeNode &node, const char *filter);
    void render_workspace_create_menu(const std::filesystem::path &destination);
    void reset_scene_camera();
    void restore_saved_worlds_if_needed(EntityManager &entityManager, ComponentManager &componentManager);
    void ensure_world_state(EntityManager &entityManager, ComponentManager &componentManager);
    Entity::EntityId create_world(EntityManager &entityManager, ComponentManager &componentManager);
    void load_world(Entity::EntityId world, ComponentManager &componentManager);
    void set_default_world(Entity::EntityId world, EntityManager &entityManager, ComponentManager &componentManager);
    void request_entity_creation(EditorEntityPreset preset, Entity::EntityId parent);
    void request_add_entity_picker(Entity::EntityId parent);
    void request_entity_deletion(Entity::EntityId entity);
    void select_entity(Entity::EntityId entity);
    void workspace(EntityManager &entityManager, ComponentManager &componentManager);
    void handle_entity_creation_requests(EntityManager &entityManager, ComponentManager &componentManager);
    void handle_entity_deletion_requests(EntityManager &entityManager, ComponentManager &componentManager, ScriptRuntime &scriptRuntime);
    void render_add_entity_dialog(EntityManager &entityManager, ComponentManager &componentManager);
    void handle_play_mode_requests(EntityManager &entityManager, ComponentManager &componentManager, ScriptRuntime &scriptRuntime);
    void start_play_mode(EntityManager &entityManager, ComponentManager &componentManager, ScriptRuntime &scriptRuntime);
    void set_main_camera(Entity::EntityId entity, EntityManager &entityManager, ComponentManager &componentManager);
    std::optional<Entity::EntityId> get_selected_parent(EntityManager &entityManager, ComponentManager &componentManager) const;
    std::string entity_label(Entity::EntityId entity, ComponentManager &componentManager) const;
    void entities(EntityManager &entityManager, ComponentManager &componentManager);
    void scene(EntityManager &entityManager, ComponentManager &componentManager);
    void properties(EntityManager &entityManager, ComponentManager &componentManager);
    void render_hierarchy(Entity::EntityId entity, EntityManager &entityManager, ComponentManager &componentManager);
    void render_hierarchies(EntityManager &entityManager, ComponentManager &componentManager);
    void debug(float deltaTime, EntityManager &entityManager, ComponentManager &componentManager, ScriptRuntime &scriptRuntime);
    void save_worlds(EntityManager &entityManager, ComponentManager &componentManager);
    void open_world_from_disk(const std::string &worldName, EntityManager &entityManager, ComponentManager &componentManager);
  };

}

#endif
