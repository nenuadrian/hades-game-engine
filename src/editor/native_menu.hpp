#ifndef HADES_EDITOR_NATIVE_MENU_HPP
#define HADES_EDITOR_NATIVE_MENU_HPP

#include <vector>

namespace hades
{
  struct MenuBarItem;

  namespace native_menu
  {
    // Returns true if the native platform menu bar (e.g. macOS global menu
    // bar) is available on this platform. When false, callers should fall
    // back to the in-window ImGui menu bar.
    bool is_available();

    // Sync the native menu bar with the given item tree. Safe to call every
    // frame; the implementation diffs internally and only rebuilds the
    // platform menu when the structure or item state actually changes.
    void sync(const std::vector<MenuBarItem> &items);

    // Tear down any native menus owned by this bridge. Safe to call even if
    // nothing has been built yet.
    void teardown();
  }
}

#endif
