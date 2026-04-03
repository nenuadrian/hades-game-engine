#ifndef HADES_EDITOR_EDITOR_HPP
#define HADES_EDITOR_EDITOR_HPP

#include <array>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string_view>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "types.h"
#include "script_analysis.hpp"
#include "plugins/editor_plugin.hpp"
#include "../engine/core/ecs/entity.hpp"
#include "TextEditor.h"

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

    struct WorkspaceTreeNode
    {
      std::filesystem::path path;
      bool directory = false;
      std::vector<WorkspaceTreeNode> children;
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
    bool is_script_editor_window_open() const;
    void set_script_editor_window_open(bool open);
    bool consume_script_editor_focus_request();
    void render_script_editor_window();
    void log_message(DebugMessageLevel level, const std::string &text);
    void log_info(const std::string &text);
    void log_warning(const std::string &text);
    void log_error(const std::string &text);

  private:
    enum class ScriptCompileStatus
    {
      Unknown,
      Succeeded,
      Failed,
    };

    struct BackgroundCompileTaskResult
    {
      std::uint64_t requestId = 0;
      std::string error;
    };

    struct ScriptEditorTab
    {
      std::filesystem::path path;
      std::string relativePath;
      std::string contents;
      std::string savedContents;
      std::unique_ptr<TextEditor> textEditor;
      std::optional<std::filesystem::file_time_type> savedWriteTime;
      bool dirty = false;
    };

    bool dockLayoutInitialized = false;
    bool openImportModelDialog = false;
    std::array<char, 512> importModelPathBuffer{};
    std::string importModelError;
    std::filesystem::path activeWorkspacePath_;
    std::optional<WorkspaceTreeNode> workspaceTreeRoot_;
    std::vector<std::string> workspaceScriptFiles_;
    std::string workspaceScanError_;
    std::vector<ScriptEditorTab> openScriptEditorTabs_;
    std::optional<std::size_t> activeScriptEditorTabIndex_;
    std::string scriptEditorStatusMessage_;
    bool scriptEditorStatusIsError_ = false;
    bool openScriptEditorWindow_ = false;
    bool focusScriptEditorWindow_ = false;
    bool openScriptEditorUnsavedChangesDialog_ = false;
    std::optional<std::filesystem::path> pendingScriptEditorClosePath_;
    double nextWorkspaceScanTime_ = 0.0;
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
    bool openDebugConsoleWindow_ = false;
    bool focusDebugConsoleWindow_ = false;
    std::filesystem::path pendingWorkspaceDeletePath_;
    std::string workspaceDeleteError_;
    std::optional<Entity::EntityId> pendingEntityDeletion_;
    float sceneCameraTargetX_ = 0.0f;
    float sceneCameraTargetY_ = 0.0f;
    float sceneCameraTargetZ_ = 0.0f;
    float sceneCameraDistance_ = 1.0f;
    float sceneCameraYawDegrees_ = 0.0f;
    float sceneCameraPitchDegrees_ = 0.0f;
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
    bool pendingSavedWorldRestore_ = false;

    // File watch / background compile state.
    std::unordered_map<std::string, std::filesystem::file_time_type> scriptModTimes_;
    std::future<BackgroundCompileTaskResult> backgroundCompileResult_;
    bool backgroundCompileInProgress_ = false;
    std::string lastCompileError_;
    ScriptCompileStatus scriptCompileStatus_ = ScriptCompileStatus::Unknown;
    std::uint64_t currentCompileRequestId_ = 0;
    std::uint64_t nextCompileRequestId_ = 0;

    // Parsed script class cache (keyed by resolved script path).
    std::unordered_map<std::string, std::vector<ParsedScriptClass>> parsedScriptCache_;
    std::unordered_map<std::string, std::filesystem::file_time_type> parsedScriptModTimes_;
    std::vector<std::unique_ptr<EditorPlugin>> plugins_;

    void register_builtin_plugins();
    void register_plugin(std::unique_ptr<EditorPlugin> plugin);
    EditorPlugin *find_plugin(std::string_view pluginId);
    const EditorPlugin *find_plugin(std::string_view pluginId) const;
    void render_plugins(EditorPluginPhase phase, EditorPluginContext &context);
    void sync_menu_bar(EntityManager &entityManager, ComponentManager &componentManager);
    void configure_default_dock_layout(std::uint32_t dockspaceId);
    void refresh_workspace_cache(float deltaTime, const std::filesystem::path &workspacePath);
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
    void render_workspace_tree_node(const WorkspaceTreeNode &node);
    void render_script_editor();
    std::optional<std::size_t> find_script_editor_tab_index(const std::filesystem::path &scriptPath) const;
    ScriptEditorTab *active_script_editor_tab();
    const ScriptEditorTab *active_script_editor_tab() const;
    void activate_script_editor_tab(std::size_t index);
    void close_script_editor_tab(std::size_t index);
    void request_script_editor_open(const std::filesystem::path &scriptPath, const std::string &relativePath);
    bool open_script_document(const std::filesystem::path &scriptPath, const std::string &relativePath, std::string *errorMessage = nullptr);
    bool save_active_script_document(bool triggerCompile, std::string *errorMessage = nullptr);
    bool save_script_document_at_index(std::size_t index, bool triggerCompile, std::string *errorMessage = nullptr);
    bool save_all_script_documents(bool triggerCompile, std::string *errorMessage = nullptr);
    void queue_workspace_script_compile();
    void reset_scene_camera();
    void restore_saved_worlds_if_needed(EntityManager &entityManager, ComponentManager &componentManager);
    void ensure_world_state(EntityManager &entityManager, ComponentManager &componentManager);
    Entity::EntityId create_world(EntityManager &entityManager, ComponentManager &componentManager);
    void load_world(Entity::EntityId world, ComponentManager &componentManager);
    void set_default_world(Entity::EntityId world, EntityManager &entityManager, ComponentManager &componentManager);
    void request_entity_creation(EditorEntityPreset preset, Entity::EntityId parent);
    void request_model_import(Entity::EntityId parent);
    void request_entity_deletion(Entity::EntityId entity);
    void workspace(EntityManager &entityManager, ComponentManager &componentManager);
    void handle_entity_creation_requests(EntityManager &entityManager, ComponentManager &componentManager);
    void handle_entity_deletion_requests(EntityManager &entityManager, ComponentManager &componentManager, ScriptRuntime &scriptRuntime);
    void import_model(EntityManager &entityManager, ComponentManager &componentManager);
    void handle_play_mode_requests(EntityManager &entityManager, ComponentManager &componentManager, ScriptRuntime &scriptRuntime);
    void start_play_mode(EntityManager &entityManager, ComponentManager &componentManager, ScriptRuntime &scriptRuntime);
    void stop_play_mode(ScriptRuntime &scriptRuntime);
    void set_main_camera(Entity::EntityId entity, EntityManager &entityManager, ComponentManager &componentManager);
    std::optional<Entity::EntityId> get_selected_parent(EntityManager &entityManager, ComponentManager &componentManager) const;
    std::string entity_label(Entity::EntityId entity, ComponentManager &componentManager) const;
    void entities(EntityManager &entityManager, ComponentManager &componentManager);
    void scene(EntityManager &entityManager, ComponentManager &componentManager);
    void properties(EntityManager &entityManager, ComponentManager &componentManager);
    void components(EntityManager &entityManager, ComponentManager &componentManager);
    void render_hierarchy(Entity::EntityId entity, EntityManager &entityManager, ComponentManager &componentManager);
    void render_hierarchies(EntityManager &entityManager, ComponentManager &componentManager);
    void debug(float deltaTime);
    void save_worlds(EntityManager &entityManager, ComponentManager &componentManager);
    void open_world_from_disk(const std::string &worldName, EntityManager &entityManager, ComponentManager &componentManager);
  };

}

#endif
