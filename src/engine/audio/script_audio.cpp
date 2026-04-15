#include "script_audio.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>

#include <soloud.h>
#include <soloud_bus.h>
#include <soloud_noise.h>
#include <soloud_sfxr.h>
#include <soloud_speech.h>
#include <soloud_wav.h>
#include <soloud_wavstream.h>

// Filters -- each header is guarded by SoLoud's own include-once, and unused
// filter classes are stripped by the linker because they're only referenced
// by setBusFilter's switch statement.
#include <soloud_bassboostfilter.h>
#include <soloud_biquadresonantfilter.h>
#include <soloud_echofilter.h>
#include <soloud_flangerfilter.h>
#include <soloud_freeverbfilter.h>
#include <soloud_lofifilter.h>
#include <soloud_robotizefilter.h>
#include <soloud_waveshaperfilter.h>

#include "../core/log.hpp"
#include "../runtime/hades_script.hpp"
#include "audio_engine.hpp"

namespace hades
{
  namespace
  {
    // Pool of procedurally-generated sources and bus-level filters. SoLoud
    // does not own AudioSource/Filter lifetime, so we keep them alive in
    // these pools. Sources are pruned when their voice stops.
    struct ActiveSource
    {
      std::unique_ptr<SoLoud::AudioSource> source;
      std::uint32_t voice = 0;
    };

    std::mutex g_pool_mutex;
    std::vector<ActiveSource> g_active_sources;
    // Four filter slots per bus (Master reuses Sfx, see AudioEngine::bus_raw).
    constexpr int kBusCount = 4;
    constexpr int kFilterSlots = 4;
    std::array<std::array<std::unique_ptr<SoLoud::Filter>, kFilterSlots>, kBusCount> g_bus_filters{};

    AudioEngine *engine()
    {
      return HadesAPI::audioEngine();
    }

    SoLoud::Soloud *soloud()
    {
      AudioEngine *e = engine();
      if (e == nullptr || !e->is_initialized())
      {
        return nullptr;
      }
      return &e->raw();
    }

    SoLoud::Bus *busPtr(AudioBus bus)
    {
      AudioEngine *e = engine();
      if (e == nullptr || !e->is_initialized())
      {
        return nullptr;
      }
      return &e->bus_raw(bus);
    }

    // Drop any pooled sources whose voice has finished so memory doesn't
    // grow unbounded when scripts fire a lot of one-shots.
    void prune_finished(SoLoud::Soloud &sl)
    {
      g_active_sources.erase(
          std::remove_if(
              g_active_sources.begin(),
              g_active_sources.end(),
              [&](const ActiveSource &entry)
              {
                return entry.voice == 0 || !sl.isValidVoiceHandle(entry.voice);
              }),
          g_active_sources.end());
    }

    // Play a procedural source routed through the Sfx bus (so bus volume
    // and bus-level filters apply). Takes ownership of the source.
    AudioHandle play_pooled(std::unique_ptr<SoLoud::AudioSource> src, float volume, bool paused = false)
    {
      if (!src)
      {
        return {};
      }
      SoLoud::Soloud *sl = soloud();
      SoLoud::Bus *bus = busPtr(AudioBus::Sfx);
      if (sl == nullptr || bus == nullptr)
      {
        return {};
      }

      std::lock_guard<std::mutex> lock(g_pool_mutex);
      prune_finished(*sl);

      const SoLoud::handle voice = bus->play(*src, volume, 0.0f, paused);
      if (voice == 0)
      {
        return {};
      }
      g_active_sources.push_back({std::move(src), voice});
      return {voice};
    }

    AudioHandle play_pooled_3d(
        std::unique_ptr<SoLoud::AudioSource> src,
        float x, float y, float z,
        float volume,
        bool paused = false)
    {
      if (!src)
      {
        return {};
      }
      SoLoud::Soloud *sl = soloud();
      SoLoud::Bus *bus = busPtr(AudioBus::Sfx);
      if (sl == nullptr || bus == nullptr)
      {
        return {};
      }

      std::lock_guard<std::mutex> lock(g_pool_mutex);
      prune_finished(*sl);

      const SoLoud::handle voice =
          bus->play3d(*src, x, y, z, 0.0f, 0.0f, 0.0f, volume, paused);
      if (voice == 0)
      {
        return {};
      }
      g_active_sources.push_back({std::move(src), voice});
      return {voice};
    }

