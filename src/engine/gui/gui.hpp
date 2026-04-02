#ifndef GUI_H
#define GUI_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hades
{
  struct MenuBarItem
  {
    std::string title;
    std::vector<MenuBarItem> children_menu_items;
    std::function<void()> on_activate;
  };

  class GUI
  {
  public:
    std::vector<MenuBarItem> menu_bar_items;

    explicit GUI() = default;

    virtual std::uint32_t render_frame() = 0;
    virtual ~GUI() = default;
  };
}

#endif
