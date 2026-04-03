#ifndef HADES_EDITOR_EDITOR_SETTINGS_HPP
#define HADES_EDITOR_EDITOR_SETTINGS_HPP

#include <filesystem>
#include <string>
#include <unordered_map>

namespace hades
{
  struct WorkspaceEditorSettings
  {
    bool showDebugInfo = false;
    float sceneCameraTargetX = 0.0f;
    float sceneCameraTargetY = 0.0f;
    float sceneCameraTargetZ = 0.0f;
    float sceneCameraDistance = 1.0f;
    float sceneCameraYawDegrees = 0.0f;
    float sceneCameraPitchDegrees = 0.0f;
    std::unordered_map<std::string, bool> pluginVisibility;
  };

  std::filesystem::path workspace_settings_path(const std::filesystem::path &workspacePath);

  bool load_workspace_settings(
      const std::filesystem::path &workspacePath,
      WorkspaceEditorSettings &settings,
      std::string *errorMessage = nullptr);

  bool save_workspace_settings(
      const std::filesystem::path &workspacePath,
      const WorkspaceEditorSettings &settings,
      std::string *errorMessage = nullptr);
}

#endif
