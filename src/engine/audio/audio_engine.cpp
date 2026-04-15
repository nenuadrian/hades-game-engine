#include "audio_engine.hpp"

#include <algorithm>

#include <soloud_wav.h>
#include <soloud_wavstream.h>

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

    const SoLoud::result result = soloud.init();
    if (result != SoLoud::SO_NO_ERROR)
    {
      hades::Log::warn("failed to initialize SoLoud engine (%u)", result);
      return false;
    }

    initialized = true;
    if (!ensure_buses())
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
      if (managed.voiceHandle != 0)
      {
        soloud.stop(managed.voiceHandle);
        managed.voiceHandle = 0;
      }
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

      if (it->second.voiceHandle != 0)
      {
        soloud.stop(it->second.voiceHandle);
      }
      it = managedSources.erase(it);
    }
  }

  void AudioEngine::set_master_volume(float volume)
  {
    if (!initialized)
    {
      return;
    }

    soloud.setGlobalVolume(clamp_non_negative(volume));
  }

  void AudioEngine::set_bus_volume(AudioBus bus, float volume)
  {
    if (!busesInitialized)
    {
      // Master volume is handled separately via set_master_volume.
      if (bus == AudioBus::Master && initialized)
      {
        soloud.setGlobalVolume(clamp_non_negative(volume));
      }
      return;
    }

    const float clamped = clamp_non_negative(volume);
    switch (bus)
    {
    case AudioBus::Master:
      soloud.setGlobalVolume(clamped);
      return;
    case AudioBus::Music:
      soloud.setVolume(musicBusHandle, clamped);
      return;
    case AudioBus::Sfx:
      soloud.setVolume(sfxBusHandle, clamped);
      return;
    case AudioBus::Voice:
      soloud.setVolume(voiceBusHandle, clamped);
      return;
    }
  }

  void AudioEngine::set_listener(const AudioListenerComponent &listener, const PositionComponent3D &position)
  {
    if (!initialized)
    {
      return;
    }

    soloud.set3dListenerParameters(
        position.x, position.y, position.z,
        listener.forwardX, listener.forwardY, listener.forwardZ,
        listener.upX, listener.upY, listener.upZ,
        0.0f, 0.0f, 0.0f);
    soloud.update3dAudio();
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

    const float volume = clamp_non_negative(source.volume);
    const float pitch = clamp_positive(source.pitch, 1.0f);
    const float minDistance = clamp_positive(source.minDistance, 1.0f);
    const float maxDistance = std::max(clamp_positive(source.maxDistance, minDistance), minDistance);
    const float rolloff = clamp_positive(source.rolloff, 1.0f);

    if (managed.audioSource)
    {
      managed.audioSource->setLooping(source.looping);
      if (managed.spatialized)
      {
        managed.audioSource->set3dAttenuation(SoLoud::AudioSource::INVERSE_DISTANCE, rolloff);
        managed.audioSource->set3dMinMaxDistance(minDistance, maxDistance);
      }
    }

    const PositionComponent3D origin = (position != nullptr) ? *position : PositionComponent3D();

    if (source.playOnStart && !managed.autoplayConsumed)
    {
      SoLoud::Bus *bus = bus_for(managed.bus);
      if (bus != nullptr && managed.audioSource)
      {
        if (managed.spatialized)
        {
          managed.voiceHandle = bus->play3d(
              *managed.audioSource,
              origin.x, origin.y, origin.z,
              0.0f, 0.0f, 0.0f,
              volume);
        }
        else
        {
          managed.voiceHandle = bus->play(*managed.audioSource, volume);
        }
        managed.autoplayConsumed = true;
      }
    }

    if (managed.voiceHandle != 0 && soloud.isValidVoiceHandle(managed.voiceHandle))
    {
      soloud.setVolume(managed.voiceHandle, volume);
      soloud.setRelativePlaySpeed(managed.voiceHandle, pitch);
      soloud.setLooping(managed.voiceHandle, source.looping);

      if (managed.spatialized)
      {
        soloud.set3dSourcePosition(managed.voiceHandle, origin.x, origin.y, origin.z);
        soloud.set3dSourceAttenuation(managed.voiceHandle, SoLoud::AudioSource::INVERSE_DISTANCE, rolloff);
        soloud.set3dSourceMinMaxDistance(managed.voiceHandle, minDistance, maxDistance);
      }
    }
    else
    {
      managed.voiceHandle = 0;
    }
  }

  SoLoud::Bus &AudioEngine::bus_raw(AudioBus bus)
  {
    switch (bus)
    {
    case AudioBus::Master:
      return sfxBus;
    case AudioBus::Music:
      return musicBus;
    case AudioBus::Sfx:
      return sfxBus;
    case AudioBus::Voice:
      return voiceBus;
    }
    return sfxBus;
  }

  SoLoud::Bus *AudioEngine::bus_for(AudioBus bus)
  {
    if (!busesInitialized)
    {
      return nullptr;
    }

    switch (bus)
    {
    case AudioBus::Master:
      // Playing directly into the master output is accomplished by routing
      // through sfxBus; there is no dedicated master bus in SoLoud.
      return &sfxBus;
    case AudioBus::Music:
      return &musicBus;
    case AudioBus::Sfx:
      return &sfxBus;
    case AudioBus::Voice:
      return &voiceBus;
    }

    return &sfxBus;
  }

  bool AudioEngine::ensure_buses()
  {
    if (!initialized)
    {
      return false;
    }
    if (busesInitialized)
    {
      return true;
    }

    musicBusHandle = soloud.play(musicBus);
    sfxBusHandle = soloud.play(sfxBus);
    voiceBusHandle = soloud.play(voiceBus);

    if (musicBusHandle == 0 || sfxBusHandle == 0 || voiceBusHandle == 0)
    {
      hades::Log::warn("failed to start one or more SoLoud audio buses");
      if (musicBusHandle != 0)
      {
        soloud.stop(musicBusHandle);
      }
      if (sfxBusHandle != 0)
      {
        soloud.stop(sfxBusHandle);
      }
      if (voiceBusHandle != 0)
      {
        soloud.stop(voiceBusHandle);
      }
      musicBusHandle = sfxBusHandle = voiceBusHandle = 0;
      return false;
    }

    busesInitialized = true;
    return true;
  }

  bool AudioEngine::ensure_source(Entity::EntityId entity, const AudioSourceComponent &source)
  {
    if (!busesInitialized)
    {
      return false;
    }

    auto existing = managedSources.find(entity);
    if (existing != managedSources.end())
    {
      const bool matchesConfig = existing->second.audioSource &&
                                 existing->second.assetPath == source.assetPath &&
                                 existing->second.streaming == source.streaming &&
                                 existing->second.spatialized == source.spatialized &&
                                 existing->second.bus == source.bus;
      if (matchesConfig)
      {
        return true;
      }

      const bool failedWithSameConfig = existing->second.failed &&
                                        existing->second.assetPath == source.assetPath &&
                                        existing->second.streaming == source.streaming &&
                                        existing->second.spatialized == source.spatialized &&
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
    managed.streaming = source.streaming;
    managed.spatialized = source.spatialized;

    SoLoud::result loadResult = SoLoud::SO_NO_ERROR;
    if (source.streaming)
    {
      auto stream = std::make_unique<SoLoud::WavStream>();
      loadResult = stream->load(source.assetPath.c_str());
      if (loadResult == SoLoud::SO_NO_ERROR)
      {
        managed.audioSource = std::move(stream);
      }
    }
    else
    {
      auto wav = std::make_unique<SoLoud::Wav>();
      loadResult = wav->load(source.assetPath.c_str());
      if (loadResult == SoLoud::SO_NO_ERROR)
      {
        managed.audioSource = std::move(wav);
      }
    }

    if (loadResult != SoLoud::SO_NO_ERROR || !managed.audioSource)
    {
      managed.failed = true;
      hades::Log::warn(
          "failed to load audio asset '%s' for entity %u (%u)",
          source.assetPath.c_str(),
          entity,
          loadResult);
      return false;
    }

    managed.failed = false;
    managed.autoplayConsumed = false;
    managed.voiceHandle = 0;
    return true;
  }

  void AudioEngine::release_source(Entity::EntityId entity)
  {
    auto it = managedSources.find(entity);
    if (it == managedSources.end())
    {
      return;
    }

    if (it->second.voiceHandle != 0)
    {
      soloud.stop(it->second.voiceHandle);
    }

    managedSources.erase(it);
  }

  void AudioEngine::shutdown()
  {
    for (auto &entry : managedSources)
    {
      if (entry.second.voiceHandle != 0)
      {
        soloud.stop(entry.second.voiceHandle);
      }
    }
    managedSources.clear();

    if (busesInitialized)
    {
      if (musicBusHandle != 0)
      {
        soloud.stop(musicBusHandle);
      }
      if (sfxBusHandle != 0)
      {
        soloud.stop(sfxBusHandle);
      }
      if (voiceBusHandle != 0)
      {
        soloud.stop(voiceBusHandle);
      }
      musicBusHandle = sfxBusHandle = voiceBusHandle = 0;
      busesInitialized = false;
    }

    if (initialized)
    {
      soloud.deinit();
      initialized = false;
    }
  }
}
