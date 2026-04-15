#ifndef HADES_ENGINE_TRAINING_HADES_SCRIPT_ENV_HPP
#define HADES_ENGINE_TRAINING_HADES_SCRIPT_ENV_HPP

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <hne/core/environment.hpp>
#include <hne/core/types.hpp>
#include <nlohmann/json.hpp>

#include "../core/ecs/entity.hpp"

namespace hades
{
  class ComponentManager;
  class EntityManager;
  class PhysicsSystem;
  class PhysicsWorld;
  class PolicyRegistry;
  class ScriptRuntime;
  class NeuralScript;

  /// A single script attached to a single entity that should be treated as
  /// a training agent. The MVP drives exactly one subject (the first), but
  /// the vector lives in the config so multi-agent support can be layered on
  /// without an API break.
  struct TrainingSubject
  {
    /// Name of the entity as declared in the saved world. Resolved to an
    /// EntityId after the world loads. (Using the name rather than the ID
    /// lets the config survive world reloads that renumber entities.)
    std::string entityName;

    /// Qualified class name of the NeuralScript attachment to drive. Scripts
    /// with multiple attachments need this to disambiguate.
    std::string className;
  };

  /// `hne::IEnvironment` adapter that loads a Hades world, picks out a
  /// `NeuralScript` attachment, and exposes it as a gym-style environment.
  ///
  /// Lifecycle:
  ///   1. Construct with a `Config` — the env loads the world, starts a
  ///      `TrainingHost` ScriptRuntime, marks the subject as training-owned.
  ///   2. `reset(seed)` destroys the world, restores it from a captured JSON
  ///      snapshot, calls `NeuralScript::onReset`, returns the first observation.
  ///   3. `step(action)` applies the action, ticks other (legacy/inference)
  ///      scripts and physics, reads the next observation, and returns the
  ///      reward/terminated/truncated triplet.
  ///
  /// Each env instance owns its own `EntityManager`/`ComponentManager`/physics,
  /// so a `VectorizedEnv` can run many in parallel without contention. The
  /// policy registry can be shared across envs (via `Config::sharedPolicies`)
  /// so a single `.pt` is loaded once regardless of env count.
  class HadesScriptEnv : public hne::IEnvironment
  {
  public:
    struct Config
    {
      std::filesystem::path workspacePath;
      std::string worldName;

      /// Training subjects — MVP expects exactly one element; future
      /// multi-agent work will consume the full vector without an API change.
      std::vector<TrainingSubject> subjects;

      /// Hard episode cap. When reached, `truncated = true` on the returned
      /// `StepResult` and the env auto-resets on the next step.
      int32_t maxStepsPerEpisode = 1000;

      /// Fixed-step dt (seconds) passed to physics and applyAction. Default
      /// is 60 Hz to match the runtime's PhysicsSystem FIXED_TIMESTEP.
      float tickDt = 1.0f / 60.0f;

      /// Optional non-owning pointer to a shared policy cache — pass the
      /// Trainer-owned registry so parallel envs don't each reload the same
      /// policy.
      PolicyRegistry *sharedPolicies = nullptr;
    };

    explicit HadesScriptEnv(Config config);
    ~HadesScriptEnv() override;

    HadesScriptEnv(const HadesScriptEnv &) = delete;
    HadesScriptEnv &operator=(const HadesScriptEnv &) = delete;

    // hne::IEnvironment ------------------------------------------------------
    [[nodiscard]] hne::SpaceSpec observation_space() const override;
    [[nodiscard]] hne::SpaceSpec action_space() const override;

    hne::Tensor reset(int32_t seed = -1) override;
    hne::StepResult step(const hne::Action &action) override;

    [[nodiscard]] std::string name() const override;

    // Test / introspection hooks --------------------------------------------
    [[nodiscard]] Entity::EntityId subject_entity() const { return subjectEntity_; }
    [[nodiscard]] int32_t steps_this_episode() const { return stepsThisEpisode_; }
    [[nodiscard]] bool is_ready() const { return ready_; }
    [[nodiscard]] const std::string &last_error() const { return lastError_; }

  private:
    bool build_environment(std::string &errorMessage);
    void restore_from_snapshot();
    Entity::EntityId resolve_subject_entity() const;

    Config config_;
    std::unique_ptr<EntityManager> entityManager_;
    std::unique_ptr<ComponentManager> componentManager_;
    std::unique_ptr<PhysicsWorld> physicsWorld_;
    std::unique_ptr<PhysicsSystem> physicsSystem_;
    std::unique_ptr<ScriptRuntime> scriptRuntime_;

    nlohmann::json worldSnapshot_;
    Entity::EntityId worldRoot_ = Entity::INVALID;
    Entity::EntityId subjectEntity_ = Entity::INVALID;
    NeuralScript *subjectScript_ = nullptr;

    hne::SpaceSpec obsSpec_;
    hne::SpaceSpec actSpec_;

    int32_t stepsThisEpisode_ = 0;
    bool ready_ = false;
    std::string lastError_;
  };
}

#endif
