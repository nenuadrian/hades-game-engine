#ifndef HADES_ENGINE_BLUEPRINT_BLUEPRINT_EXEC_CONTEXT_HPP
#define HADES_ENGINE_BLUEPRINT_BLUEPRINT_EXEC_CONTEXT_HPP

#include <cstdint>
#include <string>

#include "../core/ecs/entity.hpp"
#include "blueprint_value.hpp"

namespace hades
{
  class ComponentManager;
  class EntityManager;
  class BlueprintHost;
  class BlueprintVM;
  struct BlueprintNode;
  struct BlueprintFrame;

  /// Scratch storage a node keeps between executions of the same instance.
  /// Deliberately tiny and untyped: every built-in stateful node (Sequence,
  /// ForLoop, DoOnce, Gate, FlipFlop, ...) fits in these five slots, and the
  /// VM can reset the whole table with a single fill.
  struct BlueprintNodeState
  {
    std::int32_t i0 = 0;
    std::int32_t i1 = 0;
    float f0 = 0.0f;
    bool b0 = false;
    bool b1 = false;
  };

  /// What a node tells the VM to do next.
  struct BlueprintExecResult
  {
    /// Index into the node's exec *output* pins, or -1 to end this chain.
    int execOut = -1;
    /// Push this node onto the continuation stack: when the chain started by
    /// `execOut` finishes, the VM re-enters this node with `is_reentry()`
    /// true. This is how Sequence and the loop nodes work.
    bool reenter = false;
    /// Suspend the whole chain and resume `execOut` after `latentSeconds`.
    bool suspend = false;
    float latentSeconds = 0.0f;

    static BlueprintExecResult stop()
    {
      return BlueprintExecResult{};
    }

    static BlueprintExecResult next(int execOut = 0)
    {
      BlueprintExecResult result;
      result.execOut = execOut;
      return result;
    }

    static BlueprintExecResult loop(int execOut)
    {
      BlueprintExecResult result;
      result.execOut = execOut;
      result.reenter = true;
      return result;
    }

    static BlueprintExecResult wait(float seconds, int execOut = 0)
    {
      BlueprintExecResult result;
      result.execOut = execOut;
      result.suspend = true;
      result.latentSeconds = seconds;
      return result;
    }
  };

  /// The view a node function gets of the running graph.
  ///
  /// Constructed by the VM immediately before it calls a node and destroyed
  /// straight after, so it never outlives the frame it points into.
  class BlueprintExecContext
  {
  public:
    BlueprintExecContext(BlueprintVM &vm, BlueprintFrame &frame, int compiledNode)
        : vm_(&vm), frame_(&frame), compiledNode_(compiledNode) {}

    Entity::EntityId entity() const;
    ComponentManager &components() const;
    EntityManager &entities() const;
    BlueprintHost &host() const;

    float delta_time() const;
    float time_seconds() const;

    /// The authored node, for `config` access.
    const BlueprintNode &node() const;

    /// Compile-time resolved indices (variable slot, function slot, target
    /// node, ...). Which one a node uses is part of its contract.
    int aux0() const;
    int aux1() const;

    /// True when the VM re-entered this node from the continuation stack
    /// rather than reaching it along an exec wire.
    bool is_reentry() const { return reentry_; }

    /// Index of the exec *input* pin the chain arrived on.
    int exec_input() const { return execInput_; }

    /// Value on data input pin `index`, already coerced to the pin's type.
    /// Returned by value because the coercion may synthesise a new value.
    BlueprintValue input(int index) const;
    /// How many data input pins this node instance has. Signature-driven
    /// nodes (Call Event) only learn this at runtime.
    int input_count() const;
    void set_output(int index, BlueprintValue value);

    BlueprintNodeState &state();

    /// Graph variable by compile-time slot index.
    BlueprintValue &variable(int index);
    int variable_count() const;

    /// Run another exec chain to completion inside the current frame, used by
    /// `Call Event`. Returns false if the VM aborted (error or depth limit).
    bool call_chain(int compiledNode);

    /// Copy this node's data inputs `[firstInput, firstInput + count)` into
    /// the data output registers of `targetNode`. `Call Event` uses it to hand
    /// a payload to the Custom Event it triggers, landing the values in the
    /// same registers `BlueprintVM::dispatch` writes for an event raised from
    /// outside the graph.
    void write_event_payload(int targetNode, int firstInput, int count);

    /// Invoke user function `functionIndex` with `argumentCount` values taken
    /// from this node's data inputs starting at `firstArgument`, writing the
    /// results into this node's data outputs. Returns false on abort.
    bool call_function(int functionIndex, int firstArgument, int argumentCount);

    /// Abort the whole dispatch with a runtime error.
    void fail(const std::string &message);

    float random_float();
    std::int32_t random_int(std::int32_t minInclusive, std::int32_t maxInclusive);

  private:
    friend class BlueprintVM;

    BlueprintVM *vm_ = nullptr;
    BlueprintFrame *frame_ = nullptr;
    int compiledNode_ = -1;
    int execInput_ = 0;
    bool reentry_ = false;
  };

  using BlueprintNodeFn = BlueprintExecResult (*)(BlueprintExecContext &);
}

#endif
