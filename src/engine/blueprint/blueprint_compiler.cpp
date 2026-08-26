#include "blueprint_compiler.hpp"

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace hades
{
  namespace
  {
    constexpr int kWildcardResolutionPasses = 8;

    void add_message(
        std::vector<BlueprintCompileMessage> &messages,
        BlueprintCompileMessage::Severity severity,
        const std::string &graphName,
        BlueprintNodeId node,
        const std::string &pin,
        std::string text)
    {
      BlueprintCompileMessage message;
      message.severity = severity;
      message.graph = graphName;
      message.node = node;
      message.pin = pin;
      message.text = std::move(text);
      messages.push_back(std::move(message));
    }

    /// Compiles one graph (the event graph, or a single user function).
    class UnitCompiler
    {
    public:
      UnitCompiler(
          const Blueprint &blueprint,
          const BlueprintFunction *function,
          CompiledGraphUnit &unit,
          std::vector<BlueprintCompileMessage> &messages)
          : blueprint_(blueprint),
            function_(function),
            graph_(function == nullptr ? blueprint.eventGraph : function->graph),
            unit_(unit),
            messages_(messages),
            graphName_(function == nullptr ? std::string() : function->name)
      {
      }

      bool run();

    private:
      void error(BlueprintNodeId node, const std::string &pin, std::string text)
      {
        add_message(messages_, BlueprintCompileMessage::Severity::Error, graphName_, node, pin, std::move(text));
        failed_ = true;
      }

      void warning(BlueprintNodeId node, const std::string &pin, std::string text)
      {
        add_message(messages_, BlueprintCompileMessage::Severity::Warning, graphName_, node, pin, std::move(text));
      }

      std::string node_label(int index) const;

      void resolve_signatures();
      void resolve_wildcards();
      void allocate_registers();
      void resolve_inputs();
      void resolve_exec_targets();
      void validate_links();
      void collect_events();
      void resolve_aux();
      void schedule_pure_nodes();
      void report_unreachable();

      const Blueprint &blueprint_;
      const BlueprintFunction *function_ = nullptr;
      const BlueprintGraph &graph_;
      CompiledGraphUnit &unit_;
      std::vector<BlueprintCompileMessage> &messages_;
      std::string graphName_;

      std::vector<BlueprintNodeSignature> signatures_;
      std::vector<bool> known_;
      bool failed_ = false;
    };

    std::string UnitCompiler::node_label(int index) const
    {
      const auto &node = unit_.nodes[static_cast<std::size_t>(index)];
      if (!signatures_[static_cast<std::size_t>(index)].title.empty())
      {
        return signatures_[static_cast<std::size_t>(index)].title;
      }
      if (node.type != nullptr)
      {
        return node.type->displayName;
      }
      return node.source.type;
    }

    void UnitCompiler::resolve_signatures()
    {
      register_builtin_blueprint_nodes();
      auto &registry = BlueprintNodeRegistry::instance();

      BlueprintSignatureContext context;
      context.blueprint = &blueprint_;
      context.function = function_;

      unit_.nodes.resize(graph_.nodes.size());
      signatures_.resize(graph_.nodes.size());
      known_.assign(graph_.nodes.size(), false);

      for (std::size_t i = 0; i < graph_.nodes.size(); ++i)
      {
        const BlueprintNode &source = graph_.nodes[i];
        CompiledNode &compiled = unit_.nodes[i];
        compiled.source = source;
        compiled.type = registry.find(source.type);

        if (compiled.type == nullptr)
        {
          error(source.id, {}, "unknown node type '" + source.type + "'");
          continue;
        }

        known_[i] = true;
        resolve_blueprint_node_signature(context, source, signatures_[i]);

        if (compiled.type->latent)
        {
          unit_.hasLatentNodes = true;
          if (function_ != nullptr)
          {
            error(
                source.id,
                {},
                "'" + compiled.type->displayName +
                    "' suspends execution and cannot be used inside a function");
          }
        }

        const bool isFunctionEntry = source.type == "function.entry";
        const bool isFunctionResult = source.type == "function.result";
        compiled.isFunctionResult = isFunctionResult;

        if (function_ == nullptr)
        {
          if (isFunctionEntry || isFunctionResult)
          {
            error(source.id, {}, "'" + compiled.type->displayName + "' only belongs in a function graph");
          }
        }
        else
        {
          if (compiled.type->kind == BlueprintNodeKind::Event && !isFunctionEntry)
          {
            error(source.id, {}, "event nodes are not allowed inside a function");
          }
          if (source.type == "flow.call_event")
          {
            error(source.id, {}, "Call Event is only available in the event graph");
          }
        }
      }

      if (function_ != nullptr)
      {
        int entryCount = 0;
        for (std::size_t i = 0; i < unit_.nodes.size(); ++i)
        {
          if (known_[i] && unit_.nodes[i].source.type == "function.entry")
          {
            ++entryCount;
            unit_.entryNode = static_cast<int>(i);
          }
        }

        if (entryCount == 0)
        {
          error(kInvalidBlueprintNode, {}, "function '" + graphName_ + "' has no entry node");
        }
        else if (entryCount > 1)
        {
          error(kInvalidBlueprintNode, {}, "function '" + graphName_ + "' has more than one entry node");
        }
      }
    }

    void UnitCompiler::resolve_wildcards()
    {
      // Wildcard pins adopt the concrete type of whatever they are wired to,
      // then propagate that type across the rest of the node's wildcards so
      // Select's three pins always agree.
      for (int pass = 0; pass < kWildcardResolutionPasses; ++pass)
      {
        bool changed = false;

        for (std::size_t i = 0; i < unit_.nodes.size(); ++i)
        {
          if (!known_[i])
          {
            continue;
          }

          BlueprintNodeSignature &signature = signatures_[i];
          const BlueprintNodeId id = unit_.nodes[i].source.id;
          ValueType discovered = ValueType::Wildcard;

          for (auto &input : signature.dataInputs)
          {
            if (input.type != ValueType::Wildcard)
            {
              continue;
            }

            const BlueprintLink *link = graph_.incoming_data_link({id, input.name});
            if (link == nullptr)
            {
              continue;
            }

            const int sourceIndex = graph_.node_index(link->from.node);
            if (sourceIndex < 0 || !known_[static_cast<std::size_t>(sourceIndex)])
            {
              continue;
            }

            const auto &sourceSignature = signatures_[static_cast<std::size_t>(sourceIndex)];
            const int pinIndex = sourceSignature.find_data_output(link->from.pin);
            if (pinIndex < 0)
            {
              continue;
            }

            const ValueType type = sourceSignature.dataOutputs[static_cast<std::size_t>(pinIndex)].type;
            if (type == ValueType::Wildcard)
            {
              continue;
            }

            input.type = type;
            discovered = type;
            changed = true;
          }

          for (auto &output : signature.dataOutputs)
          {
            if (output.type != ValueType::Wildcard)
            {
              continue;
            }

            for (const auto &link : graph_.links)
            {
              if (link.kind != BlueprintLinkKind::Data ||
                  link.from.node != id ||
                  link.from.pin != output.name)
              {
                continue;
              }

              const int targetIndex = graph_.node_index(link.to.node);
              if (targetIndex < 0 || !known_[static_cast<std::size_t>(targetIndex)])
              {
                continue;
              }

              const auto &targetSignature = signatures_[static_cast<std::size_t>(targetIndex)];
              const int pinIndex = targetSignature.find_data_input(link.to.pin);
              if (pinIndex < 0)
              {
                continue;
              }

              const ValueType type = targetSignature.dataInputs[static_cast<std::size_t>(pinIndex)].type;
              if (type == ValueType::Wildcard)
              {
                continue;
              }

              output.type = type;
              discovered = type;
              changed = true;
              break;
            }
          }

          if (discovered != ValueType::Wildcard)
          {
            for (auto &input : signature.dataInputs)
            {
              if (input.type == ValueType::Wildcard)
              {
                input.type = discovered;
                changed = true;
              }
            }
            for (auto &output : signature.dataOutputs)
            {
              if (output.type == ValueType::Wildcard)
              {
                output.type = discovered;
                changed = true;
              }
            }
          }
        }

        if (!changed)
        {
          break;
        }
      }

      // Anything still unresolved falls back to the authored literal's own
      // type, or to String, which every value can represent.
      for (std::size_t i = 0; i < unit_.nodes.size(); ++i)
      {
        if (!known_[i])
        {
          continue;
        }

        BlueprintNodeSignature &signature = signatures_[i];
        const BlueprintNode &source = unit_.nodes[i].source;

        for (auto &input : signature.dataInputs)
        {
          if (input.type != ValueType::Wildcard)
          {
            continue;
          }

          const auto it = source.pinDefaults.find(input.name);
          input.type = (it != source.pinDefaults.end() && it->second.type() != ValueType::Wildcard)
                           ? it->second.type()
                           : ValueType::String;
        }

        for (auto &output : signature.dataOutputs)
        {
          if (output.type == ValueType::Wildcard)
          {
            output.type = ValueType::String;
          }
        }
      }
    }

    void UnitCompiler::allocate_registers()
    {
      int nextRegister = 0;
      for (std::size_t i = 0; i < unit_.nodes.size(); ++i)
      {
        auto &compiled = unit_.nodes[i];
        compiled.outputs.clear();
        if (!known_[i])
        {
          continue;
        }

        for (std::size_t j = 0; j < signatures_[i].dataOutputs.size(); ++j)
        {
          compiled.outputs.push_back(nextRegister++);
        }
      }

      unit_.registerCount = nextRegister;
    }

    void UnitCompiler::resolve_inputs()
    {
      for (std::size_t i = 0; i < unit_.nodes.size(); ++i)
      {
        auto &compiled = unit_.nodes[i];
        compiled.inputs.clear();
        if (!known_[i])
        {
          continue;
        }

        const BlueprintNodeId id = compiled.source.id;

        for (const auto &pinSpec : signatures_[i].dataInputs)
        {
          CompiledInput input;
          input.targetType = pinSpec.type;

          int wireCount = 0;
          const BlueprintLink *chosen = nullptr;
          for (const auto &link : graph_.links)
          {
            if (link.kind == BlueprintLinkKind::Data &&
                link.to.node == id &&
                link.to.pin == pinSpec.name)
            {
              ++wireCount;
              if (chosen == nullptr)
              {
                chosen = &link;
              }
            }
          }

          if (wireCount > 1)
          {
            error(
                id,
                pinSpec.name,
                "input pin '" + pinSpec.label() + "' has " + std::to_string(wireCount) +
                    " incoming wires; data inputs accept one");
          }

          if (chosen != nullptr)
          {
            const int sourceIndex = graph_.node_index(chosen->from.node);
            if (sourceIndex < 0 || !known_[static_cast<std::size_t>(sourceIndex)])
            {
              error(id, pinSpec.name, "input pin '" + pinSpec.label() + "' is wired to an invalid node");
            }
            else
            {
              const auto &sourceSignature = signatures_[static_cast<std::size_t>(sourceIndex)];
              const int pinIndex = sourceSignature.find_data_output(chosen->from.pin);
              if (pinIndex < 0)
              {
                error(
                    id,
                    pinSpec.name,
                    "input pin '" + pinSpec.label() + "' is wired to '" + chosen->from.pin +
                        "', which is not an output on " + node_label(sourceIndex));
              }
              else
              {
                const ValueType sourceType =
                    sourceSignature.dataOutputs[static_cast<std::size_t>(pinIndex)].type;

                if (!value_type_convertible(sourceType, pinSpec.type))
                {
                  error(
                      id,
                      pinSpec.name,
                      std::string("cannot connect ") + value_type_name(sourceType) + " to " +
                          value_type_name(pinSpec.type) + " on pin '" + pinSpec.label() + "'");
                }
                else if (value_type_conversion_is_lossy(sourceType, pinSpec.type))
                {
                  warning(
                      id,
                      pinSpec.name,
                      std::string("lossy conversion from ") + value_type_name(sourceType) + " to " +
                          value_type_name(pinSpec.type) + " on pin '" + pinSpec.label() + "'");
                }

                input.literal = false;
                input.index = unit_.nodes[static_cast<std::size_t>(sourceIndex)]
                                  .outputs[static_cast<std::size_t>(pinIndex)];
                input.sourceType = sourceType;
              }
            }
          }

          if (input.index < 0)
          {
            input.literal = true;
            input.sourceType = pinSpec.type;
            input.index = static_cast<int>(unit_.literals.size());
            unit_.literals.push_back(blueprint_pin_literal(compiled.source, pinSpec));
          }

          compiled.inputs.push_back(input);
        }
      }
    }

    void UnitCompiler::resolve_exec_targets()
    {
      for (std::size_t i = 0; i < unit_.nodes.size(); ++i)
      {
        auto &compiled = unit_.nodes[i];
        compiled.execTargets.clear();
        if (!known_[i])
        {
          continue;
        }

        const BlueprintNodeId id = compiled.source.id;

        for (const auto &pinName : signatures_[i].execOutputs)
        {
          CompiledExecTarget target;

          int wireCount = 0;
          const BlueprintLink *chosen = nullptr;
          for (const auto &link : graph_.links)
          {
            if (link.kind == BlueprintLinkKind::Exec &&
                link.from.node == id &&
                link.from.pin == pinName)
            {
              ++wireCount;
              if (chosen == nullptr)
              {
                chosen = &link;
              }
            }
          }

          if (wireCount > 1)
          {
            error(
                id,
                pinName,
                "exec output '" + pinName + "' has " + std::to_string(wireCount) +
                    " outgoing wires; use a Sequence node to fan out");
          }

          if (chosen != nullptr)
          {
            const int targetIndex = graph_.node_index(chosen->to.node);
            if (targetIndex < 0 || !known_[static_cast<std::size_t>(targetIndex)])
            {
              error(id, pinName, "exec output '" + pinName + "' is wired to an invalid node");
            }
            else
            {
              const int execPin =
                  signatures_[static_cast<std::size_t>(targetIndex)].find_exec_input(chosen->to.pin);
              if (execPin < 0)
              {
                error(
                    id,
                    pinName,
                    "exec output '" + pinName + "' is wired to '" + chosen->to.pin +
                        "', which is not an execution input on " + node_label(targetIndex));
              }
              else
              {
                target.node = targetIndex;
                target.pin = execPin;
              }
            }
          }

          compiled.execTargets.push_back(target);
        }
      }
    }

    void UnitCompiler::validate_links()
    {
      // Everything above walks pins and looks for wires. This walks wires and
      // looks for pins, which is what catches links left behind by a node type
      // whose pin set changed.
      for (const auto &link : graph_.links)
      {
        const int fromIndex = graph_.node_index(link.from.node);
        const int toIndex = graph_.node_index(link.to.node);
        if (fromIndex < 0 || toIndex < 0 ||
            !known_[static_cast<std::size_t>(fromIndex)] ||
            !known_[static_cast<std::size_t>(toIndex)])
        {
          continue;
        }

        const auto &fromSignature = signatures_[static_cast<std::size_t>(fromIndex)];
        const auto &toSignature = signatures_[static_cast<std::size_t>(toIndex)];

        if (link.kind == BlueprintLinkKind::Exec)
        {
          if (fromSignature.find_exec_output(link.from.pin) < 0)
          {
            error(
                link.from.node,
                link.from.pin,
                "no execution output named '" + link.from.pin + "' on " + node_label(fromIndex));
          }
          if (toSignature.find_exec_input(link.to.pin) < 0)
          {
            error(
                link.to.node,
                link.to.pin,
                "no execution input named '" + link.to.pin + "' on " + node_label(toIndex));
          }
        }
        else
        {
          if (fromSignature.find_data_output(link.from.pin) < 0)
          {
            error(
                link.from.node,
                link.from.pin,
                "no data output named '" + link.from.pin + "' on " + node_label(fromIndex));
          }
          if (toSignature.find_data_input(link.to.pin) < 0)
          {
            error(
                link.to.node,
                link.to.pin,
                "no data input named '" + link.to.pin + "' on " + node_label(toIndex));
          }
        }
      }
    }

    void UnitCompiler::collect_events()
    {
      for (std::size_t i = 0; i < unit_.nodes.size(); ++i)
      {
        if (!known_[i])
        {
          continue;
        }

        const auto &compiled = unit_.nodes[i];
        if (compiled.type->kind != BlueprintNodeKind::Event)
        {
          continue;
        }

        if (compiled.source.type == "function.entry")
        {
          continue;
        }

        std::string eventName = compiled.type->eventName;
        if (eventName.empty())
        {
          eventName = compiled.source.config.value("name", std::string());
          if (eventName.empty())
          {
            error(compiled.source.id, {}, "custom event needs a name");
            continue;
          }
          eventName = "custom:" + eventName;
        }

        const auto existing = unit_.eventEntries.find(eventName);
        if (existing != unit_.eventEntries.end())
        {
          error(
              compiled.source.id,
              {},
              "duplicate event node: '" + node_label(static_cast<int>(i)) +
                  "' is already handled elsewhere in this graph");
          continue;
        }

        unit_.eventEntries.emplace(eventName, static_cast<int>(i));
      }
    }

    void UnitCompiler::resolve_aux()
    {
      for (std::size_t i = 0; i < unit_.nodes.size(); ++i)
      {
        if (!known_[i])
        {
          continue;
        }

        auto &compiled = unit_.nodes[i];
        const std::string &type = compiled.source.type;

        if (type == "variable.get" || type == "variable.set")
        {
          const std::string name = compiled.source.config.value("variable", std::string());
          const int index = blueprint_.variable_index(name);
          if (index < 0)
          {
            error(
                compiled.source.id,
                {},
                name.empty() ? "variable node has no variable selected"
                             : ("unknown variable '" + name + "'"));
          }
          compiled.aux0 = index;
        }
        else if (type == "flow.sequence")
        {
          compiled.aux0 = static_cast<int>(signatures_[i].execOutputs.size());
        }
        else if (type == "flow.call_event")
        {
          const std::string name = compiled.source.config.value("name", std::string());
          if (name.empty())
          {
            error(compiled.source.id, {}, "Call Event has no target event selected");
          }
          else
          {
            const auto it = unit_.eventEntries.find("custom:" + name);
            if (it == unit_.eventEntries.end())
            {
              error(compiled.source.id, {}, "no Custom Event named '" + name + "' in this graph");
            }
            else
            {
              compiled.aux0 = it->second;
            }
          }
        }
        else if (type == "function.call")
        {
          const std::string name = compiled.source.config.value("function", std::string());
          int functionIndex = -1;
          for (std::size_t f = 0; f < blueprint_.functions.size(); ++f)
          {
            if (blueprint_.functions[f].name == name)
            {
              functionIndex = static_cast<int>(f);
              break;
            }
          }

          if (functionIndex < 0)
          {
            error(
                compiled.source.id,
                {},
                name.empty() ? "Call Function has no function selected"
                             : ("unknown function '" + name + "'"));
          }
          else if (function_ != nullptr && function_->name == name && !function_->allowRecursion)
          {
            error(
                compiled.source.id,
                {},
                "'" + name + "' calls itself; tick 'Allow Recursion' on the function to permit this");
          }

          compiled.aux0 = functionIndex;
          compiled.aux1 = static_cast<int>(signatures_[i].dataInputs.size());
        }
      }
    }

    void UnitCompiler::schedule_pure_nodes()
    {
      const std::size_t count = unit_.nodes.size();

      // Register index -> producing node index, so a data input can be traced
      // back to the node that wrote it.
      std::vector<int> producer(static_cast<std::size_t>(std::max(unit_.registerCount, 0)), -1);
      for (std::size_t i = 0; i < count; ++i)
      {
        for (int registerIndex : unit_.nodes[i].outputs)
        {
          if (registerIndex >= 0 && registerIndex < unit_.registerCount)
          {
            producer[static_cast<std::size_t>(registerIndex)] = static_cast<int>(i);
          }
        }
      }

      const auto pure_predecessors = [&](std::size_t index)
      {
        std::vector<int> result;
        for (const auto &input : unit_.nodes[index].inputs)
        {
          if (input.literal || input.index < 0 || input.index >= unit_.registerCount)
          {
            continue;
          }

          const int source = producer[static_cast<std::size_t>(input.index)];
          if (source < 0)
          {
            continue;
          }

          const auto *type = unit_.nodes[static_cast<std::size_t>(source)].type;
          if (type == nullptr || type->kind != BlueprintNodeKind::Pure)
          {
            continue;
          }

          if (std::find(result.begin(), result.end(), source) == result.end())
          {
            result.push_back(source);
          }
        }
        return result;
      };

      // 0 = unvisited, 1 = on stack, 2 = done. A node found on the stack means
      // the pure sub-graph feeding it contains a cycle, which would otherwise
      // hang the VM.
      std::vector<int> colour(count, 0);
      std::vector<std::vector<int>> closure(count);
      bool reportedCycle = false;

      std::function<void(std::size_t)> visit = [&](std::size_t index)
      {
        if (colour[index] == 2)
        {
          return;
        }

        if (colour[index] == 1)
        {
          if (!reportedCycle)
          {
            reportedCycle = true;
            error(
                unit_.nodes[index].source.id,
                {},
                "circular data dependency through '" + node_label(static_cast<int>(index)) + "'");
          }
          return;
        }

        colour[index] = 1;

        std::vector<int> ordered;
        for (int predecessor : pure_predecessors(index))
        {
          visit(static_cast<std::size_t>(predecessor));

          for (int inherited : closure[static_cast<std::size_t>(predecessor)])
          {
            if (std::find(ordered.begin(), ordered.end(), inherited) == ordered.end())
            {
              ordered.push_back(inherited);
            }
          }
          if (std::find(ordered.begin(), ordered.end(), predecessor) == ordered.end())
          {
            ordered.push_back(predecessor);
          }
        }

        closure[index] = std::move(ordered);
        colour[index] = 2;
      };

      for (std::size_t i = 0; i < count; ++i)
      {
        visit(i);
      }

      for (std::size_t i = 0; i < count; ++i)
      {
        unit_.nodes[i].pureDeps = closure[i];
      }
    }

    void UnitCompiler::report_unreachable()
    {
      const std::size_t count = unit_.nodes.size();
      std::vector<bool> reached(count, false);
      std::vector<int> pending;

      for (const auto &[name, index] : unit_.eventEntries)
      {
        (void)name;
        pending.push_back(index);
      }
      if (unit_.entryNode >= 0)
      {
        pending.push_back(unit_.entryNode);
      }

      while (!pending.empty())
      {
        const int index = pending.back();
        pending.pop_back();
        if (index < 0 || static_cast<std::size_t>(index) >= count ||
            reached[static_cast<std::size_t>(index)])
        {
          continue;
        }

        reached[static_cast<std::size_t>(index)] = true;
        for (const auto &target : unit_.nodes[static_cast<std::size_t>(index)].execTargets)
        {
          pending.push_back(target.node);
        }
        // Call Event hands control to another entry point in the same graph.
        if (unit_.nodes[static_cast<std::size_t>(index)].source.type == "flow.call_event")
        {
          pending.push_back(unit_.nodes[static_cast<std::size_t>(index)].aux0);
        }
      }

      for (std::size_t i = 0; i < count; ++i)
      {
        if (!known_[i] || reached[i])
        {
          continue;
        }

        const auto *type = unit_.nodes[i].type;
        if (type == nullptr || type->kind == BlueprintNodeKind::Pure)
        {
          continue;
        }

        warning(
            unit_.nodes[i].source.id,
            {},
            "'" + node_label(static_cast<int>(i)) + "' is never reached from an event");
      }
    }

    bool UnitCompiler::run()
    {
      unit_.name = graphName_;

      resolve_signatures();
      resolve_wildcards();
      allocate_registers();
      resolve_inputs();
      resolve_exec_targets();
      validate_links();
      collect_events();
      resolve_aux();
      schedule_pure_nodes();
      report_unreachable();

      // Hand the resolved pin layout to the VM: it needs output pin types to
      // keep registers well typed, and the editor's debug view reads titles
      // from it.
      for (std::size_t i = 0; i < unit_.nodes.size(); ++i)
      {
        unit_.nodes[i].signature = signatures_[i];
      }

      return !failed_;
    }
  }

  int CompiledGraphUnit::find_event(const std::string &name) const
  {
    const auto it = eventEntries.find(name);
    return it == eventEntries.end() ? -1 : it->second;
  }

  int CompiledBlueprint::error_count() const
  {
    return static_cast<int>(std::count_if(
        messages.begin(),
        messages.end(),
        [](const BlueprintCompileMessage &message)
        { return message.is_error(); }));
  }

  int CompiledBlueprint::warning_count() const
  {
    return static_cast<int>(messages.size()) - error_count();
  }

  std::string CompiledBlueprint::error_summary() const
  {
    std::string summary;
    for (const auto &message : messages)
    {
      if (!message.is_error())
      {
        continue;
      }

      if (!summary.empty())
      {
        summary += '\n';
      }
      if (!message.graph.empty())
      {
        summary += message.graph + ": ";
      }
      summary += message.text;
    }
    return summary;
  }

  CompiledBlueprint compile_blueprint(const Blueprint &blueprint)
  {
    register_builtin_blueprint_nodes();

    CompiledBlueprint result;
    result.blueprint = blueprint;
    result.blueprint.normalize();

    bool ok = true;

    // Duplicate names would make variable and function lookup ambiguous.
    for (std::size_t i = 0; i < result.blueprint.variables.size(); ++i)
    {
      const auto &variable = result.blueprint.variables[i];
      if (variable.name.empty())
      {
        add_message(
            result.messages, BlueprintCompileMessage::Severity::Error, {}, kInvalidBlueprintNode, {},
            "a variable has no name");
        ok = false;
        continue;
      }

      for (std::size_t j = 0; j < i; ++j)
      {
        if (result.blueprint.variables[j].name == variable.name)
        {
          add_message(
              result.messages, BlueprintCompileMessage::Severity::Error, {}, kInvalidBlueprintNode, {},
              "duplicate variable name '" + variable.name + "'");
          ok = false;
          break;
        }
      }
    }

    for (std::size_t i = 0; i < result.blueprint.functions.size(); ++i)
    {
      const auto &function = result.blueprint.functions[i];
      if (function.name.empty())
      {
        add_message(
            result.messages, BlueprintCompileMessage::Severity::Error, {}, kInvalidBlueprintNode, {},
            "a function has no name");
        ok = false;
        continue;
      }

      for (std::size_t j = 0; j < i; ++j)
      {
        if (result.blueprint.functions[j].name == function.name)
        {
          add_message(
              result.messages, BlueprintCompileMessage::Severity::Error, {}, kInvalidBlueprintNode, {},
              "duplicate function name '" + function.name + "'");
          ok = false;
          break;
        }
      }
    }

    result.functions.resize(result.blueprint.functions.size());
    for (std::size_t i = 0; i < result.blueprint.functions.size(); ++i)
    {
      UnitCompiler compiler(
          result.blueprint,
          &result.blueprint.functions[i],
          result.functions[i],
          result.messages);
      ok = compiler.run() && ok;
    }

    {
      UnitCompiler compiler(result.blueprint, nullptr, result.eventGraph, result.messages);
      ok = compiler.run() && ok;
    }

    result.succeeded = ok;
    return result;
  }
}
