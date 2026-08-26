#ifndef HADES_ENGINE_UI_UI_BLUEPRINT_NODES_HPP
#define HADES_ENGINE_UI_UI_BLUEPRINT_NODES_HPP

namespace hades
{
  /// Registers the "UI" Blueprint node category (ui.set_text, ui.set_value,
  /// ui.set_visible, ...). Idempotent; called from the tail of
  /// register_builtin_blueprint_nodes() so callers never have to remember.
  void register_ui_blueprint_nodes();
}

#endif
