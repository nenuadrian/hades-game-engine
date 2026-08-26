#include "blueprint_vm.hpp"

#include <algorithm>
#include <utility>

#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"
#include "blueprint_host.hpp"

namespace hades
{
  namespace
  {
    constexpr float kNeverExecuted = -1.0e9f;
  }

  BlueprintHost &null_blueprint_host()
  {
    static BlueprintHost host;
    return host;
  }

  void BlueprintInstance::reset()
  {
    latentActions.clear();
    elapsedSeconds = 0.0f;
    started = false;
    faulted = false;
    error.clear();

    if (compiled == nullptr)
    {
      return;
    }

    variables.clear();
    variables.reserve(compiled->blueprint.variables.size());
    for (const auto &variable : compiled->blueprint.variables)
    {
      variables.push_back(variable.defaultValue.coerced_to(variable.type));
    }

    eventRegisters.assign(static_cast<std::size_t>(std::max(compiled->eventGraph.registerCount, 0)), BlueprintValue());
    eventStates.assign(compiled->eventGraph.nodes.size(), BlueprintNodeState());
    nodeLastExecuted.assign(compiled->eventGraph.nodes.size(), kNeverExecuted);
  }

  bool BlueprintInstance::node_active(int nodeIndex, float window) const
  {
    if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= nodeLastExecuted.size())
    {
      return false;
    }

