#ifndef HADES_ENGINE_COMPONENTS_COLLIDER_COMPONENT_HPP
#define HADES_ENGINE_COMPONENTS_COLLIDER_COMPONENT_HPP

namespace hades
{
  enum class ColliderShape
  {
    Box,
    Sphere,
    Capsule
  };

  struct ColliderComponent
  {
    ColliderShape shape = ColliderShape::Box;

    // Box half-extents.
    float halfExtentX = 0.5f;
    float halfExtentY = 0.5f;
    float halfExtentZ = 0.5f;

    // Sphere radius.
    float radius = 0.5f;

    // Capsule dimensions.
    float capsuleHalfHeight = 0.5f;
    float capsuleRadius = 0.25f;
  };
}

#endif
