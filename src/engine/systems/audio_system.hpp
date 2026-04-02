#ifndef HADES_ENGINE_SYSTEMS_AUDIO_SYSTEM_HPP
#define HADES_ENGINE_SYSTEMS_AUDIO_SYSTEM_HPP

#include "../core/ecs/system.hpp"

namespace hades
{
  class AudioEngine;

  class AudioSystem : public System
  {
  public:
    void setAudioEngine(AudioEngine *audioEngine);
    void update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager) override;

  private:
    AudioEngine *audioEngine_ = nullptr;
  };
}

#endif
