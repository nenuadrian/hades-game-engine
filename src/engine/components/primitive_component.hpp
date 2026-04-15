#ifndef HADES_ENGINE_COMPONENTS_PRIMITIVE_COMPONENT_HPP
#define HADES_ENGINE_COMPONENTS_PRIMITIVE_COMPONENT_HPP

namespace hades
{
  enum class PrimitiveType
  {
    Cube,
    Plane,
    Sphere,
  };

  struct PrimitiveComponent
  {
    PrimitiveType type = PrimitiveType::Cube;
  };
}

#endif
