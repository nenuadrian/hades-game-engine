#ifndef HADES_ENGINE_COMPONENTS_PRIMITIVE_COMPONENT_HPP
#define HADES_ENGINE_COMPONENTS_PRIMITIVE_COMPONENT_HPP

namespace hades
{
  enum class PrimitiveType
  {
    Cube,
  };

  struct PrimitiveComponent
  {
    PrimitiveType type = PrimitiveType::Cube;
  };
}

#endif
