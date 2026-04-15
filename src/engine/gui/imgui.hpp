#ifndef GUI_IMGUI_H
#define GUI_IMGUI_H

#include "gui.hpp"

namespace hades
{
  class ImGui_GUI : public GUI
  {
  public:
    std::uint32_t render_frame() override;

    // When true, render_frame() skips drawing the in-window main menu bar.
    // Used when a native platform menu bar (e.g. the macOS system menu bar)
    // is rendering the menu instead.
    void set_suppress_main_menu_bar(bool suppress) { suppress_main_menu_bar_ = suppress; }

  private:
    bool suppress_main_menu_bar_ = false;
  };
}
#endif
