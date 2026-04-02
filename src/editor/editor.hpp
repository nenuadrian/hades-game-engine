#ifndef HADES_EDITOR_EDITOR_HPP
#define HADES_EDITOR_EDITOR_HPP

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
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
    bool openWorkspaceCreateDialog_ = false;
    WorkspaceCreateKind pendingWorkspaceCreateKind_ = WorkspaceCreateKind::None;
    std::filesystem::path pendingWorkspaceCreateParentPath_;
    std::array<char, 256> workspaceCreateNameBuffer_{};
    std::string workspaceCreateError_;

    void configure_default_dock_layout(std::uint32_t dockspaceId);
    void refresh_workspace_cache(float deltaTime, const std::filesystem::path &workspacePath);
    void invalidate_workspace_cache();
    void request_workspace_item_creation(WorkspaceCreateKind kind, const std::filesystem::path &parentPath);
    void render_workspace_create_dialog();
    void render_workspace_tree_node(const WorkspaceTreeNode &node);
    void workspace();
    void handle_entity_creation_requests(EntityManager &entityManager, ComponentManager &componentManager);
    void import_model(EntityManager &entityManager, ComponentManager &componentManager);
    void handle_play_mode_requests(EntityManager &entityManager, ComponentManager &componentManager, ScriptRuntime &scriptRuntime);
    void start_play_mode(EntityManager &entityManager, ComponentManager &componentManager, ScriptRuntime &scriptRuntime);
    void stop_play_mode(ScriptRuntime &scriptRuntime);
    void set_main_camera(Entity::EntityId entity, EntityManager &entityManager, ComponentManager &componentManager);
    std::optional<Entity::EntityId> get_selected_parent(ComponentManager &componentManager) const;
    std::string entity_label(Entity::EntityId entity, ComponentManager &componentManager) const;
    void entities(EntityManager &entityManager, ComponentManager &componentManager);
    void properties(EntityManager &entityManager, ComponentManager &componentManager);
    void components(EntityManager &entityManager, ComponentManager &componentManager);
    void game(EntityManager &entityManager, ComponentManager &componentManager, ScriptRuntime &scriptRuntime);
    void render_hierarchy(Entity::EntityId entity, ComponentManager &componentManager);
    void render_hierarchies(EntityManager &entityManager, ComponentManager &componentManager);
    void debug(float deltaTime);
  };

}

#endif
