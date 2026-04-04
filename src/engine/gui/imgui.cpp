#include "imgui.hpp"

#include "imgui.h"

namespace hades
{
  namespace
  {
    void render_menu_item(const MenuBarItem &item)
    {
      if (!item.children_menu_items.empty())
      {
        if (ImGui::BeginMenu(item.title.c_str(), item.enabled))
        {
          for (const auto &child : item.children_menu_items)
          {
            render_menu_item(child);
          }
          ImGui::EndMenu();
        }
      }
      else
      {
        if (ImGui::MenuItem(
                item.title.c_str(),
                nullptr,
                item.selected,
                item.enabled))
        {
          if (item.on_activate)
          {
            item.on_activate();
          }
        }
      }
    }
  }

  std::uint32_t ImGui_GUI::render_frame()
  {
    const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    if (ImGui::BeginMainMenuBar())
    {
      for (const auto &item : menu_bar_items)
      {
        if (ImGui::BeginMenu(item.title.c_str()))
        {
          for (const auto &child_item : item.children_menu_items)
          {
            render_menu_item(child_item);
          }
          ImGui::EndMenu();
        }
      }

      ImGui::EndMainMenuBar();
    }

    return dockspaceId;
  }
}
