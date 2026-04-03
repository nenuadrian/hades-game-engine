#ifndef HADES_ENGINE_PHYSICS_MATH_HPP
#define HADES_ENGINE_PHYSICS_MATH_HPP

#include <Jolt/Jolt.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>

#include "../components/position_component_3d.hpp"
#include "../components/rotation_component_3d.hpp"

namespace hades::physics
{
  inline JPH::Vec3 to_jph(const PositionComponent3D &p)
  {
    return JPH::Vec3(p.x, p.y, p.z);
  }

  inline void from_jph(const JPH::Vec3 &v, PositionComponent3D &p)
  {
    p.x = v.GetX();
    p.y = v.GetY();
    p.z = v.GetZ();
  }

  inline JPH::Quat to_jph(const RotationComponent3D &r)
  {
    return JPH::Quat(r.qx, r.qy, r.qz, r.qw);
  }

  inline void from_jph(const JPH::Quat &q, RotationComponent3D &r)
  {
    r.qx = q.GetX();
    r.qy = q.GetY();
    r.qz = q.GetZ();
    r.qw = q.GetW();
  }
}

#endif
