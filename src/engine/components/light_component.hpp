#ifndef HADES_ENGINE_COMPONENTS_LIGHT_COMPONENT_HPP
#define HADES_ENGINE_COMPONENTS_LIGHT_COMPONENT_HPP

namespace hades
{
  enum class LightType
  {
    Directional,
    Point,
    Spot,
  };

  struct LightComponent
  {
    LightType type = LightType::Directional;

    float colorR = 1.0f;
    float colorG = 1.0f;
    float colorB = 1.0f;

    float intensity = 1.0f;

    float range = 10.0f;

    float directionX = 0.0f;
    float directionY = -1.0f;
    float directionZ = 0.0f;

    float innerConeAngle = 25.0f;
    float outerConeAngle = 35.0f;

    float ambientContribution = 0.05f;

    bool castShadows = false;
    bool enabled = true;
  };
}

#endif
