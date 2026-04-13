#include "workspace_file_operations.hpp"

#include <algorithm>
#include <system_error>
#include <unordered_set>

#include "../engine/components/script_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"

namespace hades
{
  namespace
  {
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

    std::filesystem::path normalize_existing_path(
        const std::filesystem::path &path,
        const std::string &label,
        std::string *errorMessage)
    {
      std::error_code errorCode;
      const std::filesystem::path absolutePath = std::filesystem::absolute(path, errorCode);
      if (errorCode)
      {
        set_error_message(
            errorMessage,
            "Unable to resolve " + label + " '" + path.string() + "': " + errorCode.message());
        return {};
      }

      std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(absolutePath, errorCode);
      if (errorCode)
      {
        normalizedPath = absolutePath.lexically_normal();
      }

      return normalizedPath;
    }

    std::string normalize_script_attachment_path(const std::string &scriptPath)
    {
      return std::filesystem::path(scriptPath).lexically_normal().generic_string();
    }

    std::string relative_workspace_path(
        const std::filesystem::path &workspacePath,
        const std::filesystem::path &path)
    {
      const std::filesystem::path relativePath = path.lexically_relative(workspacePath);
      const std::filesystem::path normalizedPath =
          relativePath.empty() ? path.lexically_normal() : relativePath.lexically_normal();
      return normalizedPath.generic_string();
    }

    bool path_is_within_workspace(
        const std::filesystem::path &workspacePath,
        const std::filesystem::path &targetPath)
    {
      const std::filesystem::path relativePath = targetPath.lexically_relative(workspacePath);
      const std::string relativeString = relativePath.generic_string();
      return !relativeString.empty() &&
             relativeString != ".." &&
             relativeString.rfind("../", 0) != 0;
    }

    bool collect_deleted_script_paths(
        const std::filesystem::path &workspacePath,
        const std::filesystem::path &targetPath,
        std::vector<std::string> &scriptPaths,
        std::string *errorMessage)
    {
      std::error_code errorCode;
      const bool targetIsDirectory = std::filesystem::is_directory(targetPath, errorCode);
      if (errorCode)
      {
        set_error_message(
            errorMessage,
            "Unable to inspect workspace item '" + targetPath.string() + "': " + errorCode.message());
        return false;
      }

      if (!targetIsDirectory)
      {
        if (targetPath.extension() == ".cpp")
        {
          scriptPaths.push_back(relative_workspace_path(workspacePath, targetPath));
        }
        return true;
      }

      for (std::filesystem::recursive_directory_iterator iterator(targetPath, errorCode);
           !errorCode && iterator != std::filesystem::recursive_directory_iterator();
           iterator.increment(errorCode))
      {
        std::error_code entryError;
        if (!iterator->is_regular_file(entryError))
        {
          continue;
        }

        if (iterator->path().extension() == ".cpp")
        {
          scriptPaths.push_back(relative_workspace_path(workspacePath, iterator->path()));
        }
      }

      if (errorCode)
      {
        set_error_message(
            errorMessage,
            "Unable to inspect workspace folder '" + targetPath.string() + "': " + errorCode.message());
        return false;
      }

      std::sort(scriptPaths.begin(), scriptPaths.end());
      scriptPaths.erase(std::unique(scriptPaths.begin(), scriptPaths.end()), scriptPaths.end());
      return true;
    }

