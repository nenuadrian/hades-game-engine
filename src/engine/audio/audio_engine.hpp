#ifndef HADES_ENGINE_AUDIO_AUDIO_ENGINE_HPP
#define HADES_ENGINE_AUDIO_AUDIO_ENGINE_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <soloud.h>
#include <soloud_bus.h>

#include "../components/audio_listener_component.hpp"
#include "../components/audio_source_component.hpp"
#include "../components/position_component_3d.hpp"
#include "../core/ecs/entity.hpp"

namespace SoLoud
{
  class AudioSource;
}

namespace hades
{
  class AudioEngine
  {
  public:
    AudioEngine();
    ~AudioEngine();

    bool init();
    bool is_initialized() const;
    void stop_all();
    void prune_sources(const std::unordered_set<Entity::EntityId> &activeSources);
    void set_master_volume(float volume);
    void set_bus_volume(AudioBus bus, float volume);
    void set_listener(const AudioListenerComponent &listener, const PositionComponent3D &position);
    void sync_source(
        Entity::EntityId entity,
        const AudioSourceComponent &source,
        const PositionComponent3D *position);

  private:
    struct ManagedSource
    {
      std::unique_ptr<SoLoud::AudioSource> audioSource;
      std::string assetPath;
      AudioBus bus = AudioBus::Sfx;
      bool streaming = false;
      bool spatialized = true;
      bool failed = false;
      bool autoplayConsumed = false;
      SoLoud::handle voiceHandle = 0;
    };

    SoLoud::Bus *bus_for(AudioBus bus);
    bool ensure_buses();
    bool ensure_source(Entity::EntityId entity, const AudioSourceComponent &source);
    void release_source(Entity::EntityId entity);
    void shutdown();

    SoLoud::Soloud soloud{};
    SoLoud::Bus musicBus{};
    SoLoud::Bus sfxBus{};
    SoLoud::Bus voiceBus{};
    SoLoud::handle musicBusHandle = 0;
    SoLoud::handle sfxBusHandle = 0;
    SoLoud::handle voiceBusHandle = 0;
    bool busesInitialized = false;
    bool initialized = false;
    std::unordered_map<Entity::EntityId, ManagedSource> managedSources;
  };
}

#endif
