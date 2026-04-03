#ifndef HADES_ENGINE_COMPONENTS_RIGID_BODY_COMPONENT_HPP
#define HADES_ENGINE_COMPONENTS_RIGID_BODY_COMPONENT_HPP

#include <cstdint>

namespace hades
{
  enum class RigidBodyType
  {
    Static,
    Kinematic,
    Dynamic
  };

  struct RigidBodyComponent
  {
    RigidBodyType type = RigidBodyType::Dynamic;
    float mass = 1.0f;
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    float friction = 0.2f;
    float restitution = 0.0f;
    float gravityScale = 1.0f;

    // Runtime state managed by PhysicsSystem. Not serialized.
    std::uint32_t bodyIdValue = 0xFFFFFFFF;
    bool bodyCreated = false;
  };
}

#endif
