#include <cstdlib>
#include <filesystem>
#include <string>

#include <CLI/CLI.hpp>

#include "game_runtime.hpp"

int main(int argc, char **argv)
{
  CLI::App app{"Hades Game Runtime"};
  std::string projectPath;
  app.add_option("-p,--project", projectPath, "Path to the project directory")->required();
  CLI11_PARSE(app, argc, argv);

  hades::GameRuntime runtime;
  if (!runtime.init(std::filesystem::path(projectPath)))
  {
    return EXIT_FAILURE;
  }

  return runtime.run();
}
