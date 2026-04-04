#include <cstdlib>
#include <filesystem>
#include <string>

#include <CLI/CLI.hpp>

#include "game_runtime.hpp"

int main(int argc, char **argv)
{
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
}
