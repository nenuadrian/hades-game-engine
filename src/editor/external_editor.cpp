#include "external_editor.hpp"

#include <string>
#include <vector>

#include "../engine/runtime/subprocess.hpp"

namespace hades
{
  const char *external_editor_name(ExternalEditor editor)
  {
    switch (editor)
    {
    case ExternalEditor::VSCode:
      return "Visual Studio Code";
    case ExternalEditor::Rider:
      return "Rider";
    case ExternalEditor::VisualStudio:
      return "Visual Studio";
    case ExternalEditor::System:
      return "System Default";
    }
    return "Unknown";
  }

  bool open_in_external_editor(
      ExternalEditor editor,
      const std::filesystem::path &workspacePath,
      const std::filesystem::path &scriptPath,
      std::string *errorMessage)
  {
    if (workspacePath.empty())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "No workspace is open.";
      }
      return false;
    }

    const std::filesystem::path slnPath = workspacePath / "HadesScripts.sln";
    std::vector<std::string> args;

    switch (editor)
    {
    case ExternalEditor::VSCode:
    {
      if (!scriptPath.empty())
      {
        args = {"code", "-g", scriptPath.string()};
      }
      else
      {
        args = {"code", workspacePath.string()};
      }
      break;
    }

    case ExternalEditor::Rider:
    {
      if (!scriptPath.empty())
      {
        args = {"rider", "--line", "1", scriptPath.string()};
      }
      else
      {
        args = {"rider", slnPath.string()};
      }
      break;
    }

    case ExternalEditor::VisualStudio:
    {
#ifdef _WIN32
      if (!scriptPath.empty())
      {
        args = {"devenv", "/edit", scriptPath.string()};
      }
      else
      {
        args = {"devenv", slnPath.string()};
      }
#else
      if (errorMessage != nullptr)
      {
        *errorMessage = "Visual Studio is only available on Windows.";
      }
      return false;
#endif
      break;
    }

    case ExternalEditor::System:
    {
      const std::filesystem::path &target = scriptPath.empty()
                                                ? workspacePath
                                                : scriptPath;
#ifdef __APPLE__
      args = {"open", target.string()};
#elif defined(_WIN32)
      args = {"cmd", "/c", "start", "", target.string()};
#else
      args = {"xdg-open", target.string()};
#endif
      break;
    }
    }

    if (args.empty())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "No launch command configured.";
      }
      return false;
    }

    const ProcessResult result = Subprocess::run_capture(args);
    if (!result.launched)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "Failed to launch " + std::string(external_editor_name(editor)) +
                        ". Make sure it is installed and on your PATH.";
      }
      return false;
    }

    return true;
  }
}
