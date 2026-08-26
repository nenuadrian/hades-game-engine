#ifndef HADES_ENGINE_ANIMATION_ANIMATION_BLUEPRINT_NODES_HPP
#define HADES_ENGINE_ANIMATION_ANIMATION_BLUEPRINT_NODES_HPP

// Blueprint node library for skeletal animation.
//
// Lives here rather than in `blueprint/` so the animation facade stays the
// only thing these nodes depend on: every implementation is a one-line
// forward to `hades::Animation`, which already no-ops for entities without a
// model, so no node needs to guard.

namespace hades
{
  /// Add the "Animation" category to the Blueprint node registry. Idempotent,
  /// and called from `register_builtin_blueprint_nodes()`, so callers never
  /// have to remember it.
  void register_animation_blueprint_nodes();
}

#endif
