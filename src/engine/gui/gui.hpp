#ifndef GUI_H
#define GUI_H

#include <string>
#include <vector>

struct MenuBarItem
{
  std::string title;
  std::vector<MenuBarItem> children_menu_items;
};

class GUI
{
public:
  std::vector<MenuBarItem> menu_bar_items;

  explicit GUI() {}

  virtual void render_frame() {}

  ~GUI()
  {
  }
};

#endif
