#ifndef HADES_ENGINE_AUDIO_AUDIO_ENGINE_HPP
#define HADES_ENGINE_AUDIO_AUDIO_ENGINE_HPP

#include <unordered_map>
#include <unordered_set>

#include <miniaudio.h>

#include "../components/audio_listener_component.hpp"
#include "../components/audio_source_component.hpp"
#include "../components/position_component_3d.hpp"
#include "../core/ecs/entity.hpp"

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
      ma_sound sound{};
      std::string assetPath;
      AudioBus bus = AudioBus::Sfx;
      ma_uint32 flags = 0;
      bool initialized = false;
      bool failed = false;
      bool autoplayConsumed = false;
    };

    ma_sound_group *group_for_bus(AudioBus bus);
    ma_uint32 source_flags(const AudioSourceComponent &source) const;
    bool ensure_groups();
    bool ensure_source(Entity::EntityId entity, const AudioSourceComponent &source);
    void release_source(Entity::EntityId entity);
    void shutdown();

    ma_engine engine{};
    ma_sound_group masterGroup{};
    ma_sound_group musicGroup{};
    ma_sound_group sfxGroup{};
    ma_sound_group voiceGroup{};
    bool groupsInitialized = false;
    bool initialized = false;
    std::unordered_map<Entity::EntityId, ManagedSource> managedSources;
  };
}

#endif
