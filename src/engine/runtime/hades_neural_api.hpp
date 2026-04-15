#ifndef HADES_ENGINE_RUNTIME_HADES_NEURAL_API_HPP
#define HADES_ENGINE_RUNTIME_HADES_NEURAL_API_HPP

#include <algorithm>
#include <cstdint>

#include <hne/core/types.hpp>

#include "../rendering/math3d.hpp"

namespace hades::neural
{
  /// Map `v ∈ [lo, hi]` into `[-1, 1]`. Clamped so spurious out-of-range
  /// values (e.g. a position that briefly exits the arena) don't blow up
  /// the downstream policy.
  inline float normalize(float v, float lo, float hi)
  {
    if (hi <= lo)
    {
      return 0.0f;
    }
    const float t = (v - lo) / (hi - lo);
    const float u = 2.0f * t - 1.0f;
    return std::clamp(u, -1.0f, 1.0f);
  }

  /// Inverse of `normalize` — useful when an action comes back in `[-1, 1]`
  /// and needs to be projected onto a game-space range.
  inline float unnormalize(float u, float lo, float hi)
  {
    const float clamped = std::clamp(u, -1.0f, 1.0f);
    const float t = 0.5f * (clamped + 1.0f);
    return lo + t * (hi - lo);
  }

  /// Write `v` into `tensor.data[i]` guarded against out-of-range index.
  /// Silent no-op on OOB — scripts run in a tight loop; a std::terminate here
  /// would take down training without a useful message.
  inline void write_obs(hne::Tensor &tensor, int32_t i, float v)
  {
    if (i < 0)
    {
      return;
    }
    const auto idx = static_cast<std::size_t>(i);
    if (idx < tensor.data.size())
    {
      tensor.data[idx] = v;
    }
  }

  /// Write a Vec3 into three consecutive observation slots starting at `base`.
  /// Ordering is x, y, z. Combine with `normalize()` when the game-space range
  /// is bounded.
  inline void write_vec3(hne::Tensor &tensor, int32_t base, const math::Vec3 &v)
  {
    write_obs(tensor, base + 0, v.x);
    write_obs(tensor, base + 1, v.y);
    write_obs(tensor, base + 2, v.z);
  }
}

#endif