    // Build a short sine/saw/square/tri/noise buffer (one cycle at target
    // freq) and load it into a Wav with loopable playback. SoLoud doesn't
    // ship a generic oscillator, so we synthesise the cycle ourselves.
    std::unique_ptr<SoLoud::Wav> build_tone(float freqHz, int wave)
    {
      constexpr float kSampleRate = 44100.0f;
      if (freqHz < 1.0f)
      {
        freqHz = 1.0f;
      }
      // Generate a whole number of cycles so looping is seamless. Aim for
      // roughly 20ms of audio so very low frequencies still produce enough
      // samples for SoLoud's resampler.
      const float minSeconds = 0.02f;
      const int cycles = std::max(1, static_cast<int>(std::ceil(freqHz * minSeconds)));
      const int samplesPerCycle = std::max(4, static_cast<int>(kSampleRate / freqHz));
      const int totalSamples = samplesPerCycle * cycles;

      std::vector<float> buffer(static_cast<size_t>(totalSamples));
      const float twoPi = 6.28318530717958647692f;
      for (int i = 0; i < totalSamples; ++i)
      {
        const float t = static_cast<float>(i) / static_cast<float>(samplesPerCycle);
        const float phase = t - std::floor(t); // 0..1 within a cycle
        float s = 0.0f;
        switch (wave)
        {
        case 0: // square
          s = (phase < 0.5f) ? 1.0f : -1.0f;
          break;
        case 1: // sawtooth
          s = 2.0f * phase - 1.0f;
          break;
        case 3: // triangle
          s = (phase < 0.5f) ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
          break;
        case 4: // noise (white)
        {
          // LCG -- deterministic enough for audio, avoids std::rand() lock.
          const uint32_t seed = static_cast<uint32_t>(i) * 1664525u + 1013904223u;
          s = (static_cast<float>(seed >> 8) / static_cast<float>(0xFFFFFFu)) * 2.0f - 1.0f;
          break;
        }
        case 2: // sine
        default:
          s = std::sin(twoPi * phase);
          break;
        }
        buffer[static_cast<size_t>(i)] = s * 0.8f;
      }

      auto wav = std::make_unique<SoLoud::Wav>();
      // loadRawWave with aCopy=true so SoLoud owns the sample memory.
      wav->loadRawWave(
          buffer.data(),
          static_cast<unsigned int>(buffer.size()),
          kSampleRate,
          /*aChannels=*/1,
          /*aCopy=*/true,
          /*aTakeOwnership=*/true);
      wav->setLooping(true);
      return wav;
    }

    std::unique_ptr<SoLoud::Filter> make_filter(Audio::Filter f, float p1, float p2)
    {
      using F = Audio::Filter;
      switch (f)
      {
      case F::None:
        return nullptr;
      case F::LowPass:
      {
        auto bq = std::make_unique<SoLoud::BiquadResonantFilter>();
        bq->setParams(SoLoud::BiquadResonantFilter::LOWPASS,
                      (p1 > 0.0f) ? p1 : 1000.0f,
                      (p2 > 0.0f) ? p2 : 0.2f);
        return bq;
      }
      case F::HighPass:
      {
        auto bq = std::make_unique<SoLoud::BiquadResonantFilter>();
        bq->setParams(SoLoud::BiquadResonantFilter::HIGHPASS,
                      (p1 > 0.0f) ? p1 : 1000.0f,
                      (p2 > 0.0f) ? p2 : 0.2f);
        return bq;
      }
      case F::BandPass:
      {
        auto bq = std::make_unique<SoLoud::BiquadResonantFilter>();
        bq->setParams(SoLoud::BiquadResonantFilter::BANDPASS,
                      (p1 > 0.0f) ? p1 : 1000.0f,
                      (p2 > 0.0f) ? p2 : 0.2f);
        return bq;
      }
      case F::Echo:
      {
        auto echo = std::make_unique<SoLoud::EchoFilter>();
        echo->setParams((p1 > 0.0f) ? p1 : 0.3f, (p2 > 0.0f) ? p2 : 0.7f);
        return echo;
      }
      case F::Reverb:
      {
        auto rev = std::make_unique<SoLoud::FreeverbFilter>();
        rev->setParams((p1 > 0.0f) ? p1 : 0.5f, 0.5f, 0.5f, 1.0f);
        return rev;
      }
      case F::LoFi:
      {
        auto lofi = std::make_unique<SoLoud::LofiFilter>();
        lofi->setParams((p1 > 0.0f) ? p1 : 8000.0f, (p2 > 0.0f) ? p2 : 8.0f);
        return lofi;
      }
      case F::Flanger:
      {
        auto fl = std::make_unique<SoLoud::FlangerFilter>();
        fl->setParams((p1 > 0.0f) ? p1 : 0.005f, (p2 > 0.0f) ? p2 : 10.0f);
        return fl;
      }
      case F::BassBoost:
      {
        auto bb = std::make_unique<SoLoud::BassboostFilter>();
        bb->setParams((p1 > 0.0f) ? p1 : 2.0f);
        return bb;
      }
      case F::Robotize:
      {
        auto rb = std::make_unique<SoLoud::RobotizeFilter>();
        rb->setParams((p1 > 0.0f) ? p1 : 30.0f, static_cast<int>((p2 > 0.0f) ? p2 : 0.0f));
        return rb;
      }
      case F::Waveshaper:
      {
        auto ws = std::make_unique<SoLoud::WaveShaperFilter>();
        ws->setParams((p1 != 0.0f) ? p1 : 0.0f);
        return ws;
      }
      }
      return nullptr;
    }

