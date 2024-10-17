#ifndef GUI_H
#define GUI_H

#include <string>
#include <vector>

namespace hades
{
  struct MenuBarItem
  {
    std::string title;
    std::vector<MenuBarItem> children_menu_items;
  };

  class GUI
  {
  public:
    std::vector<MenuBarItem> menu_bar_items;

    explicit GUI() = default;

    virtual void render_frame() = 0;
    virtual ~GUI() = default;
  };
}

#endif
