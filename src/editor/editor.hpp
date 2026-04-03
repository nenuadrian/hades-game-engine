#ifndef HADES_EDITOR_EDITOR_HPP
#define HADES_EDITOR_EDITOR_HPP

#include <array>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "types.h"
#include "../engine/core/ecs/entity.hpp"

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

  private:
    bool dockLayoutInitialized = false;
    bool openImportModelDialog = false;
    std::array<char, 512> importModelPathBuffer{};
    std::string importModelError;
    std::filesystem::path activeWorkspacePath_;
    std::optional<WorkspaceTreeNode> workspaceTreeRoot_;
    std::vector<std::string> workspaceScriptFiles_;
    std::string workspaceScanError_;
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
    std::filesystem::path pendingWorkspaceDeletePath_;
    std::string workspaceDeleteError_;
    std::optional<Entity::EntityId> pendingEntityDeletion_;
    float sceneCameraTargetX_ = 0.0f;
    float sceneCameraTargetY_ = 0.0f;
    float sceneCameraTargetZ_ = 0.0f;
    float sceneCameraDistance_ = 1.0f;
    float sceneCameraYawDegrees_ = 0.0f;
    float sceneCameraPitchDegrees_ = 0.0f;

    // File watch / background compile state.
    std::unordered_map<std::string, std::filesystem::file_time_type> scriptModTimes_;
    std::future<std::string> backgroundCompileResult_;
    bool backgroundCompileInProgress_ = false;
    std::string lastCompileError_;
    bool lastCompileSucceeded_ = true;

    // Parsed public field cache (keyed by resolved script path).
    std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> parsedFieldsCache_;
    std::unordered_map<std::string, std::filesystem::file_time_type> parsedFieldsModTimes_;

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
    void render_settings_window();
    void render_workspace_tree_node(const WorkspaceTreeNode &node);
    void reset_scene_camera();
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
  };

}

#endif
