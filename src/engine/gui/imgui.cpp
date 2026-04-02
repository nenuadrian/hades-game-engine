#include "imgui.hpp"

#include "imgui.h"

namespace hades
{
  void ImGui_GUI::render_frame()
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
              if (child_item.on_activate)
              {
                child_item.on_activate();
              }
            }
          }
          ImGui::EndMenu();
        }
      }

      ImGui::EndMainMenuBar();
    }
  }
}
