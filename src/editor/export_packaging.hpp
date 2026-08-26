#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Packaging logic behind the editor's Export window, split out of
// editor_export.cpp so it can be exercised without an ImGui context: the file
// sweeps that decide which project files travel with an exported game, and the
// text of the launcher scripts and bundle metadata written next to the binary.
// editor_export.cpp keeps the parts that need the editor — the window itself,
// the CMake subprocesses, and the build log.
namespace hades::exporting
{
  enum class ExportPlatform
  {
    macOS,
    Linux,
    Windows,
  };

  constexpr int platform_index(ExportPlatform platform)
  {
    return static_cast<int>(platform);
  }

  const char *platform_label(ExportPlatform platform);

  // Cross-compilation is not supported, so the export UI only offers the
  // platform the editor itself was built for.
  bool is_current_platform(ExportPlatform platform);

  // Name of the runtime executable as it is built and as the launcher scripts
  // invoke it.
  const char *runtime_binary_name(ExportPlatform platform);

  enum class LauncherMode
  {
    Windowed,
    Headless,
    Api,
  };

  // Runtime arguments a launcher of this mode passes to the binary.
  std::string runtime_arguments(LauncherMode mode);

  // File name of a launcher script, including the platform's extension.
  std::string launcher_file_name(
      ExportPlatform platform,
      LauncherMode mode,
      const std::string &projectName);

  // Contents of a launcher script. `debugBuild` adds the HADES_DEBUG=1 export
  // that makes the runtime log verbosely.
  std::string launcher_script(
      ExportPlatform platform,
      LauncherMode mode,
      const std::string &projectName,
      bool debugBuild);

  // Info.plist for the macOS .app bundle.
  std::string info_plist(const std::string &projectName, const std::string &binaryName);

  std::string lowercase_extension(const std::filesystem::path &path);
  bool has_model_file_extension(const std::filesystem::path &path);

  // Directory names the workspace-wide model sweep refuses to descend into.
  bool is_ignored_scan_component(std::string_view component);

  std::string decode_uri_escapes(const std::string &uri);

  // Every workspace directory holding a model file that has to travel with the
  // export: those the workspace sweep finds, plus those the saved worlds name
  // even when the sweep's ignore list would have skipped them. `destination` is
  // excluded so re-exporting into the workspace does not feed a previous
  // export's copies back in.
  std::vector<std::filesystem::path> collect_model_directories(
      const std::filesystem::path &workspacePath,
      const std::filesystem::path &destination);

  void copy_directory_recursive(
      const std::filesystem::path &source,
      const std::filesystem::path &destination,
      std::string *errorMessage);

  // Blueprint assets live at arbitrary workspace-relative paths rather than
  // inside `.hades`, and the runtime refuses to start when one is missing.
  // Returns the number of files copied.
  int copy_blueprint_assets(
      const std::filesystem::path &workspacePath,
      const std::filesystem::path &destination);

  // Buffers and images a `.gltf` references from outside its own directory,
  // copied at the same workspace-relative offsets so the URIs inside the copied
  // document still resolve. `copiedTargets` accumulates across calls so two
  // documents sharing a buffer copy it once. Returns the number of files copied.
  int copy_gltf_referenced_files(
      const std::filesystem::path &gltfFile,
      const std::filesystem::path &workspacePath,
      const std::filesystem::path &destination,
      std::vector<std::filesystem::path> &copiedTargets);

  // Copies every directory from collect_model_directories, plus the
  // out-of-directory sidecars any copied `.gltf` references. Returns the number
  // of files copied.
  int copy_model_assets(
      const std::filesystem::path &workspacePath,
      const std::filesystem::path &destination);
}
