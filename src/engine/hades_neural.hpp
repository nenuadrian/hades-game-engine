#ifndef HADES_ENGINE_HADES_NEURAL_HPP
#define HADES_ENGINE_HADES_NEURAL_HPP

// Convenience aggregate header for user scripts that want to participate in
// reinforcement learning. Pull this in instead of the individual files —
// mirror of `hades.hpp` for the neural API surface.

#include "runtime/hades_neural_script.hpp"
#include "runtime/hades_neural_api.hpp"

#include <hne/core/types.hpp>

namespace hades
{
  // Re-export commonly used HNE types into the `hades` namespace so user
  // scripts can write `hades::BoxSpace` without an extra `using` line.
  using Tensor = ::hne::Tensor;
  using Action = ::hne::Action;
  using SpaceSpec = ::hne::SpaceSpec;
  using DiscreteSpace = ::hne::DiscreteSpace;
  using MultiDiscreteSpace = ::hne::MultiDiscreteSpace;
  using BoxSpace = ::hne::BoxSpace;

  using ::hne::flat_size;

  // Lift helpers from `hades::neural` into `hades::` for terse user scripts.
  using neural::normalize;
  using neural::unnormalize;
  using neural::write_obs;
  using neural::write_vec3;
}

#endif
