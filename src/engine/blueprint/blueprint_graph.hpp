#ifndef HADES_ENGINE_BLUEPRINT_BLUEPRINT_GRAPH_HPP
#define HADES_ENGINE_BLUEPRINT_BLUEPRINT_GRAPH_HPP

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "blueprint_value.hpp"

namespace hades
{
  using BlueprintNodeId = std::uint32_t;

  inline constexpr BlueprintNodeId kInvalidBlueprintNode = 0;

  /// Execution wires order node execution; data wires carry values. They are
  /// kept in the same link list but never connect to each other.
  enum class BlueprintLinkKind : std::uint8_t
  {
    Exec = 0,
    Data,
  };

  /// A named variable owned by the Blueprint. Also used to describe the typed
  /// parameters of a user function, which behave identically from the point of
  /// view of the editor's details panel.
  struct BlueprintVariable
  {
    std::string name;
    ValueType type = ValueType::Float;
    BlueprintValue defaultValue = BlueprintValue::from_float(0.0f);
    /// Exposed variables can be overridden per entity from the inspector,
    /// the same way a script's public fields can.
    bool exposed = true;
    std::string tooltip;
  };

  /// One placed node. `type` names an entry in the node registry; `config`
  /// carries type-specific settings (which variable a Get reads, which event a
  /// Call Event targets, ...) and participates in signature resolution.
  struct BlueprintNode
  {
    BlueprintNodeId id = kInvalidBlueprintNode;
    std::string type;
    float x = 0.0f;
    float y = 0.0f;
    std::string comment;
    nlohmann::json config = nlohmann::json::object();
    /// Literal values for data input pins that have no incoming wire, keyed by
    /// pin name. Pins missing from the map fall back to the signature default.
    std::map<std::string, BlueprintValue> pinDefaults;
  };

  struct BlueprintPinRef
  {
    BlueprintNodeId node = kInvalidBlueprintNode;
    std::string pin;

    bool operator==(const BlueprintPinRef &other) const
    {
      return node == other.node && pin == other.pin;
    }
  };

  struct BlueprintLink
  {
    BlueprintLinkKind kind = BlueprintLinkKind::Exec;
    /// Output side of the wire (source).
    BlueprintPinRef from;
    /// Input side of the wire (destination).
    BlueprintPinRef to;

    bool operator==(const BlueprintLink &other) const
    {
      return kind == other.kind && from == other.from && to == other.to;
    }
  };

  /// A node/link soup. The event graph and every user function each own one.
  struct BlueprintGraph
  {
    std::vector<BlueprintNode> nodes;
    std::vector<BlueprintLink> links;

    BlueprintNode *find_node(BlueprintNodeId id);
    const BlueprintNode *find_node(BlueprintNodeId id) const;
    int node_index(BlueprintNodeId id) const;

    /// Remove the node and every wire touching it. No-op if absent.
    void remove_node(BlueprintNodeId id);

    /// Drop the wire, if present. Returns true when something was removed.
    bool remove_link(const BlueprintLink &link);

    /// Remove every wire whose *input* side is this pin. Data input pins and
    /// exec input pins differ here: a data input accepts one wire, an exec
    /// input accepts many, and an exec *output* accepts one.
    void remove_links_into(const BlueprintPinRef &pin, BlueprintLinkKind kind);

    /// Remove every wire whose *output* side is this pin.
    void remove_links_out_of(const BlueprintPinRef &pin, BlueprintLinkKind kind);

    /// Wire feeding a data input pin, or nullptr.
    const BlueprintLink *incoming_data_link(const BlueprintPinRef &pin) const;

    /// Wire leaving an exec output pin, or nullptr.
    const BlueprintLink *outgoing_exec_link(const BlueprintPinRef &pin) const;

    bool pin_has_link(const BlueprintPinRef &pin, BlueprintLinkKind kind, bool asInput) const;
  };

  /// A user-defined function: a private graph with typed parameters, callable
  /// from the event graph or from other functions.
  struct BlueprintFunction
  {
    std::string name;
    std::vector<BlueprintVariable> inputs;
    std::vector<BlueprintVariable> outputs;
    BlueprintGraph graph;
    /// Set true when the function is allowed to call itself. Recursion is
    /// still bounded by the VM call-depth limit.
    bool allowRecursion = false;
  };

  /// The whole asset: variables, functions, and the event graph.
  struct Blueprint
  {
    static constexpr int kFormatVersion = 1;

    std::string name;
    std::string description;
    std::vector<BlueprintVariable> variables;
    std::vector<BlueprintFunction> functions;
    BlueprintGraph eventGraph;
    /// Monotonic id source. Node ids are unique across the whole asset (event
    /// graph and all function graphs) so the editor can address any node with
    /// a single number.
    BlueprintNodeId nextNodeId = 1;

    BlueprintNodeId allocate_node_id();

    const BlueprintVariable *find_variable(const std::string &name) const;
    BlueprintVariable *find_variable(const std::string &name);
    int variable_index(const std::string &name) const;

    const BlueprintFunction *find_function(const std::string &name) const;
    BlueprintFunction *find_function(const std::string &name);

    /// Every graph in the asset, event graph first.
    std::vector<BlueprintGraph *> all_graphs();
    std::vector<const BlueprintGraph *> all_graphs() const;

    nlohmann::json to_json() const;

    /// Parse an asset. On failure returns false and fills `errorMessage`.
    /// Unknown node types are preserved verbatim so a graph authored against a
    /// newer engine build is not silently destroyed by a round trip; the
    /// compiler reports them as errors instead.
    static bool from_json(
        const nlohmann::json &document,
        Blueprint &out,
        std::string *errorMessage = nullptr);

    /// Repair invariants after loading or after an editor edit: assign missing
    /// node ids, drop dangling links, and bump `nextNodeId` past every node.
    /// Returns the number of repairs applied.
    int normalize();
  };
}

#endif
