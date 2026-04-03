#ifndef HADES_ENGINE_COMPONENTS_TEXT_COMPONENT_HPP
#define HADES_ENGINE_COMPONENTS_TEXT_COMPONENT_HPP

#include <string>

namespace hades
{
  struct TextComponent
  {
    std::string content = "Text";
    float fontSize = 1.0f;
    float wrapWidth = 4.0f;
    float lineSpacing = 1.25f;
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
    float rollDegrees = 0.0f;
  };
}

#endif
