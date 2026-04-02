#ifndef HADES_ENGINE_AUDIO_AUDIO_BUS_HPP
#define HADES_ENGINE_AUDIO_AUDIO_BUS_HPP

namespace hades
{
  enum class AudioBus
  {
    Master = 0,
    Music,
    Sfx,
    Voice,
  };

  inline const char *audio_bus_label(AudioBus bus)
  {
    switch (bus)
    {
    case AudioBus::Master:
      return "Master";
    case AudioBus::Music:
      return "Music";
    case AudioBus::Sfx:
      return "SFX";
    case AudioBus::Voice:
      return "Voice";
    }

    return "Unknown";
  }
}

#endif
