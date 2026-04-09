#include "audio_engine.hpp"

#include <algorithm>

#include "../core/log.hpp"

namespace hades
{
  namespace
  {
    float clamp_non_negative(float value)
    {
      return (value < 0.0f) ? 0.0f : value;
    }

    float clamp_positive(float value, float fallback)
    {
      return (value > 0.0f) ? value : fallback;
    }
  }

  AudioEngine::AudioEngine() = default;

  AudioEngine::~AudioEngine()
  {
    shutdown();
  }

  bool AudioEngine::init()
  {
    if (initialized)
    {
      return true;
    }

    const ma_engine_config config = ma_engine_config_init();
    const ma_result result = ma_engine_init(&config, &engine);
    if (result != MA_SUCCESS)
    {
      hades::Log::warn("failed to initialize miniaudio engine (%d)", result);
      return false;
    }

    initialized = true;
    if (!ensure_groups())
    {
      shutdown();
      return false;
    }

    set_master_volume(1.0f);
    set_bus_volume(AudioBus::Music, 1.0f);
    set_bus_volume(AudioBus::Sfx, 1.0f);
    set_bus_volume(AudioBus::Voice, 1.0f);
    return true;
  }

  bool AudioEngine::is_initialized() const
  {
    return initialized;
  }

  void AudioEngine::stop_all()
  {
    for (auto &entry : managedSources)
    {
      auto &managed = entry.second;
      if (!managed.initialized)
      {
        continue;
      }

      ma_sound_stop(&managed.sound);
      ma_sound_seek_to_pcm_frame(&managed.sound, 0);
      managed.autoplayConsumed = false;
    }
  }

  void AudioEngine::prune_sources(const std::unordered_set<Entity::EntityId> &activeSources)
  {
    for (auto it = managedSources.begin(); it != managedSources.end();)
    {
      if (activeSources.find(it->first) != activeSources.end())
      {
        ++it;
        continue;
      }

      if (it->second.initialized)
      {
        ma_sound_uninit(&it->second.sound);
      }
      it = managedSources.erase(it);
    }
  }

  void AudioEngine::set_master_volume(float volume)
  {
    if (!groupsInitialized)
    {
      return;
    }

    ma_sound_group_set_volume(&masterGroup, clamp_non_negative(volume));
  }

  void AudioEngine::set_bus_volume(AudioBus bus, float volume)
  {
    ma_sound_group *group = group_for_bus(bus);
    if (group == nullptr)
    {
      return;
    }

    ma_sound_group_set_volume(group, clamp_non_negative(volume));
  }