    std::size_t remove_deleted_script_assignments(
        const std::unordered_set<std::string> &removedScriptPaths,
        EntityManager &entityManager,
        ComponentManager &componentManager,
        std::size_t &affectedScriptComponents)
    {
      std::size_t removedAssignments = 0;
      affectedScriptComponents = 0;

      for (const Entity::EntityId entity : entityManager.getAllEntities())
      {
        if (!componentManager.hasComponent<ScriptComponent>(entity))
        {
          continue;
        }

        auto &scriptComponent = componentManager.getComponent<ScriptComponent>(entity);
        const std::size_t previousSize = scriptComponent.attachments.size();

        scriptComponent.attachments.erase(
            std::remove_if(
                scriptComponent.attachments.begin(),
                scriptComponent.attachments.end(),
                [&removedScriptPaths](const ScriptAttachment &attachment)
                {
                  return removedScriptPaths.find(normalize_script_attachment_path(attachment.scriptPath)) !=
                         removedScriptPaths.end();
                }),
            scriptComponent.attachments.end());

        if (scriptComponent.attachments.size() != previousSize)
        {
          removedAssignments += previousSize - scriptComponent.attachments.size();
          ++affectedScriptComponents;
        }
      }

      return removedAssignments;
    }
  }

  bool copy_file_to_directory(
      const std::filesystem::path &sourcePath,
      const std::filesystem::path &destinationDirectory,
      std::filesystem::path *copiedPath,
      std::string *errorMessage)
  {
    if (sourcePath.empty())
    {
      set_error_message(errorMessage, "Enter a source file path before copying.");
      return false;
    }

    if (destinationDirectory.empty())
    {
      set_error_message(errorMessage, "Choose a destination folder before copying.");
      return false;
    }

    const std::filesystem::path normalizedSource =
        normalize_existing_path(sourcePath, "source file", errorMessage);
    if (normalizedSource.empty())
    {
      return false;
    }

    std::error_code errorCode;
    if (!std::filesystem::exists(normalizedSource, errorCode))
    {
      set_error_message(errorMessage, "Source file does not exist: " + normalizedSource.string());
      return false;
    }

    if (errorCode)
    {
      set_error_message(
          errorMessage,
          "Unable to inspect source file '" + normalizedSource.string() + "': " + errorCode.message());
      return false;
    }

    errorCode.clear();
    if (!std::filesystem::is_regular_file(normalizedSource, errorCode))
    {
      if (errorCode)
      {
        set_error_message(
            errorMessage,
            "Unable to inspect source file '" + normalizedSource.string() + "': " + errorCode.message());
      }
      else
      {
        set_error_message(errorMessage, "Only regular files can be copied into the workspace.");
      }
      return false;
    }

    const std::filesystem::path normalizedDestination =
        normalize_existing_path(destinationDirectory, "destination folder", errorMessage);
    if (normalizedDestination.empty())
    {
      return false;
    }

    errorCode.clear();
    if (!std::filesystem::exists(normalizedDestination, errorCode) ||
        !std::filesystem::is_directory(normalizedDestination, errorCode))
    {
      if (errorCode)
      {
        set_error_message(
            errorMessage,
            "Unable to inspect destination folder '" + normalizedDestination.string() + "': " + errorCode.message());
      }
      else
      {
        set_error_message(errorMessage, "Choose an existing destination folder.");
      }
      return false;
    }

    const std::filesystem::path targetPath = normalizedDestination / normalizedSource.filename();
    errorCode.clear();
    if (std::filesystem::exists(targetPath, errorCode))
    {
      if (errorCode)
      {
        set_error_message(
            errorMessage,
            "Unable to inspect destination file '" + targetPath.string() + "': " + errorCode.message());
      }
      else
      {
        set_error_message(errorMessage, "'" + targetPath.filename().string() + "' already exists in the destination folder.");
      }
      return false;
    }

    errorCode.clear();
    std::filesystem::copy_file(normalizedSource, targetPath, std::filesystem::copy_options::none, errorCode);
    if (errorCode)
    {
      set_error_message(
          errorMessage,
          "Unable to copy '" + normalizedSource.string() + "' to '" + targetPath.string() + "': " + errorCode.message());
      return false;
    }

    if (copiedPath != nullptr)
    {
      *copiedPath = targetPath;
    }

    clear_error_message(errorMessage);
    return true;
  }

