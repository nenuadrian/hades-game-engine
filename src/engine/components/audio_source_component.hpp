#ifndef HADES_ENGINE_COMPONENTS_AUDIO_SOURCE_COMPONENT_HPP
#define HADES_ENGINE_COMPONENTS_AUDIO_SOURCE_COMPONENT_HPP

#include <string>

#include "../audio/audio_bus.hpp"

namespace hades
{
  struct AudioSourceComponent
  {
    std::string assetPath;
    AudioBus bus = AudioBus::Sfx;
    bool playOnStart = true;
    bool looping = false;
    bool streaming = false;
    bool spatialized = true;
    float volume = 1.0f;
    float pitch = 1.0f;
    float minDistance = 1.0f;
    float maxDistance = 100.0f;
    float rolloff = 1.0f;
  };
}

#endif
