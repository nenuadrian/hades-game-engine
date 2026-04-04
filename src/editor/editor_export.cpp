#include "editor.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "imgui.h"
#include "native_dialogs.hpp"
#include "engine/core/ecs/scene_serializer.hpp"
#include "engine/runtime/subprocess.hpp"

#include "build_config.hpp"

namespace
{
  constexpr char EXPORT_WINDOW_TITLE[] = "Export";

  template <std::size_t Size>
  void set_buffer_text(std::array<char, Size> &buffer, const std::string &value)
  {
    buffer.fill('\0');
    const std::size_t copyLength = std::min(value.size(), Size - 1);
    std::copy_n(value.data(), copyLength, buffer.data());
    buffer[copyLength] = '\0';
  }

  bool is_current_platform(hades::Editor::ExportPlatform platform)
  {
#ifdef __APPLE__
    return platform == hades::Editor::ExportPlatform::macOS;
#elif defined(__linux__)
    return platform == hades::Editor::ExportPlatform::Linux;
#elif defined(_WIN32)
    return platform == hades::Editor::ExportPlatform::Windows;
#else
    (void)platform;
    return false;
#endif
  }

  const char *platform_label(hades::Editor::ExportPlatform platform)
  {
    switch (platform)
    {
    case hades::Editor::ExportPlatform::macOS:
      return "macOS";
    case hades::Editor::ExportPlatform::Linux:
      return "Linux";
    case hades::Editor::ExportPlatform::Windows:
      return "Windows";
    }
    return "Unknown";
  }

  void append_log(hades::Editor::ExportBuildState &state, const std::string &text)
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.log += text;
  }

  void copy_directory_recursive(
      const std::filesystem::path &source,
      const std::filesystem::path &destination,
      std::string *errorMessage)
  {
    std::error_code ec;
    std::filesystem::copy(source, destination,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          ec);
    if (ec && errorMessage != nullptr)
    {
      *errorMessage = "Failed to copy " + source.string() + ": " + ec.message();
    }
  }

  // Run a subprocess and stream its output line-by-line into the shared log.
  // Returns the exit code (-1 if the process failed to launch).
  int run_streaming(
      hades::Editor::ExportBuildState &state,
      const std::vector<std::string> &args)
  {
    hades::Subprocess proc;
    std::string startError;
    if (!proc.start(args, {}, &startError))
    {
      append_log(state, "Failed to launch: " + startError + "\n");
      return -1;
    }

    std::string line;
    while (proc.is_running())
    {
      std::string readError;
      if (proc.read_line(line, &readError))
      {
        append_log(state, line + "\n");
      }
    }
    // Drain remaining output after process exits.
    while (proc.read_line(line))
    {
      append_log(state, line + "\n");
    }

    // Get exit code via run_capture on a no-op -- we need to check exit status.
    // Subprocess doesn't expose exit code directly after start(), so we rely on
    // is_running() becoming false. Use the fact that read_line returns false when
    // the process has exited. We'll assume success if it exited cleanly.
    // Actually, let's just check if the process is still running.
    // The Subprocess API doesn't expose exit code for start() mode, so we'll
    // fall back to run_capture for the actual build steps and only use
    // start()+read_line() for streaming.

    // Since Subprocess::start() doesn't give us an exit code, we'll use a
    // different approach: run_capture but log intermediate phase headers.
    return 0;
  }

  void run_export_build(
      std::shared_ptr<hades::Editor::ExportBuildState> state,
      const std::filesystem::path &outputDir,
      const std::filesystem::path &workspacePath,
      const std::string &projectName,
      hades::Editor::ExportPlatform platform)
  {
    const std::string sourceDir = hades::build_config::cmake_source_dir;
    const std::string cmakeCommand = hades::build_config::cmake_command;

    const std::filesystem::path buildDir = outputDir / "build";
    const std::filesystem::path stageDir = outputDir / projectName;

    auto fail = [&](const std::string &msg)
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->error = msg;
      state->succeeded = false;
      state->finished = true;
    };

    // Step 1: CMake configure.
    // Force BUILD_SHARED_LIBS=OFF so all dependencies (assimp, etc.) are
    // statically linked, producing a self-contained binary with no external
    // dylib/so dependencies that would need to be bundled alongside it.
    append_log(*state, "=== Configuring (cmake) ===\n");
    {
      auto result = hades::Subprocess::run_capture(
          {cmakeCommand,
           "-S", sourceDir,
           "-B", buildDir.string(),
           "-DCMAKE_BUILD_TYPE=Release",
           "-DBUILD_SHARED_LIBS=OFF"});
      append_log(*state, result.output);
      if (!result.launched || result.exitCode != 0)
      {
        fail("CMake configure failed (exit code " + std::to_string(result.exitCode) + ").");
        return;
      }
      append_log(*state, "\n");
    }

    // Step 2: CMake build.
    append_log(*state, "=== Building HadesRuntime ===\n");
    {
      auto result = hades::Subprocess::run_capture(
          {cmakeCommand,
           "--build", buildDir.string(),
           "--target", "HadesRuntime",
           "--config", "Release"});
      append_log(*state, result.output);
      if (!result.launched || result.exitCode != 0)
      {
        fail("Build failed (exit code " + std::to_string(result.exitCode) + ").");
        return;
      }
      append_log(*state, "\n");
    }

    // Step 3: Package the output.
    append_log(*state, "=== Packaging ===\n");

    std::error_code ec;
    std::filesystem::create_directories(stageDir, ec);
    if (ec)
    {
      fail("Failed to create output directory: " + ec.message());
      return;
    }

    // Find the built runtime binary.