    int busIndex(AudioBus bus)
    {
      switch (bus)
      {
      case AudioBus::Master:
        return 0;
      case AudioBus::Music:
        return 1;
      case AudioBus::Sfx:
        return 2;
      case AudioBus::Voice:
        return 3;
      }
      return 2;
    }
  } // namespace

  // ------------------------------------------------------------------
  // Public API
  // ------------------------------------------------------------------

  AudioHandle Audio::playSfxr(int preset, int seed, float volume)
  {
    if (soloud() == nullptr)
    {
      return {};
    }
    auto sfxr = std::make_unique<SoLoud::Sfxr>();
    if (sfxr->loadPreset(preset, seed != 0 ? seed : 3) != SoLoud::SO_NO_ERROR)
    {
      Log::warn("Audio::playSfxr: failed to load preset %d", preset);
      return {};
    }
    return play_pooled(std::move(sfxr), volume);
  }

  AudioHandle Audio::playSfxr3D(int preset, float x, float y, float z, float volume)
  {
    if (soloud() == nullptr)
    {
      return {};
    }
    auto sfxr = std::make_unique<SoLoud::Sfxr>();
    if (sfxr->loadPreset(preset, 3) != SoLoud::SO_NO_ERROR)
    {
      return {};
    }
    return play_pooled_3d(std::move(sfxr), x, y, z, volume);
  }

  AudioHandle Audio::playTone(float freqHz, int wave, float volume)
  {
    if (soloud() == nullptr)
    {
      return {};
    }
    return play_pooled(build_tone(freqHz, wave), volume);
  }

  AudioHandle Audio::playTone3D(float freqHz, int wave, float x, float y, float z, float volume)
  {
    if (soloud() == nullptr)
    {
      return {};
    }
    return play_pooled_3d(build_tone(freqHz, wave), x, y, z, volume);
  }

  AudioHandle Audio::playNoise(int color, float volume)
  {
    if (soloud() == nullptr)
    {
      return {};
    }
    auto noise = std::make_unique<SoLoud::Noise>();
    noise->setType(color);
    noise->setLooping(true);
    return play_pooled(std::move(noise), volume);
  }

  AudioHandle Audio::playSpeech(const std::string &text, float volume)
  {
    if (soloud() == nullptr)
    {
      return {};
    }
    auto speech = std::make_unique<SoLoud::Speech>();
    if (speech->setText(text.c_str()) != SoLoud::SO_NO_ERROR)
    {
      return {};
    }
    return play_pooled(std::move(speech), volume);
  }

  AudioHandle Audio::playSample(const std::string &path, float volume, bool looping)
  {
    if (soloud() == nullptr)
    {
      return {};
    }
    auto wav = std::make_unique<SoLoud::Wav>();
    if (wav->load(path.c_str()) != SoLoud::SO_NO_ERROR)
    {
      Log::warn("Audio::playSample: failed to load '%s'", path.c_str());
      return {};
    }
    wav->setLooping(looping);
    return play_pooled(std::move(wav), volume);
  }

  AudioHandle Audio::playSample3D(const std::string &path, float x, float y, float z, float volume, bool looping)
  {
    if (soloud() == nullptr)
    {
      return {};
    }
    auto wav = std::make_unique<SoLoud::Wav>();
    if (wav->load(path.c_str()) != SoLoud::SO_NO_ERROR)
    {
      return {};
    }
    wav->setLooping(looping);
    return play_pooled_3d(std::move(wav), x, y, z, volume);
  }

