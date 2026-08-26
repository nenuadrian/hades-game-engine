#ifndef HADES_ENGINE_BLUEPRINT_BLUEPRINT_NODE_REGISTRY_HPP
#define HADES_ENGINE_BLUEPRINT_BLUEPRINT_NODE_REGISTRY_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "blueprint_exec_context.hpp"
#include "blueprint_graph.hpp"

namespace hades
{
  enum class BlueprintNodeKind : std::uint8_t
  {
    /// Entry point. No exec inputs; dispatched by name by the runtime.
    Event = 0,
    /// Has at least one exec input and participates in the execution chain.
    Exec,
    /// No exec pins at all. Re-evaluated on demand, immediately before every
    /// exec node that consumes it — the same semantics as Unreal's pure nodes.
    Pure,
  };

  struct BlueprintPinSpec
  {
    std::string name;
    std::string displayName;
    ValueType type = ValueType::Float;
    BlueprintValue defaultValue;
    std::string tooltip;

    const std::string &label() const
    {
      return displayName.empty() ? name : displayName;
    }
  };

  /// The pin layout of one node *instance*. Most node types have a fixed
  /// layout, but variable accessors, custom events and function calls derive
  /// theirs from the asset, so signatures are always resolved per instance.
  struct BlueprintNodeSignature
  {
    std::vector<std::string> execInputs;
    std::vector<std::string> execOutputs;
    std::vector<BlueprintPinSpec> dataInputs;
    std::vector<BlueprintPinSpec> dataOutputs;
    /// Overrides the registry display name for this instance ("Get Speed").
    std::string title;

    int find_exec_input(const std::string &name) const;
    int find_exec_output(const std::string &name) const;
    int find_data_input(const std::string &name) const;
    int find_data_output(const std::string &name) const;
  };

  struct BlueprintSignatureContext
  {
    const Blueprint *blueprint = nullptr;
    /// The function graph the node lives in, or nullptr for the event graph.
    const BlueprintFunction *function = nullptr;
  };

  using BlueprintSignatureFn = void (*)(
      const BlueprintSignatureContext &context,
      const BlueprintNode &node,
      BlueprintNodeSignature &out);

  struct BlueprintNodeType
  {
    /// Stable identifier stored in the asset, e.g. "flow.branch".
    std::string name;
    std::string displayName;
    std::string category;
    std::string tooltip;
    /// Extra words the palette search matches against.
    std::string keywords;

    BlueprintNodeKind kind = BlueprintNodeKind::Exec;

    /// True for nodes that can suspend execution (`Delay`). Latent nodes are
    /// rejected inside user functions, exactly like Unreal does.
    bool latent = false;

    /// Not offered in the palette. Used by nodes the editor places itself
    /// (function entry/result).
    bool hidden = false;

    /// Fixed pin layout. Ignored when `signatureFn` is set.
    BlueprintNodeSignature signature;
    BlueprintSignatureFn signatureFn = nullptr;

    BlueprintNodeFn fn = nullptr;

    /// The event name this node listens for, for `kind == Event`. Custom
    /// events read their name from `node.config["name"]` instead and leave
    /// this empty.
    std::string eventName;
  };

  class BlueprintNodeRegistry
  {
  public:
    static BlueprintNodeRegistry &instance();

    void register_type(BlueprintNodeType type);
    const BlueprintNodeType *find(const std::string &name) const;

    /// Registration order, which is also palette order.
    const std::vector<const BlueprintNodeType *> &all() const { return ordered_; }

    /// Distinct categories in first-registration order.
    std::vector<std::string> categories() const;

  private:
    BlueprintNodeRegistry() = default;

    std::vector<std::unique_ptr<BlueprintNodeType>> storage_;
    std::vector<const BlueprintNodeType *> ordered_;
    std::unordered_map<std::string, const BlueprintNodeType *> byName_;
  };

  /// Populate the registry with the built-in node library. Idempotent, and
  /// called automatically by everything that needs the registry, so callers
  /// never have to remember it.
  void register_builtin_blueprint_nodes();

  /// Resolve the pin layout of a node instance. Returns false when the node
  /// type is unknown (the caller should surface that as a compile error and,
  /// in the editor, draw the node as an error stub).
  bool resolve_blueprint_node_signature(
      const BlueprintSignatureContext &context,
      const BlueprintNode &node,
      BlueprintNodeSignature &out);

  /// Value a data input pin takes when nothing is wired into it: the node's
  /// stored literal if present, otherwise the signature default.
  BlueprintValue blueprint_pin_literal(
      const BlueprintNode &node,
      const BlueprintPinSpec &pin);
}

#endif
