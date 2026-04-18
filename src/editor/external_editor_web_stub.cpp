// Web stub for external_editor.cpp. Launching a separate process (VS Code,
// Rider, etc.) is not meaningful in a browser, so these always fail with a
// message the caller can surface to the user.

#include "external_editor.hpp"

namespace hades
{
  bool open_in_external_editor(
      ExternalEditor,
      const std::filesystem::path &,
      const std::filesystem::path &,
      std::string *errorMessage)
  {
    if (errorMessage != nullptr)
    {
      *errorMessage = "External editor integration is not available on the web build.";
    }
    return false;
  }

  const char *external_editor_name(ExternalEditor editor)
  {
    switch (editor)
    {
      case ExternalEditor::VSCode: return "Visual Studio Code";
      case ExternalEditor::Rider: return "JetBrains Rider";
      case ExternalEditor::VisualStudio: return "Visual Studio";
      case ExternalEditor::System: return "System Default";
    }
    return "Unknown";
  }
}
