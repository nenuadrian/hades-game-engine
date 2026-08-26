#ifndef HADES_ENGINE_BLUEPRINT_BLUEPRINT_COMPILER_HPP
#define HADES_ENGINE_BLUEPRINT_BLUEPRINT_COMPILER_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include "blueprint_graph.hpp"
#include "blueprint_node_registry.hpp"

namespace hades
{
  struct BlueprintCompileMessage
  {
    enum class Severity
    {
      Warning,
      Error,
    };

    Severity severity = Severity::Error;
    std::string text;
    /// Graph the message belongs to: empty for the event graph, otherwise the
    /// function name. The editor uses this to jump to the offending node.
    std::string graph;
    BlueprintNodeId node = kInvalidBlueprintNode;
    std::string pin;

    bool is_error() const { return severity == Severity::Error; }
  };

  /// Where a node's data input pin reads from.
  struct CompiledInput
  {
    /// True when the pin has no incoming wire and reads a baked constant.
    bool literal = false;
    /// Literal pool index, or register index.
    int index = -1;
    /// Conversion applied when the wire's source type differs from the pin's.
    ValueType sourceType = ValueType::Wildcard;
    ValueType targetType = ValueType::Wildcard;
  };

  struct CompiledExecTarget
  {
    /// Compiled node index, or -1 when the pin is unwired.
    int node = -1;
    /// Which exec *input* pin of the target the wire lands on.
    int pin = 0;
  };

  struct CompiledNode
  {
    /// Self-contained copy so a compiled graph keeps working even if the
    /// author edits the asset while play mode is running.
    BlueprintNode source;
    const BlueprintNodeType *type = nullptr;
    BlueprintNodeSignature signature;

    std::vector<CompiledInput> inputs;
    /// Register index per data output pin.
    std::vector<int> outputs;
    std::vector<CompiledExecTarget> execTargets;
    /// Pure nodes feeding this node, in evaluation order. Re-run immediately
    /// before every execution of this node, matching Unreal's pure semantics.
    std::vector<int> pureDeps;

    int aux0 = -1;
    int aux1 = -1;

    bool isFunctionResult = false;
  };

  /// One compiled graph: the event graph, or one user function.
  struct CompiledGraphUnit
  {
    std::string name;
    std::vector<CompiledNode> nodes;
    std::vector<BlueprintValue> literals;
    int registerCount = 0;

    /// Event name -> compiled node index. Built-in events use their registry
    /// `eventName`; custom events use `config["name"]`.
    std::unordered_map<std::string, int> eventEntries;

    /// `function.entry` node index, or -1 for the event graph.
    int entryNode = -1;
    /// True when any node in this unit can suspend execution.
    bool hasLatentNodes = false;

    int find_event(const std::string &name) const;
  };

  struct CompiledBlueprint
  {
    /// Owned snapshot of the asset the graph was compiled from.
    Blueprint blueprint;

    CompiledGraphUnit eventGraph;
    /// Parallel to `blueprint.functions`.
    std::vector<CompiledGraphUnit> functions;

    std::vector<BlueprintCompileMessage> messages;
    bool succeeded = false;

    int error_count() const;
    int warning_count() const;

    /// Concatenated errors, one per line. Empty when compilation succeeded.
    std::string error_summary() const;
  };

  /// Compile an asset. Always returns a result: inspect `succeeded` and
  /// `messages`. A failed compile still carries every message found, so the
  /// editor can list them all rather than stopping at the first problem.
  CompiledBlueprint compile_blueprint(const Blueprint &blueprint);
}

#endif
