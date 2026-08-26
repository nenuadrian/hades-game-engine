#ifndef HADES_ENGINE_RUNTIME_SCRIPT_BLUEPRINT_NODES_HPP
#define HADES_ENGINE_RUNTIME_SCRIPT_BLUEPRINT_NODES_HPP

// Blueprint node library for reaching C++ scripts — the Blueprint -> script
// direction of the bridge. (The script -> Blueprint direction is the
// `hades::Blueprints` facade in `blueprint/script_blueprint.hpp`.)
//
// Lives here rather than in `blueprint/` for the same reason the animation
// nodes live in `animation/`: the node bodies belong next to the subsystem
// they talk to, not next to the VM. Every implementation forwards through
// `BlueprintHost::send_script_message`, which is inert unless the embedder
// bound a ScriptRuntime, so a graph under unit test never needs one.

namespace hades
{
  /// Add the "Scripts" category to the Blueprint node registry. Idempotent,
  /// and called from `register_builtin_blueprint_nodes()`, so callers never
  /// have to remember it.
  void register_script_blueprint_nodes();
}

#endif
