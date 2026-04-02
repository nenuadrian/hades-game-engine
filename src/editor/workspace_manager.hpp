#ifndef HADES_EDITOR_WORKSPACE_MANAGER_HPP
#define HADES_EDITOR_WORKSPACE_MANAGER_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hades
{
  struct WorkspaceEntry
  {
    std::string name;
    std::filesystem::path path;
  };

  class WorkspaceManager
  {
  public:
    WorkspaceManager() = default;
    explicit WorkspaceManager(std::filesystem::path storagePath);

    void set_storage_path(std::filesystem::path storagePath);

    bool load(std::string *errorMessage = nullptr);
    bool save(std::string *errorMessage = nullptr) const;

    std::optional<WorkspaceEntry> open_workspace(
        const std::filesystem::path &workspacePath,
        std::string *errorMessage = nullptr);
    std::optional<WorkspaceEntry> create_workspace(
        const std::filesystem::path &parentDirectory,
        std::string_view workspaceName,
        std::string *errorMessage = nullptr);
    bool prune_missing_recent_workspaces(std::string *errorMessage = nullptr);

    bool has_current_workspace() const;
    const std::optional<WorkspaceEntry> &current_workspace() const;
    const std::vector<WorkspaceEntry> &recent_workspaces() const;

  private:
    std::filesystem::path storagePath_;
    std::optional<WorkspaceEntry> currentWorkspace_;
    std::vector<WorkspaceEntry> recentWorkspaces_;

    void add_recent_workspace(const WorkspaceEntry &workspace);
  };
}

#endif