  void AudioEngine::set_listener(const AudioListenerComponent &listener, const PositionComponent3D &position)
  {
    if (!initialized)
    {
      return;
    }

    ma_engine_listener_set_position(&engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(
        &engine,
        0,
        listener.forwardX,
        listener.forwardY,
        listener.forwardZ);
    ma_engine_listener_set_world_up(&engine, 0, listener.upX, listener.upY, listener.upZ);
    ma_engine_listener_set_velocity(&engine, 0, 0.0f, 0.0f, 0.0f);
  }

  void AudioEngine::sync_source(
      Entity::EntityId entity,
      const AudioSourceComponent &source,
      const PositionComponent3D *position)
  {
    if (!initialized)
    {
      return;
    }

    if (source.assetPath.empty())
    {
      release_source(entity);
      return;
    }

    if (!ensure_source(entity, source))
    {
      return;
    }

    auto &managed = managedSources.at(entity);
    ma_sound_set_volume(&managed.sound, clamp_non_negative(source.volume));
    ma_sound_set_pitch(&managed.sound, clamp_positive(source.pitch, 1.0f));
    ma_sound_set_looping(&managed.sound, source.looping ? MA_TRUE : MA_FALSE);

    if (source.spatialized)
    {
      const float minDistance = clamp_positive(source.minDistance, 1.0f);
      const float maxDistance = std::max(clamp_positive(source.maxDistance, minDistance), minDistance);
      const PositionComponent3D origin = (position != nullptr) ? *position : PositionComponent3D();

      ma_sound_set_position(&managed.sound, origin.x, origin.y, origin.z);
      ma_sound_set_attenuation_model(&managed.sound, ma_attenuation_model_inverse);
      ma_sound_set_rolloff(&managed.sound, clamp_positive(source.rolloff, 1.0f));
      ma_sound_set_min_distance(&managed.sound, minDistance);
      ma_sound_set_max_distance(&managed.sound, maxDistance);
    }

    if (source.playOnStart && !managed.autoplayConsumed)
    {
      if (ma_sound_start(&managed.sound) == MA_SUCCESS)
      {
        managed.autoplayConsumed = true;
      }
    }
  }

  ma_sound_group *AudioEngine::group_for_bus(AudioBus bus)
  {
    if (!groupsInitialized)
    {
      return nullptr;
    }

    switch (bus)
    {
    case AudioBus::Master:
      return &masterGroup;
    case AudioBus::Music:
      return &musicGroup;
    case AudioBus::Sfx:
      return &sfxGroup;
    case AudioBus::Voice:
      return &voiceGroup;
    }

    return &masterGroup;
  }

  ma_uint32 AudioEngine::source_flags(const AudioSourceComponent &source) const
  {
    ma_uint32 flags = MA_SOUND_FLAG_ASYNC;
    if (source.streaming)
    {
      flags |= MA_SOUND_FLAG_STREAM;
    }
    if (!source.spatialized)
    {
      flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
    }

    return flags;
  }

  bool AudioEngine::ensure_groups()
  {
    if (!initialized)
    {
      return false;
    }
    if (groupsInitialized)
    {
      return true;
    }

    const auto init_group = [this](ma_sound_group *group, ma_sound_group *parent, const char *name) -> bool
    {
      const ma_result initResult = ma_sound_group_init(&engine, 0, parent, group);
      if (initResult != MA_SUCCESS)
      {
        hades::Log::warn("failed to initialize %s audio group (%d)", name, initResult);
        return false;
      }

      const ma_result startResult = ma_sound_group_start(group);
      if (startResult != MA_SUCCESS)
      {
        hades::Log::warn("failed to start %s audio group (%d)", name, startResult);
        ma_sound_group_uninit(group);
        return false;
      }

      return true;
    };

    if (!init_group(&masterGroup, nullptr, "master"))
    {
      return false;
    }
    if (!init_group(&musicGroup, &masterGroup, "music"))
    {
      ma_sound_group_uninit(&masterGroup);
      return false;
    }
    if (!init_group(&sfxGroup, &masterGroup, "sfx"))
    {
      ma_sound_group_uninit(&musicGroup);
      ma_sound_group_uninit(&masterGroup);
      return false;
    }
    if (!init_group(&voiceGroup, &masterGroup, "voice"))
    {
      ma_sound_group_uninit(&sfxGroup);
      ma_sound_group_uninit(&musicGroup);
      ma_sound_group_uninit(&masterGroup);
      return false;
    }

    groupsInitialized = true;
    return true;
  }

  bool AudioEngine::ensure_source(Entity::EntityId entity, const AudioSourceComponent &source)
  {
    if (!groupsInitialized)
    {
      return false;
    }

    const ma_uint32 flags = source_flags(source);
    auto existing = managedSources.find(entity);
    if (existing != managedSources.end())
    {
      const bool matchesConfig = existing->second.initialized &&
                                 existing->second.assetPath == source.assetPath &&
                                 existing->second.flags == flags &&
                                 existing->second.bus == source.bus;
      if (matchesConfig)
      {
        return true;
      }

      const bool failedWithSameConfig = existing->second.failed &&
                                        existing->second.assetPath == source.assetPath &&
                                        existing->second.flags == flags &&
                                        existing->second.bus == source.bus;
      if (failedWithSameConfig)
      {
        return false;
      }

      release_source(entity);
    }

    ManagedSource &managed = managedSources[entity];
    managed.assetPath = source.assetPath;
    managed.bus = source.bus;
    managed.flags = flags;

    const ma_result result = ma_sound_init_from_file(
        &engine,
        source.assetPath.c_str(),
        flags,
        group_for_bus(source.bus),
        nullptr,
        &managed.sound);
    if (result != MA_SUCCESS)
    {
      managed.failed = true;
      hades::Log::warn(
          "failed to load audio asset '%s' for entity %u (%d)",
          source.assetPath.c_str(),
          entity,
          result);
      return false;
    }

    managed.initialized = true;
    managed.failed = false;
    managed.autoplayConsumed = false;
    return true;
  }

  void AudioEngine::release_source(Entity::EntityId entity)
  {
    auto it = managedSources.find(entity);
    if (it == managedSources.end())
    {
      return;
    }

    if (it->second.initialized)
    {
      ma_sound_uninit(&it->second.sound);
    }

    managedSources.erase(it);
  }

  void AudioEngine::shutdown()
  {
    for (auto &entry : managedSources)
    {
      if (entry.second.initialized)
      {
        ma_sound_uninit(&entry.second.sound);
      }
    }
    managedSources.clear();

    if (groupsInitialized)
    {
      ma_sound_group_uninit(&voiceGroup);
      ma_sound_group_uninit(&sfxGroup);
      ma_sound_group_uninit(&musicGroup);
      ma_sound_group_uninit(&masterGroup);
      groupsInitialized = false;
    }

    if (initialized)
    {
      ma_engine_uninit(&engine);
      initialized = false;
    }
  }
}
