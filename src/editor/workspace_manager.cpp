#include "workspace_manager.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

namespace hades
{
  namespace
  {
    constexpr std::size_t MAX_RECENT_WORKSPACES = 8;

    void set_error_message(std::string *errorMessage, const std::string &message)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = message;
      }
    }

    void clear_error_message(std::string *errorMessage)
    {
      if (errorMessage != nullptr)
      {
        errorMessage->clear();
      }
    }

    std::string trim_copy(std::string_view value)
    {
      std::size_t first = 0;
      while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0)
      {
        ++first;
      }

      std::size_t last = value.size();
      while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0)
      {
        --last;
      }

      return std::string(value.substr(first, last - first));
    }

    std::string workspace_name_from_path(const std::filesystem::path &path)
    {
      const std::string filename = path.filename().string();
      if (!filename.empty())
      {
        return filename;
      }

      return path.string();
    }

    std::optional<std::filesystem::path> normalize_existing_directory(
        const std::filesystem::path &inputPath,
        std::string *errorMessage)
    {
      std::error_code errorCode;
      const std::filesystem::path absolutePath = std::filesystem::absolute(inputPath, errorCode);
      if (errorCode)
      {
        set_error_message(
            errorMessage,
            "Unable to resolve workspace path '" + inputPath.string() + "': " + errorCode.message());
        return std::nullopt;
      }

      if (!std::filesystem::exists(absolutePath, errorCode))
      {
        if (errorCode)
        {
          set_error_message(
              errorMessage,
              "Unable to inspect workspace path '" + absolutePath.string() + "': " + errorCode.message());
        }
        else
        {
          set_error_message(errorMessage, "Workspace folder does not exist: " + absolutePath.string());
        }
        return std::nullopt;
      }

      errorCode.clear();
      if (!std::filesystem::is_directory(absolutePath, errorCode))
      {
        if (errorCode)
        {
          set_error_message(
              errorMessage,
              "Unable to inspect workspace folder '" + absolutePath.string() + "': " + errorCode.message());
        }
        else
        {
          set_error_message(errorMessage, "Workspace path is not a folder: " + absolutePath.string());
        }
        return std::nullopt;
      }

      errorCode.clear();
      std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(absolutePath, errorCode);
      if (errorCode)
      {
        normalizedPath = absolutePath.lexically_normal();
      }

      clear_error_message(errorMessage);
      return normalizedPath;
    }

    std::optional<std::filesystem::path> normalize_directory_path(
        const std::filesystem::path &inputPath,
        std::string *errorMessage)
    {
      std::error_code errorCode;
      const std::filesystem::path absolutePath = std::filesystem::absolute(inputPath, errorCode);
      if (errorCode)
      {
        set_error_message(
            errorMessage,
            "Unable to resolve workspace path '" + inputPath.string() + "': " + errorCode.message());
        return std::nullopt;
      }

      clear_error_message(errorMessage);
      return absolutePath.lexically_normal();
    }

    std::optional<WorkspaceEntry> make_workspace_entry(
        const std::filesystem::path &workspacePath,
        std::string *errorMessage)
    {
      const auto normalizedPath = normalize_existing_directory(workspacePath, errorMessage);
      if (!normalizedPath.has_value())
      {
        return std::nullopt;
      }

      clear_error_message(errorMessage);
      return WorkspaceEntry{
          workspace_name_from_path(*normalizedPath),
          *normalizedPath};
    }
  }

  WorkspaceManager::WorkspaceManager(std::filesystem::path storagePath)
      : storagePath_(std::move(storagePath)) {}

  void WorkspaceManager::set_storage_path(std::filesystem::path storagePath)
  {
    storagePath_ = std::move(storagePath);
  }

  bool WorkspaceManager::load(std::string *errorMessage)
  {
    recentWorkspaces_.clear();

    if (storagePath_.empty())
    {
      set_error_message(errorMessage, "Workspace storage path is not configured.");
      return false;
    }

    std::ifstream input(storagePath_);
    if (!input.is_open())
    {
      if (!std::filesystem::exists(storagePath_))
      {
        clear_error_message(errorMessage);
        return true;
      }

      set_error_message(errorMessage, "Unable to read workspace history from: " + storagePath_.string());
      return false;
    }

    bool removedInvalidEntries = false;
    std::string line;
    while (std::getline(input, line))
    {
      const std::string trimmedLine = trim_copy(line);
      if (trimmedLine.empty())
      {
        continue;
      }

      std::string entryError;
      const auto workspace = make_workspace_entry(std::filesystem::path(trimmedLine), &entryError);
      if (!workspace.has_value())
      {
        removedInvalidEntries = true;
        continue;
      }

      add_recent_workspace(*workspace);
    }

    if (removedInvalidEntries)
    {
      save();
    }

    clear_error_message(errorMessage);
    return true;
  }

  bool WorkspaceManager::save(std::string *errorMessage) const
  {
    if (storagePath_.empty())
    {
      set_error_message(errorMessage, "Workspace storage path is not configured.");
      return false;
    }

    std::error_code errorCode;
    const std::filesystem::path parentDirectory = storagePath_.parent_path();
    if (!parentDirectory.empty())
    {
      std::filesystem::create_directories(parentDirectory, errorCode);
      if (errorCode)
      {
        set_error_message(
            errorMessage,
            "Unable to prepare workspace settings directory '" + parentDirectory.string() + "': " + errorCode.message());
        return false;
      }
    }

    std::ofstream output(storagePath_, std::ios::trunc);
    if (!output.is_open())
    {
      set_error_message(errorMessage, "Unable to write workspace history to: " + storagePath_.string());
      return false;
    }

    for (const WorkspaceEntry &workspace : recentWorkspaces_)
    {
      output << workspace.path.string() << '\n';
    }

    clear_error_message(errorMessage);
    return true;
  }

  std::optional<WorkspaceEntry> WorkspaceManager::open_workspace(
      const std::filesystem::path &workspacePath,
      std::string *errorMessage)
  {
    const auto workspace = make_workspace_entry(workspacePath, errorMessage);
    if (!workspace.has_value())
    {
      return std::nullopt;
    }

    currentWorkspace_ = *workspace;
    add_recent_workspace(*workspace);
    if (!save(errorMessage))
    {
      return workspace;
    }

    clear_error_message(errorMessage);
    return workspace;
  }

  std::optional<WorkspaceEntry> WorkspaceManager::create_workspace(
      const std::filesystem::path &parentDirectory,
      std::string_view workspaceName,
      std::string *errorMessage)
  {
    const std::string trimmedName = trim_copy(workspaceName);
    if (trimmedName.empty())
    {
      set_error_message(errorMessage, "Workspace name cannot be empty.");
      return std::nullopt;
    }

    if (trimmedName.find('/') != std::string::npos || trimmedName.find('\\') != std::string::npos)
    {
      set_error_message(errorMessage, "Workspace name cannot contain path separators.");
      return std::nullopt;
    }

    std::string parentError;
    const auto normalizedParent = normalize_directory_path(parentDirectory, &parentError);
    if (!normalizedParent.has_value())
    {
      set_error_message(
          errorMessage,
          parentError.empty() ? "Choose a valid parent folder before creating a workspace." : parentError);
      return std::nullopt;
    }

    const std::filesystem::path workspaceDirectory = *normalizedParent / trimmedName;

    std::error_code errorCode;
    if (std::filesystem::exists(workspaceDirectory, errorCode))
    {
      if (errorCode)
      {
        set_error_message(
            errorMessage,
            "Unable to inspect target workspace folder '" + workspaceDirectory.string() + "': " + errorCode.message());
      }
      else
      {
        set_error_message(errorMessage, "Workspace folder already exists: " + workspaceDirectory.string());
      }
      return std::nullopt;
    }

    errorCode.clear();
    std::filesystem::create_directories(workspaceDirectory, errorCode);
    if (errorCode)
    {
      set_error_message(
          errorMessage,
          "Unable to create workspace folder '" + workspaceDirectory.string() + "': " + errorCode.message());
      return std::nullopt;
    }

    return open_workspace(workspaceDirectory, errorMessage);
  }

  bool WorkspaceManager::prune_missing_recent_workspaces(std::string *errorMessage)
  {
    const auto originalSize = recentWorkspaces_.size();

    recentWorkspaces_.erase(
        std::remove_if(
            recentWorkspaces_.begin(),
            recentWorkspaces_.end(),
            [](const WorkspaceEntry &workspace)
            {
              std::error_code errorCode;
              return !std::filesystem::exists(workspace.path, errorCode) ||
                     !std::filesystem::is_directory(workspace.path, errorCode);
            }),
        recentWorkspaces_.end());

    if (recentWorkspaces_.size() == originalSize)
    {
      clear_error_message(errorMessage);
      return true;
    }

    if (!save(errorMessage))
    {
      return false;
    }

    clear_error_message(errorMessage);
    return true;
  }

  bool WorkspaceManager::has_current_workspace() const
  {
    return currentWorkspace_.has_value();
  }

  void WorkspaceManager::close_current_workspace()
  {
    currentWorkspace_ = std::nullopt;
  }

  const std::optional<WorkspaceEntry> &WorkspaceManager::current_workspace() const
  {
    return currentWorkspace_;
  }

  const std::vector<WorkspaceEntry> &WorkspaceManager::recent_workspaces() const
  {
    return recentWorkspaces_;
  }

  void WorkspaceManager::add_recent_workspace(const WorkspaceEntry &workspace)
  {
    recentWorkspaces_.erase(
        std::remove_if(
            recentWorkspaces_.begin(),
            recentWorkspaces_.end(),
            [&workspace](const WorkspaceEntry &existingWorkspace)
            {
              return existingWorkspace.path == workspace.path;
            }),
        recentWorkspaces_.end());

    recentWorkspaces_.insert(recentWorkspaces_.begin(), workspace);
    if (recentWorkspaces_.size() > MAX_RECENT_WORKSPACES)
    {
      recentWorkspaces_.resize(MAX_RECENT_WORKSPACES);
    }
  }
}
