#include "editor_settings.hpp"

#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace hades
{
  namespace
  {
    constexpr int WORKSPACE_SETTINGS_FORMAT_VERSION = 2;
    constexpr char WORKSPACE_SETTINGS_FILENAME[] = "settings.json";

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

    void read_optional_bool(const json &value, const char *key, bool &target)
    {
      if (value.contains(key) && value[key].is_boolean())
      {
        target = value[key].get<bool>();
      }
    }

    void read_optional_number(const json &value, const char *key, float &target)
    {
      if (value.contains(key) && value[key].is_number())
      {
        target = value[key].get<float>();
      }
    }

    void read_optional_int(const json &value, const char *key, int &target)
    {
      if (value.contains(key) && value[key].is_number_integer())
      {
        target = value[key].get<int>();
      }
    }

    void read_optional_string(const json &value, const char *key, std::string &target)
    {
      if (value.contains(key) && value[key].is_string())
      {
        target = value[key].get<std::string>();
      }
    }

    void read_export_settings(const json &value, WorkspaceExportSettings &target)
    {
      if (!value.is_object())
      {
        return;
      }

      read_optional_string(value, "projectName", target.projectName);
      read_optional_string(value, "outputPath", target.outputPath);
      read_optional_bool(value, "enableHeadless", target.enableHeadless);
    }

    json write_export_settings(const WorkspaceExportSettings &value)
    {
      return {
          {"projectName", value.projectName},
          {"outputPath", value.outputPath},
          {"enableHeadless", value.enableHeadless},
      };
    }
  }

  std::filesystem::path workspace_settings_path(const std::filesystem::path &workspacePath)
  {
    return workspacePath / ".hades" / WORKSPACE_SETTINGS_FILENAME;
  }

  bool load_workspace_settings(
      const std::filesystem::path &workspacePath,
      WorkspaceEditorSettings &settings,
      std::string *errorMessage)
  {
    if (workspacePath.empty())
    {
      clear_error_message(errorMessage);
      return true;
    }

    const std::filesystem::path settingsPath = workspace_settings_path(workspacePath);
    std::error_code errorCode;
    if (!std::filesystem::exists(settingsPath, errorCode))
    {
      clear_error_message(errorMessage);
      return true;
    }

    if (errorCode)
    {
      set_error_message(
          errorMessage,
          "Unable to inspect workspace settings '" + settingsPath.string() + "': " + errorCode.message());
      return false;
    }

    std::ifstream input(settingsPath);
    if (!input.is_open())
    {
      set_error_message(errorMessage, "Unable to open workspace settings '" + settingsPath.string() + "'.");
      return false;
    }

    try
    {
      json document;
      input >> document;

      if (document.contains("editor") && document["editor"].is_object())
      {
        const json &editor = document["editor"];
        read_optional_bool(editor, "showDebugInfo", settings.showDebugInfo);

        if (editor.contains("sceneCamera") && editor["sceneCamera"].is_object())
        {
          const json &sceneCamera = editor["sceneCamera"];
          read_optional_number(sceneCamera, "targetX", settings.sceneCameraTargetX);
          read_optional_number(sceneCamera, "targetY", settings.sceneCameraTargetY);
          read_optional_number(sceneCamera, "targetZ", settings.sceneCameraTargetZ);
          read_optional_number(sceneCamera, "distance", settings.sceneCameraDistance);
          read_optional_number(sceneCamera, "yawDegrees", settings.sceneCameraYawDegrees);
          read_optional_number(sceneCamera, "pitchDegrees", settings.sceneCameraPitchDegrees);
        }
      }

      if (document.contains("plugins") && document["plugins"].is_object())
      {
        for (auto it = document["plugins"].begin(); it != document["plugins"].end(); ++it)
        {
          if (!it.value().is_boolean())
          {
            continue;
          }

          settings.pluginVisibility[it.key()] = it.value().get<bool>();
        }
      }

      if (document.contains("export") && document["export"].is_object())
      {
        const json &exportSettings = document["export"];
        read_optional_int(exportSettings, "selectedPlatform", settings.selectedExportPlatform);

        if (exportSettings.contains("macOS"))
        {
          read_export_settings(exportSettings["macOS"], settings.exportMacOS);
        }
        if (exportSettings.contains("linux"))
        {
          read_export_settings(exportSettings["linux"], settings.exportLinux);
        }
        if (exportSettings.contains("windows"))
        {
          read_export_settings(exportSettings["windows"], settings.exportWindows);
        }
        if (exportSettings.contains("web"))
        {
          read_export_settings(exportSettings["web"], settings.exportWeb);
        }
      }
    }
    catch (const std::exception &exception)
    {
      set_error_message(
          errorMessage,
          "Unable to parse workspace settings '" + settingsPath.string() + "': " + exception.what());
      return false;
    }

    clear_error_message(errorMessage);
    return true;
  }

  bool save_workspace_settings(
      const std::filesystem::path &workspacePath,
      const WorkspaceEditorSettings &settings,
      std::string *errorMessage)
  {
    if (workspacePath.empty())
    {
      clear_error_message(errorMessage);
      return true;
    }

    const std::filesystem::path settingsPath = workspace_settings_path(workspacePath);
    std::error_code errorCode;
    std::filesystem::create_directories(settingsPath.parent_path(), errorCode);
    if (errorCode)
    {
      set_error_message(
          errorMessage,
          "Unable to prepare workspace settings directory '" + settingsPath.parent_path().string() + "': " + errorCode.message());
      return false;
    }

    json document;
    document["version"] = WORKSPACE_SETTINGS_FORMAT_VERSION;
    document["editor"] = {
        {"showDebugInfo", settings.showDebugInfo},
        {"sceneCamera",
         {
             {"targetX", settings.sceneCameraTargetX},
             {"targetY", settings.sceneCameraTargetY},
             {"targetZ", settings.sceneCameraTargetZ},
             {"distance", settings.sceneCameraDistance},
             {"yawDegrees", settings.sceneCameraYawDegrees},
             {"pitchDegrees", settings.sceneCameraPitchDegrees},
         }}};
    document["plugins"] = json::object();
    for (const auto &[pluginId, visible] : settings.pluginVisibility)
    {
      document["plugins"][pluginId] = visible;
    }
    document["export"] = {
        {"selectedPlatform", settings.selectedExportPlatform},
        {"macOS", write_export_settings(settings.exportMacOS)},
        {"linux", write_export_settings(settings.exportLinux)},
        {"windows", write_export_settings(settings.exportWindows)},
        {"web", write_export_settings(settings.exportWeb)},
    };

    std::ofstream output(settingsPath, std::ios::trunc);
    if (!output.is_open())
    {
      set_error_message(errorMessage, "Unable to write workspace settings '" + settingsPath.string() + "'.");
      return false;
    }

    output << document.dump(2) << '\n';
    if (!output.good())
    {
      set_error_message(errorMessage, "Unable to finalize workspace settings '" + settingsPath.string() + "'.");
      return false;
    }

    clear_error_message(errorMessage);
    return true;
  }
}