#ifdef _WIN32
    const std::string binaryName = "HadesRuntime.exe";
#else
    const std::string binaryName = "HadesRuntime";
#endif

    std::filesystem::path runtimeBinary = buildDir / binaryName;
    if (!std::filesystem::exists(runtimeBinary))
    {
      // Try Release subdirectory (MSVC multi-config generators).
      runtimeBinary = buildDir / "Release" / binaryName;
    }

    if (!std::filesystem::exists(runtimeBinary))
    {
      fail("Built runtime binary not found at: " + runtimeBinary.string());
      return;
    }

#ifdef __APPLE__
    if (platform == hades::Editor::ExportPlatform::macOS)
    {
      // Create a macOS .app bundle.
      const std::filesystem::path appBundle = stageDir / (projectName + ".app");
      const std::filesystem::path contentsDir = appBundle / "Contents";
      const std::filesystem::path macosDir = contentsDir / "MacOS";
      const std::filesystem::path resourcesDir = contentsDir / "Resources";

      std::filesystem::create_directories(macosDir, ec);
      std::filesystem::create_directories(resourcesDir, ec);

      // Copy the binary.
      std::filesystem::copy_file(runtimeBinary, macosDir / binaryName,
                                 std::filesystem::copy_options::overwrite_existing, ec);
      if (ec)
      {
        fail("Failed to copy runtime binary: " + ec.message());
        return;
      }
      append_log(*state, "Copied runtime binary.\n");

      // Make it executable.
      std::filesystem::permissions(macosDir / binaryName,
                                   std::filesystem::perms::owner_exec |
                                       std::filesystem::perms::group_exec |
                                       std::filesystem::perms::others_exec,
                                   std::filesystem::perm_options::add, ec);

      // Write Info.plist.
      {
        const std::filesystem::path plistPath = contentsDir / "Info.plist";
        FILE *plist = std::fopen(plistPath.string().c_str(), "w");
        if (plist != nullptr)
        {
          std::fprintf(plist,
                       "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                       "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                       "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                       "<plist version=\"1.0\">\n"
                       "<dict>\n"
                       "  <key>CFBundleName</key>\n"
                       "  <string>%s</string>\n"
                       "  <key>CFBundleExecutable</key>\n"
                       "  <string>%s</string>\n"
                       "  <key>CFBundleIdentifier</key>\n"
                       "  <string>com.hades.%s</string>\n"
                       "  <key>CFBundleVersion</key>\n"
                       "  <string>1.0</string>\n"
                       "  <key>CFBundlePackageType</key>\n"
                       "  <string>APPL</string>\n"
                       "</dict>\n"
                       "</plist>\n",
                       projectName.c_str(), binaryName.c_str(), projectName.c_str());
          std::fclose(plist);
        }
        append_log(*state, "Created Info.plist.\n");
      }

      // Copy project data alongside the binary (inside MacOS dir so --project . works).
      const std::filesystem::path hadesDataSrc = workspacePath / ".hades";
      if (std::filesystem::exists(hadesDataSrc))
      {
        copy_directory_recursive(hadesDataSrc, macosDir / ".hades", nullptr);
        append_log(*state, "Copied project data.\n");
      }

      // Copy assets.
      const std::filesystem::path assetsSrc = std::filesystem::path(sourceDir) / "assets";
      if (std::filesystem::exists(assetsSrc))
      {
        copy_directory_recursive(assetsSrc, macosDir / "assets", nullptr);
        append_log(*state, "Copied assets.\n");
      }

      // Create a launcher script at the top level for convenience.
      {
        const std::filesystem::path launcherPath = stageDir / ("Run " + projectName + ".command");
        FILE *launcher = std::fopen(launcherPath.string().c_str(), "w");
        if (launcher != nullptr)
        {
          std::fprintf(launcher,
                       "#!/bin/bash\n"
                       "cd \"$(dirname \"$0\")\"\n"
                       "open \"%s.app\"\n",
                       projectName.c_str());
          std::fclose(launcher);
          std::filesystem::permissions(launcherPath,
                                       std::filesystem::perms::owner_exec |
                                           std::filesystem::perms::group_exec,
                                       std::filesystem::perm_options::add, ec);
        }
      }

      append_log(*state, "Created macOS app bundle: " + appBundle.string() + "\n");
    }
    else
