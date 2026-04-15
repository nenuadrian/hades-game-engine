// Demonstrates the hades::Audio scripting facade for procedural sound.
//
// Attach to any entity. On start it plays a SFXR "coin" preset. Number keys
// 1-5 play different oscillator tones (held while the key is down). SPACE
// toggles a brown-noise ambient loop with a low-pass filter applied to the
// entire SFX bus. 'R' cycles through SFXR presets. 'T' speaks a test phrase.

#include "engine/hades.hpp"

#include <unordered_map>

class ProceduralAudio : public hades::HadesScript
{
public:
  void onStart(hades::ScriptContext &) override
  {
    hades::Audio::playSfxr(hades::Audio::SfxrCoin, /*seed=*/0, /*volume=*/0.6f);
  }

  void onKeyDown(hades::ScriptContext &, int keyCode) override
  {
    switch (keyCode)
    {
    case hades::HADES_KEY_1:
      startTone_(keyCode, 220.0f, /*sine=*/2);
      break;
    case hades::HADES_KEY_2:
      startTone_(keyCode, 330.0f, /*sine=*/2);
      break;
    case hades::HADES_KEY_3:
      startTone_(keyCode, 440.0f, /*square=*/0);
      break;
    case hades::HADES_KEY_4:
      startTone_(keyCode, 660.0f, /*sawtooth=*/1);
      break;
    case hades::HADES_KEY_5:
      startTone_(keyCode, 880.0f, /*triangle=*/3);
      break;

    case hades::HADES_KEY_SPACE:
      if (ambient_.voice == 0)
      {
        ambient_ = hades::Audio::playNoise(/*brown=*/2, 0.35f);
        hades::Audio::setBusFilter(
            hades::AudioBus::Sfx,
            /*slot=*/0,
            hades::Audio::Filter::LowPass,
            /*cutoffHz=*/1200.0f,
            /*resonance=*/0.3f);
      }
      else
      {
        hades::Audio::fadeVolume(ambient_, 0.0f, 0.25f);
        hades::Audio::scheduleStop(ambient_, 0.3f);
        ambient_ = {};
        hades::Audio::clearBusFilter(hades::AudioBus::Sfx, 0);
      }
      break;

    case hades::HADES_KEY_R:
    {
      const int preset = (presetCycle_++) % 7;
      hades::Audio::playSfxr(preset, /*seed=*/preset * 11 + 1, 0.7f);
      break;
    }

    case hades::HADES_KEY_T:
      hades::Audio::playSpeech("Hello from the hades scripting audio facade", 1.0f);
      break;

    default:
      break;
    }
  }

  void onKeyUp(hades::ScriptContext &, int keyCode) override
  {
    auto it = toneVoices_.find(keyCode);
    if (it != toneVoices_.end())
    {
      hades::Audio::stop(it->second);
      toneVoices_.erase(it);
    }
  }

private:
  void startTone_(int keyCode, float freq, int wave)
  {
    // Stop any previous tone held under this key (key auto-repeat).
    if (auto it = toneVoices_.find(keyCode); it != toneVoices_.end())
    {
      hades::Audio::stop(it->second);
    }
    toneVoices_[keyCode] = hades::Audio::playTone(freq, wave, 0.3f);
  }

  hades::AudioHandle ambient_{};
  std::unordered_map<int, hades::AudioHandle> toneVoices_;
  int presetCycle_ = 0;
};

HADES_REGISTER_SCRIPT(ProceduralAudio)
