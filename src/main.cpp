#include <CLI/CLI.hpp>
#include "editor/window_manager.hpp"

int main(int argc, char **argv)
{
  CLI::App app{"Hades"};

  CLI11_PARSE(app, argc, argv);

  hades::WindowManager window_manager;
  return window_manager.run();
}
