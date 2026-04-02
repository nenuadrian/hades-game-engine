#ifndef HADES_ENGINE_SYSTEMS_AUDIO_SYSTEM_HPP
#define HADES_ENGINE_SYSTEMS_AUDIO_SYSTEM_HPP

#include <optional>

#include "../core/ecs/entity.hpp"
#include "../core/ecs/system.hpp"

namespace hades
{
  class AudioEngine;

  class AudioSystem : public System
  {
  public:
    void setAudioEngine(AudioEngine *audioEngine);
    void set_active_world(std::optional<Entity::EntityId> activeWorld);
    void update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager) override;

  private:
    AudioEngine *audioEngine_ = nullptr;
    std::optional<Entity::EntityId> activeWorld_;
  };
}

#endif