    return (elapsedSeconds - nodeLastExecuted[static_cast<std::size_t>(nodeIndex)]) <= window;
  }

  BlueprintInstance make_blueprint_instance(
      const CompiledBlueprint &compiled,
      Entity::EntityId entity)
  {
    BlueprintInstance instance;
    instance.entity = entity;
    instance.compiled = &compiled;
    instance.reset();
    return instance;
  }

  // -------------------------------------------------------------------------
  // BlueprintExecContext — a thin view onto the VM and the active frame.
  // -------------------------------------------------------------------------

  Entity::EntityId BlueprintExecContext::entity() const
  {
    return frame_->instance->entity;
  }

  ComponentManager &BlueprintExecContext::components() const
  {
    return vm_->components();
  }

  EntityManager &BlueprintExecContext::entities() const
  {
    return vm_->entities();
  }

  BlueprintHost &BlueprintExecContext::host() const
  {
    return vm_->host();
  }

  float BlueprintExecContext::delta_time() const
  {
    return vm_->delta_time();
  }

  float BlueprintExecContext::time_seconds() const
  {
    return frame_->instance->elapsedSeconds;
  }

  const BlueprintNode &BlueprintExecContext::node() const
  {
    return frame_->unit->nodes[static_cast<std::size_t>(compiledNode_)].source;
  }

  int BlueprintExecContext::aux0() const
  {
    return frame_->unit->nodes[static_cast<std::size_t>(compiledNode_)].aux0;
  }

  int BlueprintExecContext::aux1() const
  {
    return frame_->unit->nodes[static_cast<std::size_t>(compiledNode_)].aux1;
  }

  BlueprintValue BlueprintExecContext::input(int index) const
  {
    return vm_->read_input(*frame_, compiledNode_, index);
  }

  int BlueprintExecContext::input_count() const
  {
    return static_cast<int>(
        frame_->unit->nodes[static_cast<std::size_t>(compiledNode_)].inputs.size());
  }

  void BlueprintExecContext::set_output(int index, BlueprintValue value)
  {
    vm_->write_output(*frame_, compiledNode_, index, std::move(value));
  }

  void BlueprintExecContext::write_event_payload(int targetNode, int firstInput, int count)
  {
    const auto &nodes = frame_->unit->nodes;
    if (targetNode < 0 || static_cast<std::size_t>(targetNode) >= nodes.size() ||
        firstInput < 0 || count <= 0)
    {
      return;
    }

    // The two sides derive their pins from the same `params` config, but an
    // asset edited by hand can still disagree, so take the shorter of the two.
    const std::size_t limit =
        std::min(static_cast<std::size_t>(count), nodes[static_cast<std::size_t>(targetNode)].outputs.size());

    for (std::size_t i = 0; i < limit; ++i)
    {
      vm_->write_output(
          *frame_,
          targetNode,
          static_cast<int>(i),
          input(firstInput + static_cast<int>(i)));
    }
  }

  BlueprintNodeState &BlueprintExecContext::state()
  {
    return (*frame_->states)[static_cast<std::size_t>(compiledNode_)];
  }

  BlueprintValue &BlueprintExecContext::variable(int index)
  {
    auto &variables = frame_->instance->variables;
    if (index < 0 || static_cast<std::size_t>(index) >= variables.size())
    {
      // The compiler rejects unknown variables, so this is only reachable if an
      // instance outlived a recompile. Hand back a scratch slot rather than
      // reading out of bounds; writes to it are discarded.
      vm_->invalidVariable_ = BlueprintValue();
      return vm_->invalidVariable_;
    }

    return variables[static_cast<std::size_t>(index)];
  }

  int BlueprintExecContext::variable_count() const
  {
    return static_cast<int>(frame_->instance->variables.size());
  }

  bool BlueprintExecContext::call_chain(int compiledNode)
  {
    if (compiledNode < 0)
    {
      return true;
    }

    if (vm_->callDepth_ >= BlueprintVM::kMaxCallDepth)
    {
      fail("Blueprint call depth limit reached");
      return false;
    }

    ++vm_->callDepth_;
    const bool ok = vm_->execute_from(*frame_, compiledNode, 0, {});
    --vm_->callDepth_;
    return ok;
  }

  bool BlueprintExecContext::call_function(int functionIndex, int firstArgument, int argumentCount)
  {
    BlueprintInstance &instance = *frame_->instance;
    const CompiledBlueprint *compiled = instance.compiled;

    if (compiled == nullptr ||
        functionIndex < 0 ||
        static_cast<std::size_t>(functionIndex) >= compiled->functions.size())
    {
      fail("Call Function targets a function that is not compiled");
      return false;
    }

    if (vm_->callDepth_ >= BlueprintVM::kMaxCallDepth)
    {
      fail("Blueprint call depth limit reached (recursive function without a base case?)");
      return false;
    }

    const CompiledGraphUnit &unit = compiled->functions[static_cast<std::size_t>(functionIndex)];
    if (unit.entryNode < 0)
    {
      fail("function '" + unit.name + "' has no entry node");
      return false;
    }

    // Functions get a fresh register file and fresh node scratch on every
    // call, so recursion and re-entrancy behave the way callers expect.
    std::vector<BlueprintValue> registers(static_cast<std::size_t>(std::max(unit.registerCount, 0)));
    std::vector<BlueprintNodeState> states(unit.nodes.size());

    BlueprintFrame child;
    child.unit = &unit;
    child.registers = &registers;
    child.states = &states;
    child.instance = &instance;

    const CompiledNode &entry = unit.nodes[static_cast<std::size_t>(unit.entryNode)];
    const std::size_t argumentLimit =
        std::min<std::size_t>(static_cast<std::size_t>(std::max(argumentCount, 0)), entry.outputs.size());
    for (std::size_t i = 0; i < argumentLimit; ++i)
    {
      const int registerIndex = entry.outputs[i];
      if (registerIndex >= 0 && static_cast<std::size_t>(registerIndex) < registers.size())
      {
        registers[static_cast<std::size_t>(registerIndex)] =
            input(firstArgument + static_cast<int>(i))
                .coerced_to(entry.signature.dataOutputs[i].type);
      }
    }

    ++vm_->callDepth_;
    const bool ok = vm_->execute_from(child, unit.entryNode, 0, {});
    --vm_->callDepth_;

    if (!ok)
    {
      return false;
    }

    const auto &declared = compiled->blueprint.functions[static_cast<std::size_t>(functionIndex)].outputs;
    for (std::size_t i = 0; i < declared.size(); ++i)
    {
      BlueprintValue value = i < child.returnValues.size()
                                 ? child.returnValues[i]
                                 : declared[i].defaultValue;
      set_output(static_cast<int>(i), value.coerced_to(declared[i].type));
    }

    return true;
  }

  void BlueprintExecContext::fail(const std::string &message)
  {
    vm_->abort(*frame_->instance, message);
  }

  float BlueprintExecContext::random_float()
  {
    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
    return distribution(vm_->random_);
  }

  std::int32_t BlueprintExecContext::random_int(std::int32_t minInclusive, std::int32_t maxInclusive)
  {
    if (minInclusive > maxInclusive)
    {
      std::swap(minInclusive, maxInclusive);
    }

    std::uniform_int_distribution<std::int32_t> distribution(minInclusive, maxInclusive);
    return distribution(vm_->random_);
  }

  // -------------------------------------------------------------------------
  // BlueprintVM
  // -------------------------------------------------------------------------

  BlueprintVM::BlueprintVM(
      ComponentManager &componentManager,
      EntityManager &entityManager,
      BlueprintHost &host)
      : componentManager_(&componentManager),
        entityManager_(&entityManager),
        host_(&host)
  {
  }

  BlueprintValue BlueprintVM::read_input(
      const BlueprintFrame &frame,
      int nodeIndex,
      int inputIndex) const
  {
    const CompiledNode &node = frame.unit->nodes[static_cast<std::size_t>(nodeIndex)];
    if (inputIndex < 0 || static_cast<std::size_t>(inputIndex) >= node.inputs.size())
    {
      return BlueprintValue();
    }

    const CompiledInput &input = node.inputs[static_cast<std::size_t>(inputIndex)];
    if (input.index < 0)
    {
      return BlueprintValue::default_for(input.targetType);
    }

    if (input.literal)
    {
      // Literals are already stored in the pin's type by the compiler.
      return frame.unit->literals[static_cast<std::size_t>(input.index)];
    }

    const BlueprintValue &stored = (*frame.registers)[static_cast<std::size_t>(input.index)];
    if (input.sourceType == input.targetType)
    {
      return stored;
    }

    return stored.coerced_to(input.targetType);
  }

  void BlueprintVM::write_output(
      BlueprintFrame &frame,
      int nodeIndex,
      int outputIndex,
      BlueprintValue value)
  {
    const CompiledNode &node = frame.unit->nodes[static_cast<std::size_t>(nodeIndex)];
    if (outputIndex < 0 || static_cast<std::size_t>(outputIndex) >= node.outputs.size())
    {
      return;
    }

    const int registerIndex = node.outputs[static_cast<std::size_t>(outputIndex)];
    if (registerIndex < 0 || static_cast<std::size_t>(registerIndex) >= frame.registers->size())
    {
      return;
    }

    const ValueType declared = node.signature.dataOutputs[static_cast<std::size_t>(outputIndex)].type;
    (*frame.registers)[static_cast<std::size_t>(registerIndex)] =
        value.type() == declared ? std::move(value) : value.coerced_to(declared);
  }

  void BlueprintVM::evaluate_pure(BlueprintFrame &frame, int nodeIndex)
  {
    const CompiledNode &node = frame.unit->nodes[static_cast<std::size_t>(nodeIndex)];
    if (node.type == nullptr || node.type->fn == nullptr)
    {
      return;
    }

    BlueprintExecContext context(*this, frame, nodeIndex);
    node.type->fn(context);
  }

  void BlueprintVM::run_node_dependencies(BlueprintFrame &frame, int nodeIndex)
  {
    // `pureDeps` is a topologically ordered transitive closure, so a single
    // forward pass is enough — no recursion, no revisiting.
    for (int dependency : frame.unit->nodes[static_cast<std::size_t>(nodeIndex)].pureDeps)
    {
      evaluate_pure(frame, dependency);
      if (aborted_)
      {
        return;
      }
    }
  }

  void BlueprintVM::abort(BlueprintInstance &instance, const std::string &message)
  {
    aborted_ = true;
    instance.faulted = true;
    instance.error = message;
    host_->report_error(instance.entity, message);
  }

  bool BlueprintVM::execute_from(
      BlueprintFrame &frame,
      int startNode,
      int startExecInput,
      std::vector<BlueprintContinuation> continuations)
  {
    BlueprintInstance &instance = *frame.instance;
    const CompiledGraphUnit &unit = *frame.unit;
    const bool isEventGraph = instance.compiled != nullptr && &unit == &instance.compiled->eventGraph;

    int current = startNode;
    int currentExecInput = startExecInput;
    bool currentReentry = false;

    while (true)
    {
      while (current >= 0 && static_cast<std::size_t>(current) < unit.nodes.size())
      {
        if (budget_ == 0)
        {
          abort(instance, "Blueprint execution budget exhausted — check for an unbounded loop");
          return false;
        }
        --budget_;

        const CompiledNode &node = unit.nodes[static_cast<std::size_t>(current)];
        if (node.type == nullptr || node.type->fn == nullptr)
        {
          abort(instance, "Blueprint reached a node with no implementation");
          return false;
        }

        run_node_dependencies(frame, current);
        if (aborted_)
        {
          return false;
        }

        if (isEventGraph && static_cast<std::size_t>(current) < instance.nodeLastExecuted.size())
        {
          instance.nodeLastExecuted[static_cast<std::size_t>(current)] = instance.elapsedSeconds;
        }

        BlueprintExecContext context(*this, frame, current);
        context.execInput_ = currentExecInput;
        context.reentry_ = currentReentry;

        const BlueprintExecResult result = node.type->fn(context);
        if (aborted_)
        {
          return false;
        }

        if (node.isFunctionResult)
        {
          frame.returnValues.clear();
          frame.returnValues.reserve(node.inputs.size());
          for (std::size_t i = 0; i < node.inputs.size(); ++i)
          {
            frame.returnValues.push_back(read_input(frame, current, static_cast<int>(i)));
          }
          frame.hasReturn = true;
          return true;
        }

        int nextNode = -1;
        int nextExecInput = 0;
        if (result.execOut >= 0 && static_cast<std::size_t>(result.execOut) < node.execTargets.size())
        {
          nextNode = node.execTargets[static_cast<std::size_t>(result.execOut)].node;
          nextExecInput = node.execTargets[static_cast<std::size_t>(result.execOut)].pin;
        }

        if (result.suspend)
        {
          if (!isEventGraph)
          {
            abort(instance, "a latent node suspended outside the event graph");
            return false;
          }

          // Queue the resume even when the latent node's output pin is
          // unwired: pending continuations (an enclosing loop, a Sequence)
          // still have to run once the wait elapses.
          if (nextNode >= 0 || !continuations.empty())
          {
            BlueprintLatentAction action;
            action.resumeNode = nextNode;
            action.resumeExecInput = nextExecInput;
            action.remainingSeconds = result.latentSeconds;
            action.continuations = std::move(continuations);
            instance.latentActions.push_back(std::move(action));
          }

          return true;
        }

        if (result.reenter)
        {
          continuations.push_back(BlueprintContinuation{current, currentExecInput});
        }

        current = nextNode;
        currentExecInput = nextExecInput;
        currentReentry = false;
      }

      if (continuations.empty())
      {
        break;
      }

      const BlueprintContinuation resumed = continuations.back();
      continuations.pop_back();
      current = resumed.node;
      currentExecInput = resumed.execInput;
      currentReentry = true;
    }

    return true;
  }

  bool BlueprintVM::dispatch(
      BlueprintInstance &instance,
      const std::string &eventName,
      const std::vector<BlueprintValue> &payload)
  {
    if (instance.compiled == nullptr || instance.faulted)
    {
      return false;
    }

    const CompiledGraphUnit &unit = instance.compiled->eventGraph;
    const int entry = unit.find_event(eventName);
    if (entry < 0)
    {
      return false;
    }

    // The register file and node scratch must be the right size even if the
    // instance was created before a recompile.
    if (instance.eventRegisters.size() != static_cast<std::size_t>(std::max(unit.registerCount, 0)))
    {
      instance.eventRegisters.assign(static_cast<std::size_t>(std::max(unit.registerCount, 0)), BlueprintValue());
    }
    if (instance.eventStates.size() != unit.nodes.size())
    {
      instance.eventStates.assign(unit.nodes.size(), BlueprintNodeState());
    }
    if (instance.nodeLastExecuted.size() != unit.nodes.size())
    {
      instance.nodeLastExecuted.assign(unit.nodes.size(), kNeverExecuted);
    }

    BlueprintFrame frame;
    frame.unit = &unit;
    frame.registers = &instance.eventRegisters;
    frame.states = &instance.eventStates;
    frame.instance = &instance;

    // Event payloads land directly in the event node's output registers.
    const CompiledNode &node = unit.nodes[static_cast<std::size_t>(entry)];
    const std::size_t limit = std::min(payload.size(), node.outputs.size());
    for (std::size_t i = 0; i < limit; ++i)
    {
      write_output(frame, entry, static_cast<int>(i), payload[i]);
    }

    aborted_ = false;
    budget_ = kNodeBudgetPerDispatch;
    callDepth_ = 0;

    return execute_from(frame, entry, 0, {});
  }

  void BlueprintVM::advance_latent_actions(BlueprintInstance &instance, float deltaTime)
  {
    if (instance.compiled == nullptr || instance.faulted || instance.latentActions.empty())
    {
      return;
    }

    // Take the list by value: resuming a chain can queue new latent actions,
    // and those must wait for the next tick rather than fire immediately.
    std::vector<BlueprintLatentAction> pending;
    pending.swap(instance.latentActions);

    BlueprintFrame frame;
    frame.unit = &instance.compiled->eventGraph;
    frame.registers = &instance.eventRegisters;
    frame.states = &instance.eventStates;
    frame.instance = &instance;

    for (auto &action : pending)
    {
      action.remainingSeconds -= deltaTime;
      if (action.remainingSeconds > 0.0f)
      {
        instance.latentActions.push_back(std::move(action));
        continue;
      }

      aborted_ = false;
      budget_ = kNodeBudgetPerDispatch;
      callDepth_ = 0;

      execute_from(frame, action.resumeNode, action.resumeExecInput, std::move(action.continuations));
      if (instance.faulted)
      {
        return;
      }
    }
  }
}
