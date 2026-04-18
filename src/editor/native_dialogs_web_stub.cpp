// Web stub for native_dialogs.cpp. The browser has no native file picker
// equivalent available to a WASM app without explicit JS bridging, so these
// return nullopt and set an error for any UI path that surfaces one.

#include <filesystem>
#include <optional>
#include <string>

#include "native_dialogs.hpp"

namespace hades
{
  std::optional<std::filesystem::path> pick_folder_with_native_dialog(
      const std::string &,
      std::string *errorMessage)
  {
    if (errorMessage != nullptr)
    {
      *errorMessage = "Native folder picker is not available on the web build.";
    }
    return std::nullopt;
  }

  std::optional<std::filesystem::path> pick_file_with_native_dialog(
      const std::string &,
      std::string *errorMessage)
  {
    if (errorMessage != nullptr)
    {
      *errorMessage = "Native file picker is not available on the web build.";
    }
    return std::nullopt;
  }
}
