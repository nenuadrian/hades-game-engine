#include <iostream>
#include <chrono>
#include <CLI/CLI.hpp>
#include "editor/window_manager.hpp"

// Main code
int main(int, char **)
{
  WindowManager window_manager;
  window_manager.init();

  while (window_manager.running)
  {
    window_manager.render_frame();
  }

  window_manager.cleanup();

  return 0;
}
