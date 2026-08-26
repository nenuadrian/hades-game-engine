#ifndef HADES_ENGINE_BLUEPRINT_BLUEPRINT_VM_HPP
#define HADES_ENGINE_BLUEPRINT_BLUEPRINT_VM_HPP

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "blueprint_compiler.hpp"
#include "blueprint_exec_context.hpp"

namespace hades
{
  class ComponentManager;
  class EntityManager;
  class BlueprintHost;

  /// A chain the VM must come back to once the chain it spawned finishes.
  /// Sequence and the loop nodes push one of these.
  struct BlueprintContinuation
  {
    int node = -1;
    int execInput = 0;
  };

  /// A suspended chain waiting on a `Delay`.
  struct BlueprintLatentAction
  {
    int resumeNode = -1;
    int resumeExecInput = 0;
    float remainingSeconds = 0.0f;
    std::vector<BlueprintContinuation> continuations;
  };

  /// Register file + node scratch for one graph activation.
  struct BlueprintFrame
  {
    const CompiledGraphUnit *unit = nullptr;
    std::vector<BlueprintValue> *registers = nullptr;
    std::vector<BlueprintNodeState> *states = nullptr;
    class BlueprintInstance *instance = nullptr;

    /// Filled by a `Return` node and read back by the caller.
    std::vector<BlueprintValue> returnValues;
    bool hasReturn = false;
  };

  /// One Blueprint running on one entity.
  ///
  /// The event graph's registers live here rather than on the stack so a
  /// `Delay` can suspend mid-chain and pick the same values back up seconds
  /// later.
  class BlueprintInstance
  {
  public:
    Entity::EntityId entity = Entity::INVALID;
    const CompiledBlueprint *compiled = nullptr;

    std::vector<BlueprintValue> variables;
    std::vector<BlueprintValue> eventRegisters;
    std::vector<BlueprintNodeState> eventStates;
    std::vector<BlueprintLatentAction> latentActions;

    float elapsedSeconds = 0.0f;
    bool started = false;
    bool faulted = false;
    std::string error;

    /// `elapsedSeconds` at which each event-graph node last ran, or a large
    /// negative number if never. Drives the editor's live wire highlighting.
    std::vector<float> nodeLastExecuted;

    /// Reset registers, variables and latent state back to defaults.
    void reset();

    /// True when the given event graph node ran within `window` seconds.
    bool node_active(int nodeIndex, float window) const;
  };

  /// Build a runnable instance. `compiled` must outlive it.
  BlueprintInstance make_blueprint_instance(
      const CompiledBlueprint &compiled,
      Entity::EntityId entity);

  /// Executes compiled graphs. One VM serves every instance; per-instance
  /// state lives in `BlueprintInstance`.
  class BlueprintVM
  {
  public:
    /// Beyond this many node executions in a single dispatch the VM assumes
    /// the graph has an unbounded loop and aborts with an error rather than
    /// hanging the editor.
    static constexpr std::uint64_t kNodeBudgetPerDispatch = 250000;
    static constexpr int kMaxCallDepth = 64;

    BlueprintVM(ComponentManager &componentManager, EntityManager &entityManager, BlueprintHost &host);

    void set_delta_time(float deltaTime) { deltaTime_ = deltaTime; }
    float delta_time() const { return deltaTime_; }

    /// Fixing the seed makes graphs that use the random nodes reproducible,
    /// which the unit tests rely on.
    void set_random_seed(std::uint32_t seed) { random_.seed(seed); }

    ComponentManager &components() const { return *componentManager_; }
    EntityManager &entities() const { return *entityManager_; }
    BlueprintHost &host() const { return *host_; }

    /// Swap the service host without tearing the VM down, so installing a host
    /// mid-session cannot strand running instances.
    void set_host(BlueprintHost &host) { host_ = &host; }

    /// Fire a named event. `payload` is written positionally into the event
    /// node's data output pins. Returns false when the graph has no handler
    /// for the event, or when execution aborted.
    bool dispatch(
        BlueprintInstance &instance,
        const std::string &eventName,
        const std::vector<BlueprintValue> &payload = {});

    /// Count down suspended chains and resume the ones that came due.
    void advance_latent_actions(BlueprintInstance &instance, float deltaTime);

  private:
    friend class BlueprintExecContext;

    bool execute_from(
        BlueprintFrame &frame,
        int startNode,
        int startExecInput,
        std::vector<BlueprintContinuation> continuations);

    void evaluate_pure(BlueprintFrame &frame, int nodeIndex);
    void run_node_dependencies(BlueprintFrame &frame, int nodeIndex);

    BlueprintValue read_input(const BlueprintFrame &frame, int nodeIndex, int inputIndex) const;
    void write_output(BlueprintFrame &frame, int nodeIndex, int outputIndex, BlueprintValue value);

    void abort(BlueprintInstance &instance, const std::string &message);

    ComponentManager *componentManager_ = nullptr;
    EntityManager *entityManager_ = nullptr;
    BlueprintHost *host_ = nullptr;

    float deltaTime_ = 0.0f;
    /// Handed back by `BlueprintExecContext::variable` when a slot index is out
    /// of range. A VM-owned scratch rather than a function-local static, so one
    /// misbehaving graph cannot leak a value into another VM's instances.
    BlueprintValue invalidVariable_;
    std::uint64_t budget_ = 0;
    int callDepth_ = 0;
    bool aborted_ = false;
    std::mt19937 random_{0x9E3779B9u};
  };
}

#endif
