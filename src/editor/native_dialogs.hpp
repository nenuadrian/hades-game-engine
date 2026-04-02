#ifndef HADES_EDITOR_NATIVE_DIALOGS_HPP
#define HADES_EDITOR_NATIVE_DIALOGS_HPP

#include <filesystem>
#include <optional>
#include <string>

namespace hades
{
  std::optional<std::filesystem::path> pick_folder_with_native_dialog(
      const std::string &prompt,
      std::string *errorMessage = nullptr);

  std::optional<std::filesystem::path> pick_file_with_native_dialog(
      const std::string &prompt,
      std::string *errorMessage = nullptr);
}

#endif
