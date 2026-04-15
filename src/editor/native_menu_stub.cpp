#include "native_menu.hpp"

#include "../engine/gui/gui.hpp"

namespace hades::native_menu
{
  bool is_available()
  {
    return false;
  }

  void sync(const std::vector<MenuBarItem> &)
  {
  }

  void teardown()
  {
  }
}