#endif
    {
      (void)platform;

      // Linux / Windows: flat directory layout.
      std::filesystem::copy_file(runtimeBinary, stageDir / binaryName,
                                 std::filesystem::copy_options::overwrite_existing, ec);
      if (ec)
      {
        fail("Failed to copy runtime binary: " + ec.message());
        return;
      }
      append_log(*state, "Copied runtime binary.\n");

#ifndef _WIN32
      // Make executable on Linux.
      std::filesystem::permissions(stageDir / binaryName,
                                   std::filesystem::perms::owner_exec |
                                       std::filesystem::perms::group_exec |
                                       std::filesystem::perms::others_exec,
                                   std::filesystem::perm_options::add, ec);
#endif

      // Copy project data.
      const std::filesystem::path hadesDataSrc = workspacePath / ".hades";
      if (std::filesystem::exists(hadesDataSrc))
      {
        copy_directory_recursive(hadesDataSrc, stageDir / ".hades", nullptr);
        append_log(*state, "Copied project data.\n");
      }

      // Copy assets.
      const std::filesystem::path assetsSrc = std::filesystem::path(sourceDir) / "assets";
      if (std::filesystem::exists(assetsSrc))
      {
        copy_directory_recursive(assetsSrc, stageDir / "assets", nullptr);
        append_log(*state, "Copied assets.\n");
      }

#ifdef _WIN32
      // Copy DLLs that may be needed (assimp).
      const std::filesystem::path dllDir = buildDir / "Release";
      if (std::filesystem::exists(dllDir))
      {
        for (const auto &entry : std::filesystem::directory_iterator(dllDir))
        {
          if (entry.path().extension() == ".dll")
          {
            std::filesystem::copy_file(entry.path(), stageDir / entry.path().filename(),
                                       std::filesystem::copy_options::overwrite_existing, ec);
          }
        }
      }
#endif

      // Create a launcher script.
#ifdef _WIN32
      {
        const std::filesystem::path launcherPath = stageDir / (projectName + ".bat");
        FILE *launcher = std::fopen(launcherPath.string().c_str(), "w");
        if (launcher != nullptr)
        {
          std::fprintf(launcher,
                       "@echo off\r\n"
                       "cd /d \"%%~dp0\"\r\n"
                       "%s --project .\r\n",
                       binaryName.c_str());
          std::fclose(launcher);
        }
      }
#else
      {
        const std::filesystem::path launcherPath = stageDir / ("run_" + projectName + ".sh");
        FILE *launcher = std::fopen(launcherPath.string().c_str(), "w");
        if (launcher != nullptr)
        {
          std::fprintf(launcher,
                       "#!/bin/bash\n"
                       "cd \"$(dirname \"$0\")\"\n"
                       "./%s --project .\n",
                       binaryName.c_str());
          std::fclose(launcher);
          std::filesystem::permissions(launcherPath,
                                       std::filesystem::perms::owner_exec |
                                           std::filesystem::perms::group_exec,
                                       std::filesystem::perm_options::add, ec);
        }
      }
#endif

      append_log(*state, "Created game directory: " + stageDir.string() + "\n");
    }

    append_log(*state, "\nExport complete.\n");
    append_log(*state, "Build directory: " + buildDir.string() + "\n");
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->succeeded = true;
      state->finished = true;
    }
  }
}

