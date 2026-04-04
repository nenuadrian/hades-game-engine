#include <cstdlib>
#include <filesystem>
#include <string>

#ifndef HADES_PLATFORM_WEB
#include <CLI/CLI.hpp>
#endif

#include "game_runtime.hpp"

int main(int argc, char **argv)
{
#ifdef HADES_PLATFORM_WEB
  // On the web, assets are embedded in the virtual filesystem at /assets
  // via Emscripten's --preload-file. No CLI argument parsing is needed.
  (void)argc;
  (void)argv;
  static hades::GameRuntime runtime;
  if (!runtime.init(std::filesystem::path("/assets")))
  {
    return EXIT_FAILURE;
  }
  return runtime.run();
#else
  CLI::App app{"Hades Game Runtime"};
  std::string projectPath;
  bool headless = false;
  app.add_option("-p,--project", projectPath, "Path to the project directory")->required();
  app.add_flag("--headless", headless, "Run without a window or rendering");
  CLI11_PARSE(app, argc, argv);

  hades::GameRuntime runtime;
  if (!runtime.init(std::filesystem::path(projectPath), headless))
  {
    return EXIT_FAILURE;
  }

  return runtime.run();
#endif
}
