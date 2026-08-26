// Blueprint node library for reaching C++ scripts.
//
// Mirrors the structure of `blueprint/blueprint_nodes.cpp`: every node is a
// plain function pointer taking a BlueprintExecContext. The bodies are thin —
// all three forward to `BlueprintHost::send_script_message`, which lands on
// `HadesScript::onMessage` when the embedder bound a ScriptRuntime and returns
// an empty value when it did not.
//
// The Value and Result pins are typed from the node's config rather than being
// wildcards: a wildcard has to be resolved from something the compiler can
// see, and a message crossing into C++ gives it nothing to infer from. The
// editor shows a type combo for them in the details panel.

#include "script_blueprint_nodes.hpp"

#include <string>
#include <utility>
#include <vector>

#include "../blueprint/blueprint_host.hpp"
#include "../blueprint/blueprint_node_registry.hpp"
#include "../blueprint/script_blueprint_bridge.hpp"

namespace hades
{
  namespace
  {
    // -----------------------------------------------------------------------
    // Pin construction helpers
    // -----------------------------------------------------------------------

    BlueprintPinSpec pin(const char *name, ValueType type)
    {
      BlueprintPinSpec spec;
      spec.name = name;
      spec.type = type;
      spec.defaultValue = BlueprintValue::default_for(type);
      return spec;
    }

    BlueprintPinSpec labelled(BlueprintPinSpec spec, const char *displayName)
    {
      spec.displayName = displayName;
      return spec;
    }

    std::vector<std::string> execs(std::initializer_list<const char *> names)
    {
      return std::vector<std::string>(names.begin(), names.end());
    }

    BlueprintPinSpec entity_target()
    {
      BlueprintPinSpec spec = pin("target", ValueType::Entity);
      spec.displayName = "Target";
      spec.tooltip = "Leave unconnected to message the entity that owns this Blueprint.";
      return spec;
    }

    BlueprintPinSpec message_name()
    {
      BlueprintPinSpec spec = pin("name", ValueType::String);
      spec.displayName = "Name";
      spec.tooltip = "Arrives as the `name` argument of HadesScript::onMessage.";
      return spec;
    }

    // -----------------------------------------------------------------------
    // Signatures
    // -----------------------------------------------------------------------

    /// A pin type chosen in the details panel and stored on the node. Falls
    /// back to `fallback` for anything missing or nonsensical — `Exec` and
    /// `Wildcard` must never reach a compiled graph.
    ValueType configured_type(const BlueprintNode &node, const char *key, ValueType fallback)
    {
      const std::string name = node.config.value(key, std::string());
      if (name.empty())
      {
        return fallback;
      }

      ValueType type = fallback;
      if (!value_type_from_name(name, type) ||
          type == ValueType::Exec ||
          type == ValueType::Wildcard)
      {
        return fallback;
      }

      return type;
    }

    BlueprintPinSpec value_pin(ValueType type)
    {
      return labelled(pin("value", type), "Value");
    }

    void signature_script_send(
        const BlueprintSignatureContext &context,
        const BlueprintNode &node,
        BlueprintNodeSignature &out)
    {
      (void)context;
      out.execInputs = execs({"exec"});
      out.execOutputs = execs({"then"});
      out.dataInputs.push_back(entity_target());
      out.dataInputs.push_back(message_name());
      out.dataInputs.push_back(value_pin(configured_type(node, "valueType", ValueType::Float)));
      out.title = "Send Script Message";
    }

    void signature_script_broadcast(
        const BlueprintSignatureContext &context,
        const BlueprintNode &node,
        BlueprintNodeSignature &out)
    {
      (void)context;
      out.execInputs = execs({"exec"});
      out.execOutputs = execs({"then"});
      out.dataInputs.push_back(message_name());
      out.dataInputs.push_back(value_pin(configured_type(node, "valueType", ValueType::Float)));
      out.title = "Broadcast Script Message";
    }

    void signature_script_call(
        const BlueprintSignatureContext &context,
        const BlueprintNode &node,
        BlueprintNodeSignature &out)
    {
      (void)context;
      out.execInputs = execs({"exec"});
      out.execOutputs = execs({"then"});
      out.dataInputs.push_back(entity_target());
      out.dataInputs.push_back(message_name());
      out.dataInputs.push_back(value_pin(configured_type(node, "valueType", ValueType::Float)));
      out.dataOutputs.push_back(
          labelled(pin("result", configured_type(node, "resultType", ValueType::Float)), "Result"));
      out.dataOutputs.push_back(labelled(pin("handled", ValueType::Bool), "Handled"));
      out.title = "Call Script Function";
    }

    // -----------------------------------------------------------------------
    // Node implementations
    // -----------------------------------------------------------------------

    /// Entity pins default to `Entity::INVALID`, which every node reads as
    /// "the entity this graph is attached to".
    Entity::EntityId resolve_target(BlueprintExecContext &context, int inputIndex)
    {
      const Entity::EntityId target = context.input(inputIndex).as_entity();
      return target == Entity::INVALID ? context.entity() : target;
    }

    BlueprintExecResult node_script_send(BlueprintExecContext &context)
    {
      context.host().send_script_message(
          resolve_target(context, 0),
          context.input(1).as_string(),
          to_script_value(context.input(2)));
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_script_broadcast(BlueprintExecContext &context)
    {
      context.host().send_script_message(
          Entity::INVALID,
          context.input(0).as_string(),
          to_script_value(context.input(1)));
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_script_call(BlueprintExecContext &context)
    {
      const ScriptValue reply = context.host().send_script_message(
          resolve_target(context, 0),
          context.input(1).as_string(),
          to_script_value(context.input(2)));

      // An empty reply means no script handled it. `Result` still writes, so
      // the pin carries its type's zero rather than a stale value from the
      // previous execution.
      context.set_output(0, to_blueprint_value(reply));
      context.set_output(1, BlueprintValue::from_bool(!reply.empty()));
      return BlueprintExecResult::next(0);
    }

    // -----------------------------------------------------------------------
    // Registration
    // -----------------------------------------------------------------------

    void define(BlueprintNodeType type)
    {
      BlueprintNodeRegistry::instance().register_type(std::move(type));
    }

    BlueprintNodeType make_type(
        const char *name,
        const char *displayName,
        const char *tooltip,
        const char *keywords,
        BlueprintNodeFn fn,
        BlueprintSignatureFn signatureFn)
    {
      BlueprintNodeType type;
      type.name = name;
      type.displayName = displayName;
      type.category = "Scripts";
      type.tooltip = tooltip;
      type.keywords = keywords;
      type.kind = BlueprintNodeKind::Exec;
      type.fn = fn;
      type.signatureFn = signatureFn;
      return type;
    }
  }

  void register_script_blueprint_nodes()
  {
    static bool registered = false;
    if (registered)
    {
      return;
    }
    registered = true;

    define(make_type(
        "script.send",
        "Send Script Message",
        "Calls onMessage on every C++ script attached to the target entity. Fire and forget.",
        "script cpp message call send notify onmessage",
        node_script_send,
        signature_script_send));

    define(make_type(
        "script.call",
        "Call Script Function",
        "Calls onMessage on the target entity's scripts and reads the reply back. "
        "Handled is false when no script answered.",
        "script cpp function call return result query onmessage",
        node_script_call,
        signature_script_call));

    define(make_type(
        "script.broadcast",
        "Broadcast Script Message",
        "Calls onMessage on every scripted entity in the world.",
        "script cpp message broadcast global all notify onmessage",
        node_script_broadcast,
        signature_script_broadcast));
  }
}
