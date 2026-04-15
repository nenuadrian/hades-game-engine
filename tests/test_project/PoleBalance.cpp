// Classic cart-pole benchmark written as a Hades NeuralScript.
//
// Observation (4 floats): [cart x, cart x-dot, pole angle, pole ang-vel],
// each normalized into roughly [-1, 1] via `hades::normalize`.
// Action: Discrete(2) — push left (0) or right (1).
// Reward: +1 per surviving step; episode ends when |angle| > ~12° or the cart
// leaves the rails.
//
// Train with `Window → Neural Training`, then drop the exported policy path
// into the Cart entity's script attachment Model Path field and hit Play.

#include "engine/hades.hpp"
#include "engine/hades_neural.hpp"

class PoleBalance : public hades::NeuralScript
{
public:
  static constexpr float kForceMag = 10.0f;
  static constexpr float kCartXLimit = 2.4f;
  static constexpr float kAngleLimitRad = 12.0f * 3.14159265f / 180.0f;

  hades::SpaceSpec observationSpace() const override
  {
    hades::BoxSpace box;
    box.shape = {4};
    box.low = {-1.0f, -1.0f, -1.0f, -1.0f};
    box.high = {1.0f, 1.0f, 1.0f, 1.0f};
    return box;
  }

  hades::SpaceSpec actionSpace() const override
  {
    hades::DiscreteSpace d;
    d.n = 2;
    return d;
  }

  void onReset(hades::ScriptContext &ctx) override
  {
    cartX_ = 0.0f;
    cartVel_ = 0.0f;
    angle_ = 0.02f; // tiny bias so the agent has to learn to correct
    angVel_ = 0.0f;
    stepsSurvived_ = 0;

    // Snap the attached entity's position back to the origin so the visual
    // tracks the simulated cart state.
    auto &pos = ctx.componentManager.getComponent<hades::PositionComponent3D>(ctx.entityId);
    pos.x = cartX_;
  }

  void readObservation(hades::ScriptContext & /*ctx*/, hades::Tensor &out) override
  {
    hades::write_obs(out, 0, hades::normalize(cartX_, -kCartXLimit, kCartXLimit));
    hades::write_obs(out, 1, hades::normalize(cartVel_, -3.0f, 3.0f));
    hades::write_obs(out, 2, hades::normalize(angle_, -kAngleLimitRad, kAngleLimitRad));
    hades::write_obs(out, 3, hades::normalize(angVel_, -4.0f, 4.0f));
  }

  void applyAction(hades::ScriptContext &ctx, const hades::Action &action, float dt) override
  {
    const int direction = action.is_discrete() ? (action.as_discrete() == 0 ? -1 : 1) : 1;
    const float force = kForceMag * static_cast<float>(direction);

    // Minimal cart-pole dynamics (Barto 1983 style). Not physically accurate
    // beyond a few decimals but good enough to teach PPO.
    const float g = 9.81f;
    const float mc = 1.0f;  // cart mass
    const float mp = 0.1f;  // pole mass
    const float l = 0.5f;   // half pole length
    const float total = mc + mp;
    const float cosT = std::cos(angle_);
    const float sinT = std::sin(angle_);
    const float temp = (force + mp * l * angVel_ * angVel_ * sinT) / total;
    const float angAcc = (g * sinT - cosT * temp) /
                         (l * (4.0f / 3.0f - mp * cosT * cosT / total));
    const float cartAcc = temp - mp * l * angAcc * cosT / total;

    cartX_ += dt * cartVel_;
    cartVel_ += dt * cartAcc;
    angle_ += dt * angVel_;
    angVel_ += dt * angAcc;

    auto &pos = ctx.componentManager.getComponent<hades::PositionComponent3D>(ctx.entityId);
    pos.x = cartX_;
    ++stepsSurvived_;
  }

  float computeReward(hades::ScriptContext & /*ctx*/, float /*dt*/) override
  {
    return 1.0f;
  }

  bool isDone(hades::ScriptContext & /*ctx*/) override
  {
    return std::abs(cartX_) > kCartXLimit ||
           std::abs(angle_) > kAngleLimitRad;
  }

private:
  float cartX_ = 0.0f;
  float cartVel_ = 0.0f;
  float angle_ = 0.0f;
  float angVel_ = 0.0f;
  int stepsSurvived_ = 0;
};

HADES_REGISTER_SCRIPT(PoleBalance)