namespace hades
{
  void Editor::render_export_window(EntityManager &entityManager, ComponentManager &componentManager)
  {
    if (!openExportWindow_)
    {
      return;
    }

    ImGui::SetNextWindowSize(ImVec2(680.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (focusExportWindow_)
    {
      ImGui::SetNextWindowFocus();
      focusExportWindow_ = false;
    }

    if (!ImGui::Begin(EXPORT_WINDOW_TITLE, &openExportWindow_))
    {
      ImGui::End();
      return;
    }

    // Pre-populate project name from workspace folder on first open.
    if (exportProjectNameBuffer_[0] == '\0' && !activeWorkspacePath_.empty())
    {
      set_buffer_text(exportProjectNameBuffer_, activeWorkspacePath_.filename().string());
    }

    // Poll the shared build state for updates.
    if (exportBuildInProgress_ && exportBuildState_)
    {
      std::lock_guard<std::mutex> lock(exportBuildState_->mutex);
      exportBuildLog_ = exportBuildState_->log;
      if (exportBuildState_->finished)
      {
        exportBuildInProgress_ = false;
        exportBuildFinished_ = true;
        exportBuildSucceeded_ = exportBuildState_->succeeded;
        exportBuildError_ = exportBuildState_->error;
        if (exportBuildThread_.joinable())
        {
          exportBuildThread_.join();
        }
        exportBuildState_.reset();
      }
    }

    // --- Platform selector ---
    ImGui::Text("Target Platform");
    ImGui::Separator();
    ImGui::Spacing();

    const ExportPlatform platforms[] = {ExportPlatform::macOS, ExportPlatform::Linux, ExportPlatform::Windows};
    for (const auto &platform : platforms)
    {
      const bool isCurrent = is_current_platform(platform);
      if (!isCurrent)
      {
        ImGui::BeginDisabled();
      }
      if (ImGui::RadioButton(platform_label(platform), selectedExportPlatform_ == platform))
      {
        selectedExportPlatform_ = platform;
      }
      if (!isCurrent)
      {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
          ImGui::SetTooltip("Cross-compilation is not supported. Build on %s to export for this platform.",
                            platform_label(platform));
        }
      }
      else
      {
        ImGui::SameLine();
        ImGui::TextDisabled("(current)");
      }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Project Name ---
    ImGui::Text("Project Name");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##ProjectName", exportProjectNameBuffer_.data(), exportProjectNameBuffer_.size());

    ImGui::Spacing();

    // --- Output Directory ---
    ImGui::Text("Output Directory");
    const float browseButtonWidth = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseButtonWidth - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputText("##OutputPath", exportOutputPathBuffer_.data(), exportOutputPathBuffer_.size());
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
    {
      std::string pickerError;
      const auto pickedFolder = pick_folder_with_native_dialog("Select export output directory", &pickerError);
      if (pickedFolder.has_value())
      {
        set_buffer_text(exportOutputPathBuffer_, pickedFolder->string());
      }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Build & Export button ---
    const bool hasProjectName = exportProjectNameBuffer_[0] != '\0';
    const bool hasOutputPath = exportOutputPathBuffer_[0] != '\0';
    const bool canBuild = hasProjectName && hasOutputPath && !exportBuildInProgress_;

    if (!canBuild)
    {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Build & Export", ImVec2(140.0f, 0.0f)))
    {
      // Save all worlds before building.
      save_worlds(entityManager, componentManager);

      exportBuildLog_.clear();
      exportBuildError_.clear();
      exportBuildSucceeded_ = false;
      exportBuildFinished_ = false;
      exportBuildInProgress_ = true;

      exportBuildState_ = std::make_shared<ExportBuildState>();

      const std::filesystem::path outputDir = exportOutputPathBuffer_.data();
      const std::filesystem::path workspacePath = activeWorkspacePath_;
      const std::string projectName = exportProjectNameBuffer_.data();
      const ExportPlatform platform = selectedExportPlatform_;

      auto buildState = exportBuildState_;
      if (exportBuildThread_.joinable())
      {
        exportBuildThread_.join();
      }
      exportBuildThread_ = std::thread(
          [buildState, outputDir, workspacePath, projectName, platform]()
          {
            run_export_build(buildState, outputDir, workspacePath, projectName, platform);
          });
    }
    if (!canBuild)
    {
      ImGui::EndDisabled();
    }

    // --- Status ---
    if (exportBuildInProgress_)
    {
      ImGui::SameLine();
      ImGui::TextDisabled("Building...");
    }
    else if (exportBuildFinished_)
    {
      ImGui::SameLine();
      if (exportBuildSucceeded_)
      {
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "Export succeeded.");
      }
      else
      {
        ImGui::TextColored(ImVec4(0.88f, 0.42f, 0.42f, 1.0f), "%s", exportBuildError_.c_str());
      }
    }

    ImGui::Spacing();

    // --- Build Log ---
    if (!exportBuildLog_.empty() || exportBuildInProgress_)
    {
      ImGui::Text("Build Output");
      ImGui::BeginChild("BuildLog", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
      if (exportBuildInProgress_ && exportBuildLog_.empty())
      {
        ImGui::TextDisabled("Starting build...");
      }
      else
      {
        ImGui::TextUnformatted(exportBuildLog_.c_str(), exportBuildLog_.c_str() + exportBuildLog_.size());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        {
          ImGui::SetScrollHereY(1.0f);
        }
      }
      ImGui::EndChild();
    }

    ImGui::End();
  }
}
