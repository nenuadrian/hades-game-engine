#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include <CLI/CLI.hpp>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#include "engine/core/log.hpp"
#include "game_runtime.hpp"

namespace
{
  // Returns the absolute path of the current executable, or an empty path on failure.
  std::filesystem::path executable_path()
  {
#if defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
    {
      return {};
    }
    std::error_code ec;
    auto canonical = std::filesystem::canonical(buffer.c_str(), ec);
    return ec ? std::filesystem::path{buffer.c_str()} : canonical;
#elif defined(__linux__)
    std::error_code ec;
    auto canonical = std::filesystem::canonical("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : canonical;
#elif defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
    {
      return {};
    }
    return std::filesystem::path{std::wstring{buffer, length}};
#else
    return {};
#endif
  }

  // Try to locate the project (.hades directory) relative to the executable.
  // Returns an empty path if none is found. This is what makes double-clicking
  // a macOS .app bundle work without any CLI arguments.
  std::filesystem::path find_bundled_project_dir(const std::filesystem::path &exeDir)
  {
    if (exeDir.empty())
    {
      return {};
    }
    std::error_code ec;
    if (std::filesystem::is_directory(exeDir / ".hades", ec))
    {
      return exeDir;
    }
    // macOS bundle: binary lives at Foo.app/Contents/MacOS; project may sit at
    // Foo.app/Contents/Resources or at the bundle root. Check common spots.
    auto parent = exeDir.parent_path();
    if (!parent.empty() && std::filesystem::is_directory(parent / "Resources" / ".hades", ec))
    {
      return parent / "Resources";
    }
    return {};
  }
}

int main(int argc, char **argv)
{
  const std::filesystem::path exePath = executable_path();
  const std::filesystem::path exeDir = exePath.empty() ? std::filesystem::path{} : exePath.parent_path();

  // Route logs to a file next to the executable when HADES_DEBUG=1 or when
  // the binary is launched without stdin/stdout attached (typical for a
  // double-clicked macOS .app). This is what makes silent-startup failures
  // diagnosable.
  if (!exeDir.empty())
  {
    const char *debugEnv = std::getenv("HADES_DEBUG");
    const bool debugEnabled = debugEnv != nullptr && debugEnv[0] != '\0' && std::strcmp(debugEnv, "0") != 0;
    if (debugEnabled)
    {
      hades::Log::enable_file_logging(exeDir / "hades.log");
    }
  }

  CLI::App app{"Hades Game Runtime"};
  std::string projectPath;
  bool headless = false;
  bool apiMode = false;
  int apiPort = 7777;
  app.add_option("-p,--project", projectPath, "Path to the project directory");
  app.add_flag("--headless", headless, "Run without a window or rendering");
  app.add_flag("--api", apiMode, "Enable HadesAPI REST server for ML training");
  app.add_option("--api-port", apiPort, "Port for the HadesAPI server (default: 7777)");
  CLI11_PARSE(app, argc, argv);

  // Fallback: if --project wasn't supplied (e.g. macOS .app double-clicked),
  // locate a .hades directory next to the executable. Also chdir so all
  // relative paths in the runtime resolve against the bundle.
  if (projectPath.empty())
  {
    const std::filesystem::path bundled = find_bundled_project_dir(exeDir);
    if (!bundled.empty())
    {
      std::error_code ec;
      std::filesystem::current_path(bundled, ec);
      projectPath = ".";
      hades::Log::info("Launched from bundle; using project at %s", bundled.string().c_str());
    }
    else
    {
      hades::Log::error("No --project supplied and no .hades directory found next to the executable.");
      return EXIT_FAILURE;
    }
  }

  // API mode implies headless by default (no window needed for ML training).
  if (apiMode && !headless)
  {
    headless = true;
  }

  hades::GameRuntime runtime;
  if (!runtime.init(std::filesystem::path(projectPath), headless, apiMode, apiPort))
  {
    hades::Log::error("Runtime failed to initialize with project '%s'", projectPath.c_str());
    return EXIT_FAILURE;
  }

  return runtime.run();
}
