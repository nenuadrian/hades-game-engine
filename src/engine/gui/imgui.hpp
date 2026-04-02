#ifndef GUI_IMGUI_H
#define GUI_IMGUI_H

#include "imgui.h"

#include "gui.hpp"

namespace hades
{
  class ImGui_GUI : public GUI
  {
  public:
    void render_frame()
    {
      ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

      if (ImGui::BeginMainMenuBar())
      {
        for (const auto &item : menu_bar_items)
        {
          if (ImGui::BeginMenu(item.title.c_str()))
          {
            for (const auto &child_item : item.children_menu_items)
            {
              if (ImGui::MenuItem(child_item.title.c_str()))
              {
              }
              ImGui::EndMenu();
            }
          }
        }

        ImGui::EndMainMenuBar();
      }
    }
  };
}
#endif
