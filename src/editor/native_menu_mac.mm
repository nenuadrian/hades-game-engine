#include "native_menu.hpp"

#include "../engine/gui/gui.hpp"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hades::native_menu
{
  namespace
  {
    // Identifier attached to every top-level NSMenuItem we add to the main
    // menu. Lets us distinguish our items from those owned by SDL / AppKit
    // (the app menu) when removing them during a rebuild.
    NSString *const kManagedIdentifier = @"hades.managed";

    struct BridgeState
    {
      std::size_t structural_hash = 0;
      bool built = false;
      std::vector<std::function<void()>> callbacks;
    };

    BridgeState g_state;

    // Strip characters in the Unicode Private Use Area (U+E000..U+F8FF),
    // where Font Awesome glyphs live, and trim surrounding whitespace. The
    // glyphs render as garbage in the system menu bar because AppKit uses
    // the OS font, not our bundled icon font.
    std::string clean_title(const std::string &input)
    {
      std::string out;
      out.reserve(input.size());

      const unsigned char *data = reinterpret_cast<const unsigned char *>(input.data());
      const std::size_t size = input.size();
      std::size_t i = 0;
      while (i < size)
      {
        const unsigned char b = data[i];
        std::uint32_t cp = 0;
        std::size_t len = 1;
        if (b < 0x80)
        {
          cp = b;
          len = 1;
        }
        else if ((b & 0xE0) == 0xC0 && i + 1 < size)
        {
          cp = ((b & 0x1Fu) << 6) | (data[i + 1] & 0x3Fu);
          len = 2;
        }
        else if ((b & 0xF0) == 0xE0 && i + 2 < size)
        {
          cp = ((b & 0x0Fu) << 12) | ((data[i + 1] & 0x3Fu) << 6) | (data[i + 2] & 0x3Fu);
          len = 3;
        }
        else if ((b & 0xF8) == 0xF0 && i + 3 < size)
        {
          cp = ((b & 0x07u) << 18) | ((data[i + 1] & 0x3Fu) << 12) |
               ((data[i + 2] & 0x3Fu) << 6) | (data[i + 3] & 0x3Fu);
          len = 4;
        }

        if (!(cp >= 0xE000u && cp <= 0xF8FFu))
        {
          out.append(input, i, len);
        }
        i += len;
      }

      const auto not_space = [](unsigned char c) { return !std::isspace(c); };
      const auto first = std::find_if(out.begin(), out.end(), not_space);
      const auto last = std::find_if(out.rbegin(), out.rend(), not_space).base();
      if (first >= last)
      {
        return std::string();
      }
      return std::string(first, last);
    }

    void hash_combine(std::size_t &seed, std::size_t value)
    {
      seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    }

    std::size_t compute_hash(const std::vector<MenuBarItem> &items)
    {
      std::size_t h = items.size();
      for (const auto &item : items)
      {
        hash_combine(h, std::hash<std::string>{}(clean_title(item.title)));
        hash_combine(h, item.children_menu_items.empty() ? 0u : 1u);
        hash_combine(h, item.enabled ? 1u : 0u);
        hash_combine(h, item.selected ? 1u : 0u);
        hash_combine(h, static_cast<bool>(item.on_activate) ? 1u : 0u);
        hash_combine(h, compute_hash(item.children_menu_items));
      }
      return h;
    }

    // Walk the tree in the same order that build_submenu() visits leaves,
    // pushing each leaf's callback onto g_state.callbacks. Used on the
    // fast path when structure is unchanged but closures may have captured
    // fresh references this frame.
    void collect_leaf_callbacks(const std::vector<MenuBarItem> &items)
    {
      for (const auto &item : items)
      {
        if (!item.children_menu_items.empty())
        {
          collect_leaf_callbacks(item.children_menu_items);
        }
        else
        {
          g_state.callbacks.push_back(item.on_activate);
        }
      }
    }
  }
}

// Obj-C dispatcher — single target for every managed NSMenuItem. Each item's
// representedObject is an NSNumber index into g_state.callbacks.
@interface HadesMenuDispatcher : NSObject
- (void)activate:(NSMenuItem *)sender;
@end

