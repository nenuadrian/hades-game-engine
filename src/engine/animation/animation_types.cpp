#include "animation_types.hpp"

#include <algorithm>
#include <cmath>

namespace hades
{
  namespace
  {
    struct InterpolationNameEntry
    {
      const char *name;
      Interpolation value;
    };

    // Single source of truth for both directions of the mapping, so the
    // serialised name and the parser can never drift apart.
    constexpr InterpolationNameEntry kInterpolationNames[] = {
        {"step", Interpolation::Step},
        {"linear", Interpolation::Linear},
        {"easeIn", Interpolation::EaseIn},
        {"easeOut", Interpolation::EaseOut},
        {"easeInOut", Interpolation::EaseInOut},
        {"bezier", Interpolation::Bezier},
    };

    /// One axis of a cubic bezier with the implicit endpoints (0,0) and
    /// (1,1), expanded into the polynomial form CSS uses:
    /// B(u) = ((a*u + b)*u + c)*u.
    struct BezierAxis
    {
      float a = 0.0f;
      float b = 0.0f;
      float c = 0.0f;
    };

    BezierAxis bezier_axis(float p1, float p2)
    {
      const float c = 3.0f * p1;
      const float b = 3.0f * (p2 - p1) - c;
      return BezierAxis{1.0f - c - b, b, c};
    }

    float bezier_value(const BezierAxis &axis, float u)
    {
      return ((axis.a * u + axis.b) * u + axis.c) * u;
    }

    float bezier_slope(const BezierAxis &axis, float u)
    {
      return (3.0f * axis.a * u + 2.0f * axis.b) * u + axis.c;
    }

    /// Solve x(u) = t for the curve parameter u, then evaluate y(u).
    ///
    /// This is the one easing kernel: the named presets feed it the CSS
    /// control points, so every mode agrees on the shape of a curve and a
    /// key can be switched between "easeInOut" and an authored bezier
    /// without the motion jumping.
    float solve_bezier(float x1, float y1, float x2, float y2, float t)
    {
      // Control points outside [0,1] on x make x(u) non-monotonic, which
      // leaves the solve with no unique answer. y is deliberately left
      // unclamped so overshoot / anticipation curves still work.
      x1 = std::clamp(x1, 0.0f, 1.0f);
      x2 = std::clamp(x2, 0.0f, 1.0f);

      const BezierAxis xAxis = bezier_axis(x1, x2);
      const BezierAxis yAxis = bezier_axis(y1, y2);

      constexpr float kEpsilon = 1e-6f;

      float u = t;
      bool solved = false;
      for (int i = 0; i < 8; ++i)
      {
        const float error = bezier_value(xAxis, u) - t;
        if (std::fabs(error) < kEpsilon)
        {
          solved = true;
          break;
        }

        const float slope = bezier_slope(xAxis, u);
        if (std::fabs(slope) < kEpsilon)
        {
          // Flat spot: Newton would explode, hand the segment to bisection.
          break;
        }
        u -= error / slope;
      }

      if (!solved || u < 0.0f || u > 1.0f)
      {
        // x(u) is monotonic once the control points are clamped, so plain
        // bisection always converges on the bracketed root.
        float low = 0.0f;
        float high = 1.0f;
        u = std::clamp(t, 0.0f, 1.0f);
        for (int i = 0; i < 20; ++i)
        {
          const float x = bezier_value(xAxis, u);
          if (std::fabs(x - t) < kEpsilon)
          {
            break;
          }
          if (x > t)
          {
            high = u;
          }
          else
          {
            low = u;
          }
          u = (low + high) * 0.5f;
        }
      }

      return bezier_value(yAxis, u);
    }
  }

  const char *interpolation_name(Interpolation interpolation)
  {
    for (const auto &entry : kInterpolationNames)
    {
      if (entry.value == interpolation)
      {
        return entry.name;
      }
    }
    return "linear";
  }

  bool interpolation_from_name(const std::string &name, Interpolation &out)
  {
    for (const auto &entry : kInterpolationNames)
    {
      if (name == entry.name)
      {
        out = entry.value;
        return true;
      }
    }
    return false;
  }

  const char *track_channel_name(TrackChannel channel)
  {
    switch (channel)
    {
    case TrackChannel::Translation:
      return "translation";
    case TrackChannel::Rotation:
      return "rotation";
    case TrackChannel::Scale:
      return "scale";
    }
    return "translation";
  }

  float apply_easing(Interpolation interpolation, float t, const EaseCurve &curve)
  {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    switch (interpolation)
    {
    case Interpolation::Step:
      // Hold the segment's start value until the next key takes over.
      return 0.0f;
    case Interpolation::Linear:
      return clamped;
    case Interpolation::EaseIn:
      return solve_bezier(0.42f, 0.0f, 1.0f, 1.0f, clamped);
    case Interpolation::EaseOut:
      return solve_bezier(0.0f, 0.0f, 0.58f, 1.0f, clamped);
    case Interpolation::EaseInOut:
      return solve_bezier(0.42f, 0.0f, 0.58f, 1.0f, clamped);
    case Interpolation::Bezier:
      return solve_bezier(curve.x1, curve.y1, curve.x2, curve.y2, clamped);
    }
    return clamped;
  }

  void Pose::resize(std::size_t jointCount)
  {
    // Container semantics: existing joints keep their value, new ones get
    // the identity TRS. The scale fill is the one that matters — the
    // default-constructed Vec3 is zero, and a zero scale collapses every
    // matrix later derived from the pose.
    translations.resize(jointCount, math::Vec3{0.0f, 0.0f, 0.0f});
    rotations.resize(jointCount, math::Quat{});
    scales.resize(jointCount, math::Vec3{1.0f, 1.0f, 1.0f});
  }

  void Pose::clear()
  {
    translations.clear();
    rotations.clear();
    scales.clear();
  }

  math::Mat4 Pose::localMatrix(std::size_t joint) const
  {
    if (joint >= translations.size() || joint >= rotations.size() || joint >= scales.size())
    {
      return math::Mat4::identity();
    }
    return math::buildModelMatrix(translations[joint], rotations[joint], scales[joint]);
  }
}
