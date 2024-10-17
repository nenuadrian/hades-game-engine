#include <CLI/CLI.hpp>
#include "editor/window_manager.hpp"

int main(int argc, char **argv)
{
  CLI::App app{"Hades"};

  CLI11_PARSE(app, argc, argv);

  WindowManager window_manager;
  window_manager.init();

  while (window_manager.running)
  {
    window_manager.render_frame();
  }

  window_manager.cleanup();

  return 0;
}