@implementation HadesMenuDispatcher
- (void)activate:(NSMenuItem *)sender
{
  id obj = [sender representedObject];
  if (![obj isKindOfClass:[NSNumber class]])
  {
    return;
  }
  const NSUInteger idx = [(NSNumber *)obj unsignedIntegerValue];
  auto &callbacks = hades::native_menu::g_state.callbacks;
  if (idx >= callbacks.size())
  {
    return;
  }
  if (auto cb = callbacks[idx])
  {
    cb();
  }
}
@end

namespace hades::native_menu
{
  namespace
  {
    HadesMenuDispatcher *g_dispatcher = nil;

    NSString *ns_string(const std::string &value)
    {
      NSString *s = [NSString stringWithUTF8String:value.c_str()];
      return s != nil ? s : @"";
    }

    NSMenu *build_submenu(const std::vector<MenuBarItem> &items, NSString *title)
    {
      NSMenu *menu = [[NSMenu alloc] initWithTitle:title ?: @""];
      menu.autoenablesItems = NO;

      for (const auto &item : items)
      {
        NSString *itemTitle = ns_string(clean_title(item.title));
        NSMenuItem *nsItem = nil;

        if (!item.children_menu_items.empty())
        {
          nsItem = [[NSMenuItem alloc] initWithTitle:itemTitle
                                              action:nil
                                       keyEquivalent:@""];
          nsItem.submenu = build_submenu(item.children_menu_items, itemTitle);
        }
        else
        {
          nsItem = [[NSMenuItem alloc] initWithTitle:itemTitle
                                              action:@selector(activate:)
                                       keyEquivalent:@""];
          nsItem.target = g_dispatcher;
          nsItem.representedObject = @(g_state.callbacks.size());
          g_state.callbacks.push_back(item.on_activate);
        }

        nsItem.enabled = item.enabled ? YES : NO;
        nsItem.state = item.selected ? NSControlStateValueOn : NSControlStateValueOff;
        [menu addItem:nsItem];
      }

      return menu;
    }

    void remove_managed_items(NSMenu *mainMenu)
    {
      for (NSInteger i = mainMenu.numberOfItems - 1; i >= 0; --i)
      {
        NSMenuItem *existing = [mainMenu itemAtIndex:i];
        if ([existing.identifier isEqualToString:kManagedIdentifier])
        {
          [mainMenu removeItemAtIndex:i];
        }
      }
    }
  }

  bool is_available()
  {
    return true;
  }

  void sync(const std::vector<MenuBarItem> &items)
  {
    @autoreleasepool
    {
      if (NSApp == nil)
      {
        return;
      }
      if (g_dispatcher == nil)
      {
        g_dispatcher = [[HadesMenuDispatcher alloc] init];
      }

      const std::size_t newHash = compute_hash(items);

      // Callbacks are rebuilt every frame — the closures in MenuBarItem may
      // capture references that moved since the previous sync.
      g_state.callbacks.clear();

      if (g_state.built && newHash == g_state.structural_hash)
      {
        // Fast path: NSMenu structure still matches. Just refresh the
        // callback table in the same leaf order build_submenu uses so
        // existing NSMenuItem representedObject indices remain valid.
        collect_leaf_callbacks(items);
        return;
      }

      NSMenu *mainMenu = [NSApp mainMenu];
      if (mainMenu == nil)
      {
        mainMenu = [[NSMenu alloc] initWithTitle:@""];
        [NSApp setMainMenu:mainMenu];
      }

      remove_managed_items(mainMenu);

      for (const auto &item : items)
      {
        NSString *title = ns_string(clean_title(item.title));
        NSMenuItem *topItem = [[NSMenuItem alloc] initWithTitle:title
                                                         action:nil
                                                  keyEquivalent:@""];
        topItem.identifier = kManagedIdentifier;
        topItem.submenu = build_submenu(item.children_menu_items, title);
        topItem.enabled = item.enabled ? YES : NO;
        [mainMenu addItem:topItem];
      }

      g_state.structural_hash = newHash;
      g_state.built = true;
    }
  }

  void teardown()
  {
    @autoreleasepool
    {
      if (NSApp != nil)
      {
        if (NSMenu *mainMenu = [NSApp mainMenu])
        {
          remove_managed_items(mainMenu);
        }
      }
      g_state.callbacks.clear();
      g_state.structural_hash = 0;
      g_state.built = false;
    }
  }
}
