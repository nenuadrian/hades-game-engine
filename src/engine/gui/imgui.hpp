#ifndef GUI_IMGUI_H
#define GUI_IMGUI_H

#include "gui.hpp"

namespace hades
{
  class ImGui_GUI : public GUI
  {
  public:
    void render_frame() override;
  };
}
#endif
