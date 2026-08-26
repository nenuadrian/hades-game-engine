#ifndef HADES_EDITOR_WORKSPACE_FILE_OPERATIONS_HPP
#define HADES_EDITOR_WORKSPACE_FILE_OPERATIONS_HPP

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace hades
{
  class ComponentManager;
  class EntityManager;

  struct WorkspaceDeleteResult
  {
    std::vector<std::string> removedScriptPaths;
    std::size_t removedScriptAssignments = 0;
    std::size_t affectedScriptComponents = 0;

    std::vector<std::string> removedBlueprintPaths;
    std::size_t removedBlueprintAssignments = 0;
    std::size_t affectedBlueprintComponents = 0;
  };

  bool copy_file_to_directory(
      const std::filesystem::path &sourcePath,
      const std::filesystem::path &destinationDirectory,
      std::filesystem::path *copiedPath = nullptr,
      std::string *errorMessage = nullptr);

  bool delete_workspace_item(
      const std::filesystem::path &workspacePath,
      const std::filesystem::path &targetPath,
      EntityManager &entityManager,
      ComponentManager &componentManager,
      WorkspaceDeleteResult *result = nullptr,
      std::string *errorMessage = nullptr);
}

#endif