  bool delete_workspace_item(
      const std::filesystem::path &workspacePath,
      const std::filesystem::path &targetPath,
      EntityManager &entityManager,
      ComponentManager &componentManager,
      WorkspaceDeleteResult *result,
      std::string *errorMessage)
  {
    if (workspacePath.empty())
    {
      set_error_message(errorMessage, "Open a workspace before deleting files from it.");
      return false;
    }

    if (targetPath.empty())
    {
      set_error_message(errorMessage, "Choose a workspace item before deleting it.");
      return false;
    }

    const std::filesystem::path normalizedWorkspace =
        normalize_existing_path(workspacePath, "workspace folder", errorMessage);
    if (normalizedWorkspace.empty())
    {
      return false;
    }

    std::error_code errorCode;
    if (!std::filesystem::exists(normalizedWorkspace, errorCode) ||
        !std::filesystem::is_directory(normalizedWorkspace, errorCode))
    {
      if (errorCode)
      {
        set_error_message(
            errorMessage,
            "Unable to inspect workspace folder '" + normalizedWorkspace.string() + "': " + errorCode.message());
      }
      else
      {
        set_error_message(errorMessage, "Workspace folder is no longer available.");
      }
      return false;
    }

    const std::filesystem::path normalizedTarget =
        normalize_existing_path(targetPath, "workspace item", errorMessage);
    if (normalizedTarget.empty())
    {
      return false;
    }

    errorCode.clear();
    if (!std::filesystem::exists(normalizedTarget, errorCode))
    {
      if (errorCode)
      {
        set_error_message(
            errorMessage,
            "Unable to inspect workspace item '" + normalizedTarget.string() + "': " + errorCode.message());
      }
      else
      {
        set_error_message(errorMessage, "The selected workspace item is no longer available.");
      }
      return false;
    }

    errorCode.clear();
    if (std::filesystem::equivalent(normalizedWorkspace, normalizedTarget, errorCode))
    {
      set_error_message(errorMessage, "The active workspace folder cannot be deleted from the file tree.");
      return false;
    }

    if (!path_is_within_workspace(normalizedWorkspace, normalizedTarget))
    {
      set_error_message(errorMessage, "Only items inside the active workspace can be deleted here.");
      return false;
    }

    std::vector<std::string> removedScriptPaths;
    if (!collect_deleted_script_paths(normalizedWorkspace, normalizedTarget, removedScriptPaths, errorMessage))
    {
      return false;
    }

    errorCode.clear();
    const bool targetIsDirectory = std::filesystem::is_directory(normalizedTarget, errorCode);
    if (errorCode)
    {
      set_error_message(
          errorMessage,
          "Unable to inspect workspace item '" + normalizedTarget.string() + "': " + errorCode.message());
      return false;
    }

    if (targetIsDirectory)
    {
      std::filesystem::remove_all(normalizedTarget, errorCode);
      if (errorCode)
      {
        set_error_message(
            errorMessage,
            "Unable to delete folder '" + normalizedTarget.string() + "': " + errorCode.message());
        return false;
      }
    }
    else
    {
      const bool removed = std::filesystem::remove(normalizedTarget, errorCode);
      if (errorCode || !removed)
      {
        if (errorCode)
        {
          set_error_message(
              errorMessage,
              "Unable to delete file '" + normalizedTarget.string() + "': " + errorCode.message());
        }
        else
        {
          set_error_message(errorMessage, "Unable to delete file '" + normalizedTarget.string() + "'.");
        }
        return false;
      }
    }

    WorkspaceDeleteResult deleteResult;
    deleteResult.removedScriptPaths = removedScriptPaths;

    if (!removedScriptPaths.empty())
    {
      const std::unordered_set<std::string> removedPathSet(
          removedScriptPaths.begin(),
          removedScriptPaths.end());
      deleteResult.removedScriptAssignments = remove_deleted_script_assignments(
          removedPathSet,
          entityManager,
          componentManager,
          deleteResult.affectedScriptComponents);
    }

    if (result != nullptr)
    {
      *result = std::move(deleteResult);
    }

    clear_error_message(errorMessage);
    return true;
  }
}
