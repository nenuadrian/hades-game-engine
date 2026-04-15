#ifndef HADES_ENGINE_RUNTIME_HADES_NEURAL_SCRIPT_HPP
#define HADES_ENGINE_RUNTIME_HADES_NEURAL_SCRIPT_HPP

#include <hne/core/types.hpp>

#include "hades_script.hpp"

namespace hades
{
  /// Base class for scripts that participate in reinforcement learning.
  ///
  /// A `NeuralScript` declares its observation and action spaces and implements
  /// hooks that let the training harness (see `hades::HadesScriptEnv`) and the
  /// runtime inference dispatcher (see `hades::ScriptRuntime`) read observations
  /// from the live ECS, apply actions back into it, and compute rewards/done
  /// signals.
  ///
  /// Scripts still inherit `HadesScript::onStart/onUpdate/...`. When a trained
  /// policy is attached (via `ScriptAttachment::modelPath`), the runtime skips
  /// `onUpdate` and drives the script through `readObservation → policy.evaluate
  /// → applyAction` instead. When the script is the training subject, the
  /// env adapter drives it; `onUpdate` is skipped in that case too.
  class NeuralScript : public HadesScript
  {
  public:
    ~NeuralScript() override = default;

    /// Declare the observation vector shape/range. Called once when the script
    /// is registered (training, inference, or editor inspection).
    virtual hne::SpaceSpec observationSpace() const = 0;

    /// Declare the action space (discrete id or continuous box).
    virtual hne::SpaceSpec actionSpace() const = 0;

    /// Populate `out.data` with the current observation. `out` is pre-sized to
    /// `flat_size(observationSpace())` — do not resize.
    virtual void readObservation(ScriptContext &ctx, hne::Tensor &out) = 0;

    /// Apply the action returned by the policy / chosen by the training agent.
    virtual void applyAction(ScriptContext &ctx, const hne::Action &action, float deltaTime) = 0;

    /// Scalar reward for this step. Defaults to 0 (for pure inference scripts).
    virtual float computeReward(ScriptContext & /*ctx*/, float /*deltaTime*/) { return 0.0f; }

    /// Return true when the current episode has terminated (fell over, reached
    /// goal, died). Training will auto-reset.
    virtual bool isDone(ScriptContext & /*ctx*/) { return false; }

    /// Called at the start of each training episode, after the world state has
    /// been restored. Seed per-episode randomness here.
    virtual void onReset(ScriptContext & /*ctx*/) {}

    /// Introspection hook. The ScriptRuntime uses this to distinguish neural
    /// scripts from legacy ones without an extra `dynamic_cast` on every tick.
    bool isNeural() const { return true; }
  };
}

#endif
