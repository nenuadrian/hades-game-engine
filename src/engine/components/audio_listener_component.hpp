#ifndef HADES_ENGINE_COMPONENTS_AUDIO_LISTENER_COMPONENT_HPP
#define HADES_ENGINE_COMPONENTS_AUDIO_LISTENER_COMPONENT_HPP

namespace hades
{
  struct AudioListenerComponent
  {
    bool enabled = true;
    float forwardX = 0.0f;
    float forwardY = 0.0f;
    float forwardZ = 1.0f;
    float upX = 0.0f;
    float upY = 1.0f;
    float upZ = 0.0f;
  };
}

#endif
