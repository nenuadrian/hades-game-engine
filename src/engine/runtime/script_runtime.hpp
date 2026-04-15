#ifndef HADES_ENGINE_RUNTIME_SCRIPT_RUNTIME_HPP
#define HADES_ENGINE_RUNTIME_SCRIPT_RUNTIME_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../core/ecs/entity.hpp"

namespace hades
{
  class ComponentManager;
  class EntityManager;
  class HadesScript;
  class PolicyRegistry;

  /// Where this ScriptRuntime instance is being used. The role changes how
  /// neural scripts are treated at `start()`:
  ///   - Editor / Runtime: neural scripts MUST have a non-empty `modelPath`
  ///     (policy loaded into Mode::Inference). Missing/invalid → loud fail.
  ///   - TrainingHost: neural scripts with empty `modelPath` are marked
  ///     TrainingOwned so the env adapter can drive them directly.
  enum class ScriptRuntimeRole
  {
    Editor,
    Runtime,
    TrainingHost
  };

  /// How a given script instance is driven on each update. Set during `start()`,
  /// inspected by `update()` and by tests.
  enum class ScriptInstanceMode
  {
    /// Legacy `onUpdate` — the script authors its own logic.
    Legacy,
    /// `readObservation → policy.evaluate → applyAction` — driven by a trained
    /// policy.
    Inference,
    /// Skipped entirely in `update()` — the training env adapter is in charge.
    TrainingOwned
  };

  class ScriptRuntime
  {
  public:
    ScriptRuntime();
    explicit ScriptRuntime(ScriptRuntimeRole role);
    ~ScriptRuntime();

    ScriptRuntime(const ScriptRuntime &) = delete;
    ScriptRuntime &operator=(const ScriptRuntime &) = delete;

    bool start(
        ComponentManager &componentManager,
        EntityManager &entityManager,
        std::string *errorMessage = nullptr);

    bool start(
        ComponentManager &componentManager,
        EntityManager &entityManager,
        const std::filesystem::path &workspaceRoot = {},
        std::string *errorMessage = nullptr);

    bool start(
        ComponentManager &componentManager,
        EntityManager &entityManager,
        const std::filesystem::path &workspaceRoot,
        std::optional<Entity::EntityId> worldRoot,
        std::string *errorMessage = nullptr);

    /// Extended `start` overload used by the training env adapter — the caller
    /// supplies a shared `PolicyRegistry` so multiple envs running in parallel
    /// amortize policy loads. Pass nullptr to use the instance-local registry.
    bool start(
        ComponentManager &componentManager,
        EntityManager &entityManager,
        const std::filesystem::path &workspaceRoot,
        std::optional<Entity::EntityId> worldRoot,
        PolicyRegistry *sharedPolicies,
        std::string *errorMessage = nullptr);

    void update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager);
    void stop();

    bool is_running() const;
    bool faulted() const;
    const std::string &last_error() const;
    void on_key_down(int keyCode);
    void on_key_up(int keyCode);
    void on_mouse_down(int button, float screenX, float screenY);
    void on_mouse_up(int button, float screenX, float screenY);
    void on_mouse_move(float screenX, float screenY);
    void set_viewport_size(float width, float height);

    /// Check if a script has requested a world load. Returns the world name if so.
    static std::optional<std::string> consume_pending_world_load();

    /// Collect observed variables set by scripts via HadesAPI.Observe().
    /// Returns a JSON object string, e.g. {"score":10,"health":100}.
    std::string collect_observations() const;

    /// Compile the given .cpp source files without loading or running them.
    /// Returns true on success. On failure, sets errorMessage with compiler output.
    static bool compile(
        const std::vector<std::filesystem::path> &sourceFiles,
        std::string *errorMessage = nullptr);

    // -----------------------------------------------------------------------
    // Introspection / training-host integration
    // -----------------------------------------------------------------------

    /// Return the first script instance attached to `entityId`, or nullptr.
    /// Primarily used by the training env adapter to resolve the subject
    /// script pointer after `start()`.
    HadesScript *find_script(Entity::EntityId entityId) const;

    /// Mark the given entity's script(s) as `TrainingOwned` so `update()`
    /// leaves them alone — the training env drives them directly via the
    /// NeuralScript hooks.
    void mark_training_owned(Entity::EntityId entityId);

    /// Dispatch mode assigned to the given entity's first script. Returns
    /// Legacy if the entity has no tracked scripts. Used by tests.
    ScriptInstanceMode mode_for(Entity::EntityId entityId) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };
}

#endif
