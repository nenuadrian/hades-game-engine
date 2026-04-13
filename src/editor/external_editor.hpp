#ifndef HADES_EDITOR_EXTERNAL_EDITOR_HPP
#define HADES_EDITOR_EXTERNAL_EDITOR_HPP

#include <filesystem>
#include <string>

namespace hades
{
  enum class ExternalEditor
  {
    VSCode,
    Rider,
    VisualStudio,
    System,
  };

  /// Open the workspace (or a specific script file) in an external code editor.
  /// When \p scriptPath is non-empty the editor opens that file directly;
  /// otherwise it opens the workspace folder / solution.
  bool open_in_external_editor(
      ExternalEditor editor,
      const std::filesystem::path &workspacePath,
      const std::filesystem::path &scriptPath = {},
      std::string *errorMessage = nullptr);

  /// Return a human-readable name for the editor enum value.
  const char *external_editor_name(ExternalEditor editor);
}

#endif
