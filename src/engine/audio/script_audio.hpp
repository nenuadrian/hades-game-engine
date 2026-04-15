#ifndef HADES_ENGINE_AUDIO_SCRIPT_AUDIO_HPP
#define HADES_ENGINE_AUDIO_SCRIPT_AUDIO_HPP

// Scripting-friendly procedural audio facade on top of SoLoud.
//
// Use this from HadesScript subclasses to generate tones, retro SFX, noise,
// speech and filters at runtime without touching AudioSourceComponent. All
// calls are no-ops when the audio engine is unavailable.
//
// For direct access to the underlying SoLoud engine (e.g. to instantiate
// something this facade doesn't wrap) call hades::Audio::raw(), or go
// through hades::HadesAPI::audioEngine()->raw() yourself.

#include <cstdint>
#include <string>

#include "audio_bus.hpp"

namespace SoLoud
{
  class Soloud;
  class AudioSource;
}

namespace hades
{
  class AudioEngine;

  // Called by the runtime / editor when the AudioEngine is ready, so the
  // facade (and HadesAPI::audioEngine()) can reach SoLoud. Pass nullptr on
  // shutdown so scripts that fire afterwards become no-ops. Having this as
  // a free function avoids including the scripting HadesAPI (which has a
  // name collision with the networked HadesAPI) from game_runtime.cpp.
  void register_script_audio_engine(AudioEngine *engine);

  // An opaque, copyable handle to a playing voice. Zero is "invalid / not
  // playing" and all operations on it are no-ops.
  struct AudioHandle
  {
    std::uint32_t voice = 0;
    explicit operator bool() const { return voice != 0; }
  };

  class Audio
  {
  public:
    // ---- Procedural generators ------------------------------------------
    // SFXR retro preset. See SfxrPreset for values. `seed` 0 reuses the
    // preset's built-in seed; non-zero randomises around it.
    enum SfxrPreset
    {
      SfxrCoin = 0,
      SfxrLaser,
      SfxrExplosion,
      SfxrPowerup,
      SfxrHurt,
      SfxrJump,
      SfxrBlip,
    };
    static AudioHandle playSfxr(int preset, int seed = 0, float volume = 1.0f);

    // Single-oscillator tone at `freqHz` Hz.
    //   wave: 0=square, 1=sawtooth, 2=sine, 3=triangle, 4=noise
    // Loops forever until stop(). Good for continuous tones / lab tests.
    static AudioHandle playTone(float freqHz, int wave = 2, float volume = 0.5f);

    // Procedural noise. color: 0=white, 1=pink, 2=brown, 3=blue.
    // Loops forever until stop().
    static AudioHandle playNoise(int color = 0, float volume = 0.5f);

    // Text-to-speech (SoLoud's built-in synth; robotic but dependency-free).
    static AudioHandle playSpeech(const std::string &text, float volume = 1.0f);

    // ---- File playback --------------------------------------------------
    // Convenience one-shot for a WAV/MP3/OGG/FLAC on disk. For sounds you
    // want driven by components/scene data use AudioSourceComponent instead.
    static AudioHandle playSample(const std::string &path, float volume = 1.0f, bool looping = false);

    // ---- 3D positional variants ----------------------------------------
    static AudioHandle playSfxr3D(int preset, float x, float y, float z, float volume = 1.0f);
    static AudioHandle playTone3D(float freqHz, int wave, float x, float y, float z, float volume = 0.5f);
    static AudioHandle playSample3D(const std::string &path, float x, float y, float z, float volume = 1.0f, bool looping = false);

    // ---- Voice control --------------------------------------------------
    static void stop(AudioHandle h);
    static void setVolume(AudioHandle h, float v);
    static void setPitch(AudioHandle h, float relativeSpeed); // 1.0 == original
    static void setPan(AudioHandle h, float pan);             // -1 .. +1
    static bool isPlaying(AudioHandle h);
    static void fadeVolume(AudioHandle h, float target, float seconds);
    static void scheduleStop(AudioHandle h, float seconds);

    // Mute/stop everything.
    static void stopAll();

    // ---- Bus-level filters ---------------------------------------------
    // Apply a filter to an entire bus (affects every source routed through
    // it, including component-driven ones). Pass Filter::None to clear.
    // Parameter meanings follow SoLoud's API; the common two params are:
    //   LowPass/HighPass/BandPass: p1 = cutoff Hz (default 1000), p2 = resonance (0..1, default 0.2)
    //   Echo:                      p1 = delay sec (0..1, default 0.3), p2 = wet (0..1, default 0.7)
    //   Reverb:                    p1 = wet (0..1, default 0.5)
    //   LoFi:                      p1 = sample rate Hz (default 8000), p2 = bit depth (1..16, default 8)
    //   Flanger:                   p1 = delay sec (default 0.005), p2 = freq Hz (default 10)
    enum class Filter
    {
      None,
      LowPass,
      HighPass,
      BandPass,
      Echo,
      Reverb,
      LoFi,
      Flanger,
      BassBoost,
      Robotize,
      Waveshaper,
    };
    // `slot` is 0..3. Setting a filter in a slot replaces whatever was there.
    static void setBusFilter(AudioBus bus, int slot, Filter filter, float p1 = 0.0f, float p2 = 0.0f);
    static void clearBusFilter(AudioBus bus, int slot);

    // ---- Listener (for 3D playback) ------------------------------------
    // If an AudioListenerComponent is present in the world, the audio system
    // updates the listener every frame and this call is unnecessary. Exposed
    // here for headless / ad-hoc use.
    static void setListener(float x, float y, float z,
                            float fwdX = 0.0f, float fwdY = 0.0f, float fwdZ = -1.0f,
                            float upX = 0.0f, float upY = 1.0f, float upZ = 0.0f);

    // ---- Escape hatch ---------------------------------------------------
    // Direct SoLoud engine pointer. Returns nullptr when audio is disabled.
    // Use this to build anything the facade doesn't wrap (e.g. Monotone,
    // openmpt trackers, custom AudioSource subclasses). The returned engine
    // is owned by the runtime -- don't delete it.
    static SoLoud::Soloud *raw();
  };
}

#endif