  void Audio::stop(AudioHandle h)
  {
    SoLoud::Soloud *sl = soloud();
    if (sl == nullptr || h.voice == 0)
    {
      return;
    }
    sl->stop(h.voice);
  }

  void Audio::setVolume(AudioHandle h, float v)
  {
    SoLoud::Soloud *sl = soloud();
    if (sl == nullptr || h.voice == 0)
    {
      return;
    }
    sl->setVolume(h.voice, v);
  }

  void Audio::setPitch(AudioHandle h, float relativeSpeed)
  {
    SoLoud::Soloud *sl = soloud();
    if (sl == nullptr || h.voice == 0 || relativeSpeed <= 0.0f)
    {
      return;
    }
    sl->setRelativePlaySpeed(h.voice, relativeSpeed);
  }

  void Audio::setPan(AudioHandle h, float pan)
  {
    SoLoud::Soloud *sl = soloud();
    if (sl == nullptr || h.voice == 0)
    {
      return;
    }
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    sl->setPan(h.voice, pan);
  }

  bool Audio::isPlaying(AudioHandle h)
  {
    SoLoud::Soloud *sl = soloud();
    if (sl == nullptr || h.voice == 0)
    {
      return false;
    }
    return sl->isValidVoiceHandle(h.voice);
  }

  void Audio::fadeVolume(AudioHandle h, float target, float seconds)
  {
    SoLoud::Soloud *sl = soloud();
    if (sl == nullptr || h.voice == 0)
    {
      return;
    }
    sl->fadeVolume(h.voice, target, seconds);
  }

  void Audio::scheduleStop(AudioHandle h, float seconds)
  {
    SoLoud::Soloud *sl = soloud();
    if (sl == nullptr || h.voice == 0)
    {
      return;
    }
    sl->scheduleStop(h.voice, seconds);
  }

  void Audio::stopAll()
  {
    SoLoud::Soloud *sl = soloud();
    if (sl == nullptr)
    {
      return;
    }
    std::lock_guard<std::mutex> lock(g_pool_mutex);
    for (auto &entry : g_active_sources)
    {
      if (entry.voice != 0)
      {
        sl->stop(entry.voice);
      }
    }
    g_active_sources.clear();
  }

  void Audio::setBusFilter(AudioBus bus, int slot, Filter filter, float p1, float p2)
  {
    if (slot < 0 || slot >= kFilterSlots)
    {
      return;
    }
    SoLoud::Bus *b = busPtr(bus);
    if (b == nullptr)
    {
      return;
    }
    std::lock_guard<std::mutex> lock(g_pool_mutex);
    const int idx = busIndex(bus);
    auto newFilter = make_filter(filter, p1, p2);
    // Detach before releasing the old filter (SoLoud keeps a raw pointer).
    b->setFilter(static_cast<unsigned int>(slot), nullptr);
    g_bus_filters[idx][slot] = std::move(newFilter);
    b->setFilter(static_cast<unsigned int>(slot), g_bus_filters[idx][slot].get());
  }

  void Audio::clearBusFilter(AudioBus bus, int slot)
  {
    setBusFilter(bus, slot, Filter::None);
  }

  void Audio::setListener(float x, float y, float z,
                          float fwdX, float fwdY, float fwdZ,
                          float upX, float upY, float upZ)
  {
    SoLoud::Soloud *sl = soloud();
    if (sl == nullptr)
    {
      return;
    }
    sl->set3dListenerParameters(x, y, z, fwdX, fwdY, fwdZ, upX, upY, upZ, 0.0f, 0.0f, 0.0f);
    sl->update3dAudio();
  }

  SoLoud::Soloud *Audio::raw()
  {
    return soloud();
  }

  void register_script_audio_engine(AudioEngine *engine)
  {
    HadesAPI::setAudioEngine(engine);
    if (engine == nullptr)
    {
      // Drop any pooled sources so their destructors run before the
      // SoLoud engine they were playing against goes away.
      std::lock_guard<std::mutex> lock(g_pool_mutex);
      g_active_sources.clear();
      for (auto &bus : g_bus_filters)
      {
        for (auto &filter : bus)
        {
          filter.reset();
        }
      }
    }
  }
}
