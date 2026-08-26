// Built-in Blueprint node library.
//
// Every node is a plain function pointer taking a BlueprintExecContext. Pure
// nodes just write their outputs and return `stop()`; exec nodes return which
// exec pin the VM should follow next. Nodes that need to run a chain and then
// regain control (Sequence, ForLoop, WhileLoop) return `loop()`, which pushes
// them onto the VM's continuation stack.

#include "blueprint_node_registry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "../animation/animation_blueprint_nodes.hpp"
#include "../components/audio_source_component.hpp"
#include "../components/name_component.hpp"
#include "../components/position_component_3d.hpp"
#include "../components/rotation_component_3d.hpp"
#include "../components/scale_component_3d.hpp"
#include "../components/transform_hierarchy_component.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"
#include "../runtime/script_blueprint_nodes.hpp"
#include "../ui/ui_blueprint_nodes.hpp"
#include "blueprint_host.hpp"

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

    BlueprintPinSpec pin(const char *name, ValueType type, BlueprintValue defaultValue)
    {
      BlueprintPinSpec spec = pin(name, type);
      spec.defaultValue = std::move(defaultValue);
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

    struct NodeDefinition
    {
      const char *name = nullptr;
      const char *displayName = nullptr;
      const char *category = nullptr;
      const char *tooltip = "";
      BlueprintNodeKind kind = BlueprintNodeKind::Pure;
      std::vector<std::string> execInputs;
      std::vector<std::string> execOutputs;
      std::vector<BlueprintPinSpec> dataInputs;
      std::vector<BlueprintPinSpec> dataOutputs;
      BlueprintNodeFn fn = nullptr;
      const char *keywords = "";
      const char *eventName = "";
      bool latent = false;
      bool hidden = false;
      BlueprintSignatureFn signatureFn = nullptr;
    };

    void define(NodeDefinition definition)
    {
      BlueprintNodeType type;
      type.name = definition.name;
      type.displayName = definition.displayName;
      type.category = definition.category;
      type.tooltip = definition.tooltip;
      type.keywords = definition.keywords;
      type.kind = definition.kind;
      type.latent = definition.latent;
      type.hidden = definition.hidden;
      type.eventName = definition.eventName;
      type.signatureFn = definition.signatureFn;
      type.fn = definition.fn;
      type.signature.execInputs = std::move(definition.execInputs);
      type.signature.execOutputs = std::move(definition.execOutputs);
      type.signature.dataInputs = std::move(definition.dataInputs);
      type.signature.dataOutputs = std::move(definition.dataOutputs);

      BlueprintNodeRegistry::instance().register_type(std::move(type));
    }

    // -----------------------------------------------------------------------
    // Shared runtime helpers
    // -----------------------------------------------------------------------

    /// Entity pins default to `Entity::INVALID`, which every node reads as
    /// "the entity this graph is attached to" — the equivalent of Unreal's
    /// implicit `self` target.
    Entity::EntityId resolve_target(BlueprintExecContext &context, int inputIndex)
    {
      const Entity::EntityId target = context.input(inputIndex).as_entity();
      return target == Entity::INVALID ? context.entity() : target;
    }

    math::Vec3 quat_to_euler_degrees(float qx, float qy, float qz, float qw)
    {
      const float sinRoll = 2.0f * (qw * qx + qy * qz);
      const float cosRoll = 1.0f - 2.0f * (qx * qx + qy * qy);
      const float roll = std::atan2(sinRoll, cosRoll);

      float sinPitch = 2.0f * (qw * qy - qz * qx);
      sinPitch = std::clamp(sinPitch, -1.0f, 1.0f);
      const float pitch = std::asin(sinPitch);

      const float sinYaw = 2.0f * (qw * qz + qx * qy);
      const float cosYaw = 1.0f - 2.0f * (qy * qy + qz * qz);
      const float yaw = std::atan2(sinYaw, cosYaw);

      constexpr float kRadToDeg = 57.2957795130823f;
      return math::Vec3(roll * kRadToDeg, pitch * kRadToDeg, yaw * kRadToDeg);
    }

    void euler_degrees_to_quat(const math::Vec3 &degrees, RotationComponent3D &out)
    {
      constexpr float kDegToRad = 0.01745329251994329f;
      const float halfRoll = degrees.x * kDegToRad * 0.5f;
      const float halfPitch = degrees.y * kDegToRad * 0.5f;
      const float halfYaw = degrees.z * kDegToRad * 0.5f;

      const float sr = std::sin(halfRoll);
      const float cr = std::cos(halfRoll);
      const float sp = std::sin(halfPitch);
      const float cp = std::cos(halfPitch);
      const float sy = std::sin(halfYaw);
      const float cy = std::cos(halfYaw);

      out.qw = cr * cp * cy + sr * sp * sy;
      out.qx = sr * cp * cy - cr * sp * sy;
      out.qy = cr * sp * cy + sr * cp * sy;
      out.qz = cr * cp * sy - sr * sp * cy;
    }

    // -----------------------------------------------------------------------
    // Dynamic signatures
    // -----------------------------------------------------------------------

    void signature_variable_get(
        const BlueprintSignatureContext &context,
        const BlueprintNode &node,
        BlueprintNodeSignature &out)
    {
      const std::string name = node.config.value("variable", std::string());
      ValueType type = ValueType::Wildcard;

      if (context.blueprint != nullptr)
      {
        if (const BlueprintVariable *variable = context.blueprint->find_variable(name))
        {
          type = variable->type;
        }
      }

      out.dataOutputs.push_back(labelled(pin("value", type), name.c_str()));
      out.title = name.empty() ? "Get Variable" : ("Get " + name);
    }

    void signature_variable_set(
        const BlueprintSignatureContext &context,
        const BlueprintNode &node,
        BlueprintNodeSignature &out)
    {
      const std::string name = node.config.value("variable", std::string());
      ValueType type = ValueType::Wildcard;

      if (context.blueprint != nullptr)
      {
        if (const BlueprintVariable *variable = context.blueprint->find_variable(name))
        {
          type = variable->type;
        }
      }

      out.execInputs = execs({"exec"});
      out.execOutputs = execs({"then"});
      out.dataInputs.push_back(labelled(pin("value", type), name.c_str()));
      out.dataOutputs.push_back(labelled(pin("value", type), name.c_str()));
      out.title = name.empty() ? "Set Variable" : ("Set " + name);
    }

    /// A custom event's declared payload, as authored in the details panel and
    /// stored on the node: `config["params"] = [{"name": ..., "type": ...}]`.
    ///
    /// Both the event node (which turns them into data outputs) and every Call
    /// Event targeting it (which turns them into data inputs) read this, so the
    /// two sides cannot drift apart.
    std::vector<BlueprintPinSpec> custom_event_params(const BlueprintNode &node)
    {
      std::vector<BlueprintPinSpec> params;

      const auto it = node.config.find("params");
      if (it == node.config.end() || !it->is_array())
      {
        return params;
      }

      for (const auto &entry : *it)
      {
        if (!entry.is_object())
        {
          continue;
        }

        const std::string paramName = entry.value("name", std::string());
        if (paramName.empty())
        {
          continue;
        }

        ValueType type = ValueType::Float;
        const std::string typeName = entry.value("type", std::string("float"));
        // Exec and Wildcard must never reach a compiled graph; anything
        // unrecognised falls back to Float rather than dropping the pin, so a
        // hand-edited asset still wires up.
        if (!value_type_from_name(typeName, type) ||
            type == ValueType::Exec ||
            type == ValueType::Wildcard)
        {
          type = ValueType::Float;
        }

        params.push_back(pin(paramName.c_str(), type));
      }

      return params;
    }

    /// The `event.custom` node named `name` in the asset's event graph, or
    /// nullptr. Custom events only ever live in the event graph — the compiler
    /// rejects Call Event inside a function graph.
    const BlueprintNode *find_custom_event_node(const Blueprint *blueprint, const std::string &name)
    {
      if (blueprint == nullptr || name.empty())
      {
        return nullptr;
      }

      for (const auto &node : blueprint->eventGraph.nodes)
      {
        if (node.type == "event.custom" && node.config.value("name", std::string()) == name)
        {
          return &node;
        }
      }

      return nullptr;
    }

    void signature_custom_event(
        const BlueprintSignatureContext &context,
        const BlueprintNode &node,
        BlueprintNodeSignature &out)
    {
      (void)context;
      const std::string name = node.config.value("name", std::string());
      out.execOutputs = execs({"exec"});
      out.dataOutputs = custom_event_params(node);
      out.title = name.empty() ? "Custom Event" : ("Event " + name);
    }

    void signature_call_event(
        const BlueprintSignatureContext &context,
        const BlueprintNode &node,
        BlueprintNodeSignature &out)
    {
      const std::string name = node.config.value("name", std::string());
      out.execInputs = execs({"exec"});
      out.execOutputs = execs({"then"});

      // Arguments mirror the target event's parameters. When the target is
      // missing the node shows no argument pins and the compiler reports the
      // dangling name, which reads better than inventing pins for an event
      // that does not exist.
      if (const BlueprintNode *target = find_custom_event_node(context.blueprint, name))
      {
        out.dataInputs = custom_event_params(*target);
      }

      out.title = name.empty() ? "Call Event" : ("Call " + name);
    }

    void signature_sequence(
        const BlueprintSignatureContext &context,
        const BlueprintNode &node,
        BlueprintNodeSignature &out)
    {
      (void)context;
      int count = node.config.value("outputs", 2);
      count = std::clamp(count, 1, 16);

      out.execInputs = execs({"exec"});
      for (int i = 0; i < count; ++i)
      {
        out.execOutputs.push_back("then" + std::to_string(i));
      }
      out.title = "Sequence";
    }

    void signature_function_entry(
        const BlueprintSignatureContext &context,
        const BlueprintNode &node,
        BlueprintNodeSignature &out)
    {
      (void)node;
      out.execOutputs = execs({"exec"});
      out.title = "Entry";

      if (context.function != nullptr)
      {
        out.title = context.function->name.empty() ? "Entry" : context.function->name;
        for (const auto &parameter : context.function->inputs)
        {
          out.dataOutputs.push_back(pin(parameter.name.c_str(), parameter.type, parameter.defaultValue));
        }
      }
    }

    void signature_function_result(
        const BlueprintSignatureContext &context,
        const BlueprintNode &node,
        BlueprintNodeSignature &out)
    {
      (void)node;
      out.execInputs = execs({"exec"});
      out.title = "Return";

      if (context.function != nullptr)
      {
        for (const auto &parameter : context.function->outputs)
        {
          out.dataInputs.push_back(pin(parameter.name.c_str(), parameter.type, parameter.defaultValue));
        }
      }
    }

    void signature_function_call(
        const BlueprintSignatureContext &context,
        const BlueprintNode &node,
        BlueprintNodeSignature &out)
    {
      const std::string name = node.config.value("function", std::string());
      out.execInputs = execs({"exec"});
      out.execOutputs = execs({"then"});
      out.title = name.empty() ? "Call Function" : name;

      if (context.blueprint == nullptr)
      {
        return;
      }

      const BlueprintFunction *function = context.blueprint->find_function(name);
      if (function == nullptr)
      {
        return;
      }

      for (const auto &parameter : function->inputs)
      {
        out.dataInputs.push_back(pin(parameter.name.c_str(), parameter.type, parameter.defaultValue));
      }
      for (const auto &parameter : function->outputs)
      {
        out.dataOutputs.push_back(pin(parameter.name.c_str(), parameter.type, parameter.defaultValue));
      }
    }

    // -----------------------------------------------------------------------
    // Node implementations — events
    // -----------------------------------------------------------------------

    /// Every event node is a no-op: the runtime writes the payload into the
    /// node's output registers before handing control to the VM.
    BlueprintExecResult node_event_entry(BlueprintExecContext &context)
    {
      (void)context;
      return BlueprintExecResult::next(0);
    }

    // -----------------------------------------------------------------------
    // Node implementations — flow control
    // -----------------------------------------------------------------------

    BlueprintExecResult node_branch(BlueprintExecContext &context)
    {
      return BlueprintExecResult::next(context.input(0).as_bool() ? 0 : 1);
    }

    BlueprintExecResult node_sequence(BlueprintExecContext &context)
    {
      auto &state = context.state();
      const int count = context.aux0();

      if (!context.is_reentry())
      {
        state.i0 = 0;
      }
      else
      {
        ++state.i0;
      }

      if (state.i0 >= count)
      {
        return BlueprintExecResult::stop();
      }

      // Only keep the continuation alive while more pins remain.
      if (state.i0 + 1 < count)
      {
        return BlueprintExecResult::loop(state.i0);
      }

      return BlueprintExecResult::next(state.i0);
    }

    BlueprintExecResult node_for_loop(BlueprintExecContext &context)
    {
      auto &state = context.state();

      if (!context.is_reentry())
      {
        state.i0 = context.input(0).as_int();
        state.i1 = context.input(1).as_int();
      }
      else
      {
        ++state.i0;
      }

      if (state.i0 > state.i1)
      {
        return BlueprintExecResult::next(1);
      }

      context.set_output(0, BlueprintValue::from_int(state.i0));
      return BlueprintExecResult::loop(0);
    }

    BlueprintExecResult node_while_loop(BlueprintExecContext &context)
    {
      // The condition is a data input, and the VM re-evaluates a node's pure
      // dependencies on every entry including re-entries, so this reads a
      // freshly computed value each iteration.
      if (!context.input(0).as_bool())
      {
        return BlueprintExecResult::next(1);
      }

      return BlueprintExecResult::loop(0);
    }

    BlueprintExecResult node_do_once(BlueprintExecContext &context)
    {
      auto &state = context.state();

      // Exec input 1 is "reset".
      if (context.exec_input() == 1)
      {
        state.b0 = false;
        state.b1 = true;
        return BlueprintExecResult::stop();
      }

      if (!state.b1)
      {
        state.b0 = context.input(0).as_bool();
        state.b1 = true;
      }

      if (state.b0)
      {
        return BlueprintExecResult::stop();
      }

      state.b0 = true;
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_gate(BlueprintExecContext &context)
    {
      auto &state = context.state();

      if (!state.b1)
      {
        state.b0 = context.input(0).as_bool();
        state.b1 = true;
      }

      switch (context.exec_input())
      {
      case 1: // open
        state.b0 = true;
        return BlueprintExecResult::stop();
      case 2: // close
        state.b0 = false;
        return BlueprintExecResult::stop();
      case 3: // toggle
        state.b0 = !state.b0;
        return BlueprintExecResult::stop();
      default: // enter
        return state.b0 ? BlueprintExecResult::next(0) : BlueprintExecResult::stop();
      }
    }

    BlueprintExecResult node_flip_flop(BlueprintExecContext &context)
    {
      auto &state = context.state();
      state.b0 = !state.b0;
      context.set_output(0, BlueprintValue::from_bool(state.b0));
      return BlueprintExecResult::next(state.b0 ? 0 : 1);
    }

    BlueprintExecResult node_delay(BlueprintExecContext &context)
    {
      const float duration = context.input(0).as_float();
      if (duration <= 0.0f)
      {
        return BlueprintExecResult::next(0);
      }

      return BlueprintExecResult::wait(duration, 0);
    }

    BlueprintExecResult node_call_event(BlueprintExecContext &context)
    {
      const int target = context.aux0();
      if (target < 0)
      {
        return BlueprintExecResult::next(0);
      }

      // Arguments land in the event node's output registers before its chain
      // runs, exactly as they would for an event raised from outside the graph.
      context.write_event_payload(target, 0, context.input_count());

      if (!context.call_chain(target))
      {
        return BlueprintExecResult::stop();
      }

      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_stop(BlueprintExecContext &context)
    {
      (void)context;
      return BlueprintExecResult::stop();
    }

    // -----------------------------------------------------------------------
    // Node implementations — variables and functions
    // -----------------------------------------------------------------------

    BlueprintExecResult node_variable_get(BlueprintExecContext &context)
    {
      context.set_output(0, context.variable(context.aux0()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_variable_set(BlueprintExecContext &context)
    {
      BlueprintValue value = context.input(0);
      context.variable(context.aux0()) = value;
      context.set_output(0, std::move(value));
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_function_call(BlueprintExecContext &context)
    {
      if (!context.call_function(context.aux0(), 0, context.aux1()))
      {
        return BlueprintExecResult::stop();
      }

      return BlueprintExecResult::next(0);
    }

    /// The Return node ends the function body. The VM harvests its data inputs
    /// into the caller's outputs before unwinding.
    BlueprintExecResult node_function_result(BlueprintExecContext &context)
    {
      (void)context;
      return BlueprintExecResult::stop();
    }

    // -----------------------------------------------------------------------
    // Node implementations — math
    // -----------------------------------------------------------------------

    BlueprintExecResult node_add(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_float(context.input(0).as_float() + context.input(1).as_float()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_subtract(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_float(context.input(0).as_float() - context.input(1).as_float()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_multiply(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_float(context.input(0).as_float() * context.input(1).as_float()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_divide(BlueprintExecContext &context)
    {
      const float divisor = context.input(1).as_float();
      // Division by zero yields 0 rather than an inf/NaN that would then
      // silently poison every downstream transform.
      const float result = divisor == 0.0f ? 0.0f : context.input(0).as_float() / divisor;
      context.set_output(0, BlueprintValue::from_float(result));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_add_int(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_int(context.input(0).as_int() + context.input(1).as_int()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_subtract_int(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_int(context.input(0).as_int() - context.input(1).as_int()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_multiply_int(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_int(context.input(0).as_int() * context.input(1).as_int()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_divide_int(BlueprintExecContext &context)
    {
      const std::int32_t divisor = context.input(1).as_int();
      const std::int32_t dividend = context.input(0).as_int();
      // INT32_MIN / -1 overflows and traps on some targets, so it is treated
      // like the divide-by-zero case.
      const bool undefined =
          divisor == 0 || (divisor == -1 && dividend == (std::numeric_limits<std::int32_t>::min)());
      context.set_output(0, BlueprintValue::from_int(undefined ? 0 : dividend / divisor));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_modulo_int(BlueprintExecContext &context)
    {
      const std::int32_t divisor = context.input(1).as_int();
      const std::int32_t dividend = context.input(0).as_int();
      const bool undefined =
          divisor == 0 || (divisor == -1 && dividend == (std::numeric_limits<std::int32_t>::min)());
      context.set_output(0, BlueprintValue::from_int(undefined ? 0 : dividend % divisor));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_negate(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_float(-context.input(0).as_float()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_abs(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_float(std::fabs(context.input(0).as_float())));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_min(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_float(std::min(context.input(0).as_float(), context.input(1).as_float())));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_max(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_float(std::max(context.input(0).as_float(), context.input(1).as_float())));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_clamp(BlueprintExecContext &context)
    {
      const float low = context.input(1).as_float();
      const float high = context.input(2).as_float();
      const float value = context.input(0).as_float();
      context.set_output(
          0,
          BlueprintValue::from_float(low <= high ? std::clamp(value, low, high) : value));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_lerp(BlueprintExecContext &context)
    {
      const float a = context.input(0).as_float();
      const float b = context.input(1).as_float();
      const float alpha = context.input(2).as_float();
      context.set_output(0, BlueprintValue::from_float(a + (b - a) * alpha));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_sin(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_float(std::sin(context.input(0).as_float())));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_cos(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_float(std::cos(context.input(0).as_float())));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_sqrt(BlueprintExecContext &context)
    {
      const float value = context.input(0).as_float();
      context.set_output(0, BlueprintValue::from_float(value <= 0.0f ? 0.0f : std::sqrt(value)));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_pow(BlueprintExecContext &context)
    {
      context.set_output(
          0,
          BlueprintValue::from_float(std::pow(context.input(0).as_float(), context.input(1).as_float())));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_random_float(BlueprintExecContext &context)
    {
      const float low = context.input(0).as_float();
      const float high = context.input(1).as_float();
      context.set_output(0, BlueprintValue::from_float(low + (high - low) * context.random_float()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_random_int(BlueprintExecContext &context)
    {
      context.set_output(
          0,
          BlueprintValue::from_int(context.random_int(context.input(0).as_int(), context.input(1).as_int())));
      return BlueprintExecResult::stop();
    }

    // -----------------------------------------------------------------------
    // Node implementations — vectors
    // -----------------------------------------------------------------------

    BlueprintExecResult node_vector_make(BlueprintExecContext &context)
    {
      context.set_output(
          0,
          BlueprintValue::from_vector(math::Vec3(
              context.input(0).as_float(),
              context.input(1).as_float(),
              context.input(2).as_float())));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_vector_break(BlueprintExecContext &context)
    {
      const math::Vec3 value = context.input(0).as_vector();
      context.set_output(0, BlueprintValue::from_float(value.x));
      context.set_output(1, BlueprintValue::from_float(value.y));
      context.set_output(2, BlueprintValue::from_float(value.z));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_vector_add(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_vector(context.input(0).as_vector() + context.input(1).as_vector()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_vector_subtract(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_vector(context.input(0).as_vector() - context.input(1).as_vector()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_vector_scale(BlueprintExecContext &context)
    {
      context.set_output(
          0,
          BlueprintValue::from_vector(context.input(0).as_vector() * context.input(1).as_float()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_vector_dot(BlueprintExecContext &context)
    {
      context.set_output(
          0,
          BlueprintValue::from_float(context.input(0).as_vector().dot(context.input(1).as_vector())));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_vector_cross(BlueprintExecContext &context)
    {
      context.set_output(
          0,
          BlueprintValue::from_vector(context.input(0).as_vector().cross(context.input(1).as_vector())));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_vector_length(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_float(context.input(0).as_vector().length()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_vector_normalize(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_vector(context.input(0).as_vector().normalized()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_vector_distance(BlueprintExecContext &context)
    {
      context.set_output(
          0,
          BlueprintValue::from_float((context.input(0).as_vector() - context.input(1).as_vector()).length()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_vector_lerp(BlueprintExecContext &context)
    {
      context.set_output(
          0,
          BlueprintValue::from_vector(math::lerp(
              context.input(0).as_vector(),
              context.input(1).as_vector(),
              context.input(2).as_float())));
      return BlueprintExecResult::stop();
    }

    // -----------------------------------------------------------------------
    // Node implementations — logic and comparison
    // -----------------------------------------------------------------------

    BlueprintExecResult node_and(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_bool(context.input(0).as_bool() && context.input(1).as_bool()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_or(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_bool(context.input(0).as_bool() || context.input(1).as_bool()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_xor(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_bool(context.input(0).as_bool() != context.input(1).as_bool()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_not(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_bool(!context.input(0).as_bool()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_greater(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_bool(context.input(0).as_float() > context.input(1).as_float()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_greater_equal(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_bool(context.input(0).as_float() >= context.input(1).as_float()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_less(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_bool(context.input(0).as_float() < context.input(1).as_float()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_less_equal(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_bool(context.input(0).as_float() <= context.input(1).as_float()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_equal_float(BlueprintExecContext &context)
    {
      const float tolerance = std::fabs(context.input(2).as_float());
      const float difference = std::fabs(context.input(0).as_float() - context.input(1).as_float());
      context.set_output(0, BlueprintValue::from_bool(difference <= tolerance));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_equal_int(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_bool(context.input(0).as_int() == context.input(1).as_int()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_equal_bool(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_bool(context.input(0).as_bool() == context.input(1).as_bool()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_equal_string(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_bool(context.input(0).as_string() == context.input(1).as_string()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_select(BlueprintExecContext &context)
    {
      context.set_output(0, context.input(0).as_bool() ? context.input(1) : context.input(2));
      return BlueprintExecResult::stop();
    }

    // -----------------------------------------------------------------------
    // Node implementations — conversion and strings
    // -----------------------------------------------------------------------

    BlueprintExecResult node_to_string(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_string(context.input(0).as_string()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_to_int(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_int(context.input(0).as_int()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_to_float(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_float(context.input(0).as_float()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_to_bool(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_bool(context.input(0).as_bool()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_string_concat(BlueprintExecContext &context)
    {
      context.set_output(
          0,
          BlueprintValue::from_string(context.input(0).as_string() + context.input(1).as_string()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_string_length(BlueprintExecContext &context)
    {
      context.set_output(
          0,
          BlueprintValue::from_int(static_cast<std::int32_t>(context.input(0).as_string().size())));
      return BlueprintExecResult::stop();
    }

    // -----------------------------------------------------------------------
    // Node implementations — entity and transform
    // -----------------------------------------------------------------------

    BlueprintExecResult node_self(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_entity(context.entity()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_is_valid(BlueprintExecContext &context)
    {
      // Unlike the other entity nodes this one must NOT fall back to self:
      // the whole point is to report that a handle is None, and resolving None
      // to the owning entity would make the node answer true unconditionally.
      const Entity::EntityId target = context.input(0).as_entity();
      const auto &active = context.entities().getActiveEntities();
      const bool valid =
          target != Entity::INVALID &&
          std::find(active.begin(), active.end(), target) != active.end();
      context.set_output(0, BlueprintValue::from_bool(valid));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_get_position(BlueprintExecContext &context)
    {
      const Entity::EntityId target = resolve_target(context, 0);
      math::Vec3 position;
      if (context.components().hasComponent<PositionComponent3D>(target))
      {
        const auto &component = context.components().getComponent<PositionComponent3D>(target);
        position = math::Vec3(component.x, component.y, component.z);
      }
      context.set_output(0, BlueprintValue::from_vector(position));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_set_position(BlueprintExecContext &context)
    {
      const Entity::EntityId target = resolve_target(context, 0);
      if (context.components().hasComponent<PositionComponent3D>(target))
      {
        const math::Vec3 value = context.input(1).as_vector();
        auto &component = context.components().getComponent<PositionComponent3D>(target);
        component.x = value.x;
        component.y = value.y;
        component.z = value.z;
      }
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_add_offset(BlueprintExecContext &context)
    {
      const Entity::EntityId target = resolve_target(context, 0);
      if (context.components().hasComponent<PositionComponent3D>(target))
      {
        const math::Vec3 delta = context.input(1).as_vector();
        auto &component = context.components().getComponent<PositionComponent3D>(target);
        component.x += delta.x;
        component.y += delta.y;
        component.z += delta.z;
      }
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_get_rotation(BlueprintExecContext &context)
    {
      const Entity::EntityId target = resolve_target(context, 0);
      math::Vec3 euler;
      if (context.components().hasComponent<RotationComponent3D>(target))
      {
        const auto &rotation = context.components().getComponent<RotationComponent3D>(target);
        euler = quat_to_euler_degrees(rotation.qx, rotation.qy, rotation.qz, rotation.qw);
      }
      context.set_output(0, BlueprintValue::from_vector(euler));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_set_rotation(BlueprintExecContext &context)
    {
      const Entity::EntityId target = resolve_target(context, 0);
      if (context.components().hasComponent<RotationComponent3D>(target))
      {
        auto &rotation = context.components().getComponent<RotationComponent3D>(target);
        euler_degrees_to_quat(context.input(1).as_vector(), rotation);
      }
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_get_scale(BlueprintExecContext &context)
    {
      const Entity::EntityId target = resolve_target(context, 0);
      math::Vec3 scale(1.0f, 1.0f, 1.0f);
      if (context.components().hasComponent<ScaleComponent3D>(target))
      {
        const auto &component = context.components().getComponent<ScaleComponent3D>(target);
        scale = math::Vec3(component.x, component.y, component.z);
      }
      context.set_output(0, BlueprintValue::from_vector(scale));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_set_scale(BlueprintExecContext &context)
    {
      const Entity::EntityId target = resolve_target(context, 0);
      if (context.components().hasComponent<ScaleComponent3D>(target))
      {
        const math::Vec3 value = context.input(1).as_vector();
        auto &component = context.components().getComponent<ScaleComponent3D>(target);
        component.x = value.x;
        component.y = value.y;
        component.z = value.z;
      }
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_get_name(BlueprintExecContext &context)
    {
      const Entity::EntityId target = resolve_target(context, 0);
      std::string name;
      if (context.components().hasComponent<NameComponent>(target))
      {
        name = context.components().getComponent<NameComponent>(target).value;
      }
      context.set_output(0, BlueprintValue::from_string(std::move(name)));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_set_name(BlueprintExecContext &context)
    {
      const Entity::EntityId target = resolve_target(context, 0);
      if (context.components().hasComponent<NameComponent>(target))
      {
        context.components().getComponent<NameComponent>(target).value = context.input(1).as_string();
      }
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_find_by_name(BlueprintExecContext &context)
    {
      const std::string wanted = context.input(0).as_string();
      Entity::EntityId found = Entity::INVALID;

      for (Entity::EntityId candidate : context.entities().getActiveEntities())
      {
        if (!context.components().hasComponent<NameComponent>(candidate))
        {
          continue;
        }
        if (context.components().getComponent<NameComponent>(candidate).value == wanted)
        {
          found = candidate;
          break;
        }
      }

      context.set_output(0, BlueprintValue::from_entity(found));
      context.set_output(1, BlueprintValue::from_bool(found != Entity::INVALID));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_get_parent(BlueprintExecContext &context)
    {
      const Entity::EntityId target = resolve_target(context, 0);
      Entity::EntityId parent = Entity::INVALID;
      if (context.components().hasComponent<TransformHierarchyComponent>(target))
      {
        const auto &hierarchy = context.components().getComponent<TransformHierarchyComponent>(target);
        parent = hierarchy.parent.value_or(Entity::INVALID);
      }
      context.set_output(0, BlueprintValue::from_entity(parent));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_get_child_count(BlueprintExecContext &context)
    {
      const Entity::EntityId target = resolve_target(context, 0);
      std::int32_t count = 0;
      if (context.components().hasComponent<TransformHierarchyComponent>(target))
      {
        const auto &hierarchy = context.components().getComponent<TransformHierarchyComponent>(target);
        count = static_cast<std::int32_t>(hierarchy.children.size());
      }
      context.set_output(0, BlueprintValue::from_int(count));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_get_child(BlueprintExecContext &context)
    {
      const Entity::EntityId target = resolve_target(context, 0);
      const std::int32_t index = context.input(1).as_int();
      Entity::EntityId child = Entity::INVALID;

      if (context.components().hasComponent<TransformHierarchyComponent>(target))
      {
        const auto &hierarchy = context.components().getComponent<TransformHierarchyComponent>(target);
        if (index >= 0 && static_cast<std::size_t>(index) < hierarchy.children.size())
        {
          child = hierarchy.children[static_cast<std::size_t>(index)];
        }
      }

      context.set_output(0, BlueprintValue::from_entity(child));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_destroy_entity(BlueprintExecContext &context)
    {
      const Entity::EntityId target = resolve_target(context, 0);
      if (target != Entity::INVALID)
      {
        context.components().removeAllComponents(target);
        context.entities().destroyEntity(target);
      }
      return BlueprintExecResult::next(0);
    }

    // -----------------------------------------------------------------------
    // Node implementations — physics, audio, debug, time
    // -----------------------------------------------------------------------

    BlueprintExecResult node_add_force(BlueprintExecContext &context)
    {
      context.host().apply_force(resolve_target(context, 0), context.input(1).as_vector());
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_add_impulse(BlueprintExecContext &context)
    {
      context.host().apply_impulse(resolve_target(context, 0), context.input(1).as_vector());
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_set_velocity(BlueprintExecContext &context)
    {
      context.host().set_linear_velocity(resolve_target(context, 0), context.input(1).as_vector());
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_play_audio(BlueprintExecContext &context)
    {
      context.host().play_audio(resolve_target(context, 0));
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_stop_audio(BlueprintExecContext &context)
    {
      context.host().stop_audio(resolve_target(context, 0));
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_set_volume(BlueprintExecContext &context)
    {
      const Entity::EntityId target = resolve_target(context, 0);
      if (context.components().hasComponent<AudioSourceComponent>(target))
      {
        context.components().getComponent<AudioSourceComponent>(target).volume =
            std::max(0.0f, context.input(1).as_float());
      }
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_print(BlueprintExecContext &context)
    {
      const std::string level = context.node().config.value("level", std::string("info"));
      BlueprintLogLevel resolved = BlueprintLogLevel::Info;
      if (level == "warning")
      {
        resolved = BlueprintLogLevel::Warning;
      }
      else if (level == "error")
      {
        resolved = BlueprintLogLevel::Error;
      }

      context.host().print(context.entity(), context.input(0).as_string(), resolved);
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_observe(BlueprintExecContext &context)
    {
      context.host().observe(context.input(0).as_string(), context.input(1));
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_load_world(BlueprintExecContext &context)
    {
      context.host().load_world(context.input(0).as_string());
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_delta_seconds(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_float(context.delta_time()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_time_seconds(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_float(context.time_seconds()));
      return BlueprintExecResult::stop();
    }

    /// Literal nodes exist so one authored constant can feed many pins. The
    /// input pin never accepts a wire; the compiler treats it as literal-only.
    BlueprintExecResult node_literal(BlueprintExecContext &context)
    {
      context.set_output(0, context.input(0));
      return BlueprintExecResult::stop();
    }
  }

  void register_builtin_blueprint_nodes()
  {
    static bool registered = false;
    if (registered)
    {
      return;
    }
    registered = true;

    const auto entityTarget = []()
    {
      BlueprintPinSpec spec = pin("target", ValueType::Entity);
      spec.displayName = "Target";
      spec.tooltip = "Leave unconnected to act on the entity that owns this Blueprint.";
      return spec;
    };

    // -------------------------------------------------------------------
    // Events
    // -------------------------------------------------------------------
    define({
        "event.begin_play", "Event BeginPlay", "Events",
        "Fires once when play starts, after every entity in the world exists.",
        BlueprintNodeKind::Event, {}, execs({"exec"}), {}, {},
        node_event_entry, "start begin awake init", "begin_play"});

    define({
        "event.tick", "Event Tick", "Events",
        "Fires once per frame while play mode is running.",
        BlueprintNodeKind::Event, {}, execs({"exec"}), {},
        {pin("deltaSeconds", ValueType::Float)},
        node_event_entry, "update frame every", "tick"});

    define({
        "event.key_down", "Event Key Down", "Events",
        "Fires when a keyboard key is pressed.",
        BlueprintNodeKind::Event, {}, execs({"exec"}), {},
        {pin("keyCode", ValueType::Int)},
        node_event_entry, "input keyboard press", "key_down"});

    define({
        "event.key_up", "Event Key Up", "Events",
        "Fires when a keyboard key is released.",
        BlueprintNodeKind::Event, {}, execs({"exec"}), {},
        {pin("keyCode", ValueType::Int)},
        node_event_entry, "input keyboard release", "key_up"});

    define({
        "event.mouse_down", "Event Mouse Down", "Events",
        "Fires when a mouse button is pressed, with the screen-space cursor position.",
        BlueprintNodeKind::Event, {}, execs({"exec"}), {},
        {pin("button", ValueType::Int), pin("screenX", ValueType::Float), pin("screenY", ValueType::Float)},
        node_event_entry, "input click press", "mouse_down"});

    define({
        "event.mouse_up", "Event Mouse Up", "Events",
        "Fires when a mouse button is released.",
        BlueprintNodeKind::Event, {}, execs({"exec"}), {},
        {pin("button", ValueType::Int), pin("screenX", ValueType::Float), pin("screenY", ValueType::Float)},
        node_event_entry, "input click release", "mouse_up"});

    define({
        "event.mouse_move", "Event Mouse Move", "Events",
        "Fires when the cursor moves.",
        BlueprintNodeKind::Event, {}, execs({"exec"}), {},
        {pin("screenX", ValueType::Float), pin("screenY", ValueType::Float)},
        node_event_entry, "input cursor motion", "mouse_move"});

    define({
        "event.collision_begin", "Event Collision Begin", "Events",
        "Fires when this entity starts touching another physics body.",
        BlueprintNodeKind::Event, {}, execs({"exec"}), {},
        {pin("other", ValueType::Entity)},
        node_event_entry, "physics hit overlap contact", "collision_begin"});

    define({
        "event.collision_end", "Event Collision End", "Events",
        "Fires when this entity stops touching another physics body.",
        BlueprintNodeKind::Event, {}, execs({"exec"}), {},
        {pin("other", ValueType::Entity)},
        node_event_entry, "physics separate", "collision_end"});

    {
      NodeDefinition custom;
      custom.name = "event.custom";
      custom.displayName = "Custom Event";
      custom.category = "Events";
      custom.tooltip = "A named entry point you can trigger from Call Event.";
      custom.kind = BlueprintNodeKind::Event;
      custom.fn = node_event_entry;
      custom.keywords = "custom named dispatch";
      custom.signatureFn = signature_custom_event;
      define(std::move(custom));
    }

    // -------------------------------------------------------------------
    // Flow control
    // -------------------------------------------------------------------
    define({
        "flow.branch", "Branch", "Flow Control",
        "Takes the True pin when the condition is true, otherwise the False pin.",
        BlueprintNodeKind::Exec, execs({"exec"}), execs({"true", "false"}),
        {pin("condition", ValueType::Bool)}, {},
        node_branch, "if else condition"});

    {
      NodeDefinition sequence;
      sequence.name = "flow.sequence";
      sequence.displayName = "Sequence";
      sequence.category = "Flow Control";
      sequence.tooltip = "Runs each output pin in order, waiting for one chain to finish before starting the next.";
      sequence.kind = BlueprintNodeKind::Exec;
      sequence.fn = node_sequence;
      sequence.keywords = "then order multiple";
      sequence.signatureFn = signature_sequence;
      define(std::move(sequence));
    }

    define({
        "flow.for_loop", "For Loop", "Flow Control",
        "Runs Loop Body once for every index from First to Last inclusive, then takes Completed.",
        BlueprintNodeKind::Exec, execs({"exec"}), execs({"loopBody", "completed"}),
        {pin("first", ValueType::Int, BlueprintValue::from_int(0)),
         pin("last", ValueType::Int, BlueprintValue::from_int(0))},
        {pin("index", ValueType::Int)},
        node_for_loop, "iterate repeat range"});

    define({
        "flow.while_loop", "While Loop", "Flow Control",
        "Runs Loop Body for as long as Condition stays true. The condition is re-evaluated every iteration.",
        BlueprintNodeKind::Exec, execs({"exec"}), execs({"loopBody", "completed"}),
        {pin("condition", ValueType::Bool)}, {},
        node_while_loop, "iterate repeat until"});

    define({
        "flow.do_once", "Do Once", "Flow Control",
        "Passes execution through the first time only, until Reset is fired.",
        BlueprintNodeKind::Exec, execs({"exec", "reset"}), execs({"completed"}),
        {pin("startClosed", ValueType::Bool, BlueprintValue::from_bool(false))}, {},
        node_do_once, "single first latch"});

    define({
        "flow.gate", "Gate", "Flow Control",
        "Passes Enter through to Exit only while the gate is open.",
        BlueprintNodeKind::Exec, execs({"enter", "open", "close", "toggle"}), execs({"exit"}),
        {pin("startOpen", ValueType::Bool, BlueprintValue::from_bool(true))}, {},
        node_gate, "allow block valve"});

    define({
        "flow.flip_flop", "Flip Flop", "Flow Control",
        "Alternates between the A and B outputs on every execution.",
        BlueprintNodeKind::Exec, execs({"exec"}), execs({"a", "b"}),
        {}, {pin("isA", ValueType::Bool)},
        node_flip_flop, "toggle alternate"});

    {
      NodeDefinition delay;
      delay.name = "flow.delay";
      delay.displayName = "Delay";
      delay.category = "Flow Control";
      delay.tooltip = "Suspends this chain for the given number of seconds, then continues. Not allowed inside functions.";
      delay.kind = BlueprintNodeKind::Exec;
      delay.latent = true;
      delay.execInputs = execs({"exec"});
      delay.execOutputs = execs({"completed"});
      delay.dataInputs = {pin("duration", ValueType::Float, BlueprintValue::from_float(1.0f))};
      delay.fn = node_delay;
      delay.keywords = "wait sleep timer latent";
      define(std::move(delay));
    }

    {
      NodeDefinition callEvent;
      callEvent.name = "flow.call_event";
      callEvent.displayName = "Call Event";
      callEvent.category = "Flow Control";
      callEvent.tooltip = "Runs a Custom Event in this Blueprint to completion, then continues.";
      callEvent.kind = BlueprintNodeKind::Exec;
      callEvent.fn = node_call_event;
      callEvent.keywords = "trigger dispatch custom";
      callEvent.signatureFn = signature_call_event;
      define(std::move(callEvent));
    }

    define({
        "flow.stop", "Stop Execution", "Flow Control",
        "Ends this execution chain.",
        BlueprintNodeKind::Exec, execs({"exec"}), {}, {}, {},
        node_stop, "return end halt"});

    // -------------------------------------------------------------------
    // Variables
    // -------------------------------------------------------------------
    {
      NodeDefinition get;
      get.name = "variable.get";
      get.displayName = "Get Variable";
      get.category = "Variables";
      get.tooltip = "Reads a Blueprint variable.";
      get.kind = BlueprintNodeKind::Pure;
      get.fn = node_variable_get;
      get.keywords = "read value";
      get.signatureFn = signature_variable_get;
      define(std::move(get));
    }

    {
      NodeDefinition set;
      set.name = "variable.set";
      set.displayName = "Set Variable";
      set.category = "Variables";
      set.tooltip = "Writes a Blueprint variable and passes the value straight through.";
      set.kind = BlueprintNodeKind::Exec;
      set.fn = node_variable_set;
      set.keywords = "write assign store";
      set.signatureFn = signature_variable_set;
      define(std::move(set));
    }

    // -------------------------------------------------------------------
    // Functions
    // -------------------------------------------------------------------
    {
      NodeDefinition entry;
      entry.name = "function.entry";
      entry.displayName = "Function Entry";
      entry.category = "Functions";
      entry.tooltip = "Where a user function starts. Placed automatically.";
      entry.kind = BlueprintNodeKind::Event;
      entry.hidden = true;
      entry.fn = node_event_entry;
      entry.signatureFn = signature_function_entry;
      define(std::move(entry));
    }

    {
      NodeDefinition result;
      result.name = "function.result";
      result.displayName = "Return";
      result.category = "Functions";
      result.tooltip = "Ends the function and hands its outputs back to the caller.";
      result.kind = BlueprintNodeKind::Exec;
      result.hidden = true;
      result.fn = node_function_result;
      result.signatureFn = signature_function_result;
      define(std::move(result));
    }

    {
      NodeDefinition call;
      call.name = "function.call";
      call.displayName = "Call Function";
      call.category = "Functions";
      call.tooltip = "Runs a user function defined in this Blueprint.";
      call.kind = BlueprintNodeKind::Exec;
      call.fn = node_function_call;
      call.keywords = "invoke run";
      call.signatureFn = signature_function_call;
      define(std::move(call));
    }

    // -------------------------------------------------------------------
    // Math
    // -------------------------------------------------------------------
    define({"math.add", "Add", "Math", "Float addition.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Float), pin("b", ValueType::Float)},
            {pin("result", ValueType::Float)}, node_add, "plus sum +"});
    define({"math.subtract", "Subtract", "Math", "Float subtraction.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Float), pin("b", ValueType::Float)},
            {pin("result", ValueType::Float)}, node_subtract, "minus difference -"});
    define({"math.multiply", "Multiply", "Math", "Float multiplication.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Float, BlueprintValue::from_float(1.0f)),
             pin("b", ValueType::Float, BlueprintValue::from_float(1.0f))},
            {pin("result", ValueType::Float)}, node_multiply, "times product *"});
    define({"math.divide", "Divide", "Math", "Float division. Dividing by zero yields zero.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Float), pin("b", ValueType::Float, BlueprintValue::from_float(1.0f))},
            {pin("result", ValueType::Float)}, node_divide, "quotient /"});
    define({"math.add_int", "Add (Integer)", "Math", "Integer addition.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Int), pin("b", ValueType::Int)},
            {pin("result", ValueType::Int)}, node_add_int, "plus sum"});
    define({"math.subtract_int", "Subtract (Integer)", "Math", "Integer subtraction.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Int), pin("b", ValueType::Int)},
            {pin("result", ValueType::Int)}, node_subtract_int, "minus"});
    define({"math.multiply_int", "Multiply (Integer)", "Math", "Integer multiplication.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Int, BlueprintValue::from_int(1)), pin("b", ValueType::Int, BlueprintValue::from_int(1))},
            {pin("result", ValueType::Int)}, node_multiply_int, "times"});
    define({"math.divide_int", "Divide (Integer)", "Math", "Integer division. Dividing by zero yields zero.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Int), pin("b", ValueType::Int, BlueprintValue::from_int(1))},
            {pin("result", ValueType::Int)}, node_divide_int, "quotient"});
    define({"math.modulo_int", "Modulo", "Math", "Integer remainder. A zero divisor yields zero.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Int), pin("b", ValueType::Int, BlueprintValue::from_int(1))},
            {pin("result", ValueType::Int)}, node_modulo_int, "remainder %"});
    define({"math.negate", "Negate", "Math", "Flips the sign.", BlueprintNodeKind::Pure, {}, {},
            {pin("value", ValueType::Float)}, {pin("result", ValueType::Float)}, node_negate, "invert minus"});
    define({"math.abs", "Absolute", "Math", "Magnitude without sign.", BlueprintNodeKind::Pure, {}, {},
            {pin("value", ValueType::Float)}, {pin("result", ValueType::Float)}, node_abs, "abs magnitude"});
    define({"math.min", "Min", "Math", "Smaller of two floats.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Float), pin("b", ValueType::Float)},
            {pin("result", ValueType::Float)}, node_min, "smallest lower"});
    define({"math.max", "Max", "Math", "Larger of two floats.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Float), pin("b", ValueType::Float)},
            {pin("result", ValueType::Float)}, node_max, "largest upper"});
    define({"math.clamp", "Clamp", "Math", "Constrains a value to a range.", BlueprintNodeKind::Pure, {}, {},
            {pin("value", ValueType::Float), pin("min", ValueType::Float),
             pin("max", ValueType::Float, BlueprintValue::from_float(1.0f))},
            {pin("result", ValueType::Float)}, node_clamp, "limit range saturate"});
    define({"math.lerp", "Lerp", "Math", "Linear blend from A to B by Alpha.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Float), pin("b", ValueType::Float, BlueprintValue::from_float(1.0f)),
             pin("alpha", ValueType::Float)},
            {pin("result", ValueType::Float)}, node_lerp, "blend interpolate mix"});
    define({"math.sin", "Sin", "Math", "Sine of an angle in radians.", BlueprintNodeKind::Pure, {}, {},
            {pin("radians", ValueType::Float)}, {pin("result", ValueType::Float)}, node_sin, "trig sine"});
    define({"math.cos", "Cos", "Math", "Cosine of an angle in radians.", BlueprintNodeKind::Pure, {}, {},
            {pin("radians", ValueType::Float)}, {pin("result", ValueType::Float)}, node_cos, "trig cosine"});
    define({"math.sqrt", "Square Root", "Math", "Square root. Negative inputs yield zero.", BlueprintNodeKind::Pure, {}, {},
            {pin("value", ValueType::Float)}, {pin("result", ValueType::Float)}, node_sqrt, "root"});
    define({"math.pow", "Power", "Math", "Base raised to Exponent.", BlueprintNodeKind::Pure, {}, {},
            {pin("base", ValueType::Float), pin("exponent", ValueType::Float, BlueprintValue::from_float(2.0f))},
            {pin("result", ValueType::Float)}, node_pow, "exponent square"});
    define({"math.random_float", "Random Float in Range", "Math", "Uniform random float between Min and Max.", BlueprintNodeKind::Pure, {}, {},
            {pin("min", ValueType::Float), pin("max", ValueType::Float, BlueprintValue::from_float(1.0f))},
            {pin("result", ValueType::Float)}, node_random_float, "rand noise"});
    define({"math.random_int", "Random Integer in Range", "Math", "Uniform random integer between Min and Max inclusive.", BlueprintNodeKind::Pure, {}, {},
            {pin("min", ValueType::Int), pin("max", ValueType::Int, BlueprintValue::from_int(1))},
            {pin("result", ValueType::Int)}, node_random_int, "rand dice"});

    // -------------------------------------------------------------------
    // Vector
    // -------------------------------------------------------------------
    define({"vector.make", "Make Vector", "Vector", "Builds a vector from three floats.", BlueprintNodeKind::Pure, {}, {},
            {pin("x", ValueType::Float), pin("y", ValueType::Float), pin("z", ValueType::Float)},
            {pin("vector", ValueType::Vector)}, node_vector_make, "construct xyz"});
    define({"vector.break", "Break Vector", "Vector", "Splits a vector into its components.", BlueprintNodeKind::Pure, {}, {},
            {pin("vector", ValueType::Vector)},
            {pin("x", ValueType::Float), pin("y", ValueType::Float), pin("z", ValueType::Float)},
            node_vector_break, "split components xyz"});
    define({"vector.add", "Add Vectors", "Vector", "Component-wise addition.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Vector), pin("b", ValueType::Vector)},
            {pin("result", ValueType::Vector)}, node_vector_add, "plus sum"});
    define({"vector.subtract", "Subtract Vectors", "Vector", "Component-wise subtraction.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Vector), pin("b", ValueType::Vector)},
            {pin("result", ValueType::Vector)}, node_vector_subtract, "minus delta"});
    define({"vector.scale", "Scale Vector", "Vector", "Multiplies a vector by a scalar.", BlueprintNodeKind::Pure, {}, {},
            {pin("vector", ValueType::Vector), pin("scale", ValueType::Float, BlueprintValue::from_float(1.0f))},
            {pin("result", ValueType::Vector)}, node_vector_scale, "multiply times"});
    define({"vector.dot", "Dot Product", "Vector", "Dot product of two vectors.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Vector), pin("b", ValueType::Vector)},
            {pin("result", ValueType::Float)}, node_vector_dot, "projection"});
    define({"vector.cross", "Cross Product", "Vector", "Cross product of two vectors.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Vector), pin("b", ValueType::Vector)},
            {pin("result", ValueType::Vector)}, node_vector_cross, "perpendicular normal"});
    define({"vector.length", "Vector Length", "Vector", "Magnitude of a vector.", BlueprintNodeKind::Pure, {}, {},
            {pin("vector", ValueType::Vector)}, {pin("result", ValueType::Float)}, node_vector_length, "magnitude size"});
    define({"vector.normalize", "Normalize", "Vector", "Unit-length version of a vector.", BlueprintNodeKind::Pure, {}, {},
            {pin("vector", ValueType::Vector)}, {pin("result", ValueType::Vector)}, node_vector_normalize, "unit direction"});
    define({"vector.distance", "Distance", "Vector", "Distance between two points.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Vector), pin("b", ValueType::Vector)},
            {pin("result", ValueType::Float)}, node_vector_distance, "length between"});
    define({"vector.lerp", "Lerp Vectors", "Vector", "Linear blend between two vectors.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Vector), pin("b", ValueType::Vector), pin("alpha", ValueType::Float)},
            {pin("result", ValueType::Vector)}, node_vector_lerp, "blend interpolate"});

    // -------------------------------------------------------------------
    // Logic
    // -------------------------------------------------------------------
    define({"logic.and", "AND", "Logic", "True when both inputs are true.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Bool), pin("b", ValueType::Bool)},
            {pin("result", ValueType::Bool)}, node_and, "both &&"});
    define({"logic.or", "OR", "Logic", "True when either input is true.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Bool), pin("b", ValueType::Bool)},
            {pin("result", ValueType::Bool)}, node_or, "either ||"});
    define({"logic.xor", "XOR", "Logic", "True when exactly one input is true.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Bool), pin("b", ValueType::Bool)},
            {pin("result", ValueType::Bool)}, node_xor, "exclusive"});
    define({"logic.not", "NOT", "Logic", "Inverts a boolean.", BlueprintNodeKind::Pure, {}, {},
            {pin("value", ValueType::Bool)}, {pin("result", ValueType::Bool)}, node_not, "invert negate !"});
    define({"logic.greater", "Greater", "Logic", "A > B", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Float), pin("b", ValueType::Float)},
            {pin("result", ValueType::Bool)}, node_greater, "compare >"});
    define({"logic.greater_equal", "Greater or Equal", "Logic", "A >= B", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Float), pin("b", ValueType::Float)},
            {pin("result", ValueType::Bool)}, node_greater_equal, "compare >="});
    define({"logic.less", "Less", "Logic", "A < B", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Float), pin("b", ValueType::Float)},
            {pin("result", ValueType::Bool)}, node_less, "compare <"});
    define({"logic.less_equal", "Less or Equal", "Logic", "A <= B", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Float), pin("b", ValueType::Float)},
            {pin("result", ValueType::Bool)}, node_less_equal, "compare <="});
    define({"logic.equal_float", "Equal (Float)", "Logic", "Compares two floats within a tolerance.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Float), pin("b", ValueType::Float),
             pin("tolerance", ValueType::Float, BlueprintValue::from_float(0.0001f))},
            {pin("result", ValueType::Bool)}, node_equal_float, "compare == nearly"});
    define({"logic.equal_int", "Equal (Integer)", "Logic", "Compares two integers.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Int), pin("b", ValueType::Int)},
            {pin("result", ValueType::Bool)}, node_equal_int, "compare =="});
    define({"logic.equal_bool", "Equal (Boolean)", "Logic", "Compares two booleans.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::Bool), pin("b", ValueType::Bool)},
            {pin("result", ValueType::Bool)}, node_equal_bool, "compare =="});
    define({"logic.equal_string", "Equal (String)", "Logic", "Compares two strings exactly.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::String), pin("b", ValueType::String)},
            {pin("result", ValueType::Bool)}, node_equal_string, "compare == text"});
    define({"logic.select", "Select", "Logic", "Picks one of two values based on a condition. The value type follows whatever you wire in.", BlueprintNodeKind::Pure, {}, {},
            {pin("condition", ValueType::Bool), pin("onTrue", ValueType::Wildcard), pin("onFalse", ValueType::Wildcard)},
            {pin("result", ValueType::Wildcard)}, node_select, "ternary pick choose"});

    // -------------------------------------------------------------------
    // Conversion and strings
    // -------------------------------------------------------------------
    define({"convert.to_string", "To String", "Conversion", "Renders any value as text.", BlueprintNodeKind::Pure, {}, {},
            {pin("value", ValueType::Wildcard)}, {pin("result", ValueType::String)}, node_to_string, "cast text format"});
    define({"convert.to_int", "To Integer", "Conversion", "Truncates towards zero.", BlueprintNodeKind::Pure, {}, {},
            {pin("value", ValueType::Float)}, {pin("result", ValueType::Int)}, node_to_int, "cast truncate"});
    define({"convert.to_float", "To Float", "Conversion", "Widens to a float.", BlueprintNodeKind::Pure, {}, {},
            {pin("value", ValueType::Int)}, {pin("result", ValueType::Float)}, node_to_float, "cast"});
    define({"convert.to_bool", "To Boolean", "Conversion", "Non-zero and non-empty values are true.", BlueprintNodeKind::Pure, {}, {},
            {pin("value", ValueType::Wildcard)}, {pin("result", ValueType::Bool)}, node_to_bool, "cast truthy"});
    define({"string.concat", "Append", "String", "Joins two strings.", BlueprintNodeKind::Pure, {}, {},
            {pin("a", ValueType::String), pin("b", ValueType::String)},
            {pin("result", ValueType::String)}, node_string_concat, "join concat +"});
    define({"string.length", "String Length", "String", "Number of characters.", BlueprintNodeKind::Pure, {}, {},
            {pin("value", ValueType::String)}, {pin("result", ValueType::Int)}, node_string_length, "size count"});

    // -------------------------------------------------------------------
    // Entity and transform
    // -------------------------------------------------------------------
    define({"entity.self", "Self", "Entity", "The entity this Blueprint is attached to.", BlueprintNodeKind::Pure, {}, {},
            {}, {pin("self", ValueType::Entity)}, node_self, "this owner me"});
    define({"entity.is_valid", "Is Valid", "Entity", "True when the entity still exists.", BlueprintNodeKind::Pure, {}, {},
            {entityTarget()}, {pin("valid", ValueType::Bool)}, node_is_valid, "alive exists null"});
    define({"entity.find_by_name", "Find Entity by Name", "Entity", "First active entity with this name.", BlueprintNodeKind::Pure, {}, {},
            {pin("name", ValueType::String)},
            {pin("entity", ValueType::Entity), pin("found", ValueType::Bool)},
            node_find_by_name, "lookup search get"});
    define({"entity.get_name", "Get Name", "Entity", "Reads the entity's display name.", BlueprintNodeKind::Pure, {}, {},
            {entityTarget()}, {pin("name", ValueType::String)}, node_get_name, "label"});
    define({"entity.set_name", "Set Name", "Entity", "Renames the entity.", BlueprintNodeKind::Exec,
            execs({"exec"}), execs({"then"}),
            {entityTarget(), pin("name", ValueType::String)}, {}, node_set_name, "rename label"});
    define({"entity.get_parent", "Get Parent", "Entity", "Parent entity in the hierarchy, or None.", BlueprintNodeKind::Pure, {}, {},
            {entityTarget()}, {pin("parent", ValueType::Entity)}, node_get_parent, "hierarchy owner"});
    define({"entity.get_child_count", "Get Child Count", "Entity", "How many direct children the entity has.", BlueprintNodeKind::Pure, {}, {},
            {entityTarget()}, {pin("count", ValueType::Int)}, node_get_child_count, "hierarchy children"});
    define({"entity.get_child", "Get Child", "Entity", "Direct child at the given index, or None.", BlueprintNodeKind::Pure, {}, {},
            {entityTarget(), pin("index", ValueType::Int)},
            {pin("child", ValueType::Entity)}, node_get_child, "hierarchy children"});
    define({"entity.destroy", "Destroy Entity", "Entity", "Removes the entity and all of its components.", BlueprintNodeKind::Exec,
            execs({"exec"}), execs({"then"}), {entityTarget()}, {}, node_destroy_entity, "delete kill remove"});

    define({"transform.get_position", "Get Position", "Transform", "World position of the entity.", BlueprintNodeKind::Pure, {}, {},
            {entityTarget()}, {pin("position", ValueType::Vector)}, node_get_position, "location translation"});
    define({"transform.set_position", "Set Position", "Transform", "Moves the entity to a position.", BlueprintNodeKind::Exec,
            execs({"exec"}), execs({"then"}),
            {entityTarget(), pin("position", ValueType::Vector)}, {}, node_set_position, "location teleport move"});
    define({"transform.add_offset", "Add Offset", "Transform", "Moves the entity by a delta.", BlueprintNodeKind::Exec,
            execs({"exec"}), execs({"then"}),
            {entityTarget(), pin("delta", ValueType::Vector)}, {}, node_add_offset, "translate move nudge"});
    define({"transform.get_rotation", "Get Rotation", "Transform", "Rotation as Euler angles in degrees.", BlueprintNodeKind::Pure, {}, {},
            {entityTarget()}, {pin("eulerDegrees", ValueType::Vector)}, node_get_rotation, "euler orientation angle"});
    define({"transform.set_rotation", "Set Rotation", "Transform", "Sets rotation from Euler angles in degrees.", BlueprintNodeKind::Exec,
            execs({"exec"}), execs({"then"}),
            {entityTarget(), pin("eulerDegrees", ValueType::Vector)}, {}, node_set_rotation, "euler orient turn"});
    define({"transform.get_scale", "Get Scale", "Transform", "Scale of the entity.", BlueprintNodeKind::Pure, {}, {},
            {entityTarget()}, {pin("scale", ValueType::Vector)}, node_get_scale, "size"});
    define({"transform.set_scale", "Set Scale", "Transform", "Resizes the entity.", BlueprintNodeKind::Exec,
            execs({"exec"}), execs({"then"}),
            {entityTarget(), pin("scale", ValueType::Vector, BlueprintValue::from_vector(math::Vec3(1.0f, 1.0f, 1.0f)))},
            {}, node_set_scale, "size resize"});

    // -------------------------------------------------------------------
    // Physics and audio
    // -------------------------------------------------------------------
    define({"physics.add_force", "Add Force", "Physics", "Applies a continuous force to a rigid body.", BlueprintNodeKind::Exec,
            execs({"exec"}), execs({"then"}),
            {entityTarget(), pin("force", ValueType::Vector)}, {}, node_add_force, "push accelerate"});
    define({"physics.add_impulse", "Add Impulse", "Physics", "Applies an instantaneous impulse to a rigid body.", BlueprintNodeKind::Exec,
            execs({"exec"}), execs({"then"}),
            {entityTarget(), pin("impulse", ValueType::Vector)}, {}, node_add_impulse, "kick jump launch"});
    define({"physics.set_velocity", "Set Linear Velocity", "Physics", "Overwrites a rigid body's linear velocity.", BlueprintNodeKind::Exec,
            execs({"exec"}), execs({"then"}),
            {entityTarget(), pin("velocity", ValueType::Vector)}, {}, node_set_velocity, "speed move"});

    define({"audio.play", "Play Sound", "Audio", "Plays the entity's audio source.", BlueprintNodeKind::Exec,
            execs({"exec"}), execs({"then"}), {entityTarget()}, {}, node_play_audio, "sfx sound start"});
    define({"audio.stop", "Stop Sound", "Audio", "Stops the entity's audio source.", BlueprintNodeKind::Exec,
            execs({"exec"}), execs({"then"}), {entityTarget()}, {}, node_stop_audio, "sfx silence"});
    define({"audio.set_volume", "Set Volume", "Audio", "Sets the entity's audio source volume.", BlueprintNodeKind::Exec,
            execs({"exec"}), execs({"then"}),
            {entityTarget(), pin("volume", ValueType::Float, BlueprintValue::from_float(1.0f))},
            {}, node_set_volume, "loudness gain"});

    // -------------------------------------------------------------------
    // Debug, API and time
    // -------------------------------------------------------------------
    define({"debug.print", "Print String", "Debug", "Writes a line to the editor debug console.", BlueprintNodeKind::Exec,
            execs({"exec"}), execs({"then"}),
            {pin("text", ValueType::String, BlueprintValue::from_string("Hello"))},
            {}, node_print, "log console trace"});
    define({"debug.observe", "Observe", "Debug", "Publishes a value to the HadesAPI observation set for RL training.", BlueprintNodeKind::Exec,
            execs({"exec"}), execs({"then"}),
            {pin("key", ValueType::String), pin("value", ValueType::Wildcard)},
            {}, node_observe, "telemetry metric rl"});
    define({"world.load", "Load World", "World", "Queues a switch to another saved world.", BlueprintNodeKind::Exec,
            execs({"exec"}), execs({"then"}),
            {pin("worldName", ValueType::String)}, {}, node_load_world, "scene level change"});

    define({"time.delta_seconds", "Get Delta Seconds", "Time", "Seconds elapsed since the previous frame.", BlueprintNodeKind::Pure, {}, {},
            {}, {pin("deltaSeconds", ValueType::Float)}, node_delta_seconds, "dt frame"});
    define({"time.seconds", "Get Time Seconds", "Time", "Seconds since this Blueprint started playing.", BlueprintNodeKind::Pure, {}, {},
            {}, {pin("seconds", ValueType::Float)}, node_time_seconds, "elapsed clock"});

    // -------------------------------------------------------------------
    // Constants
    // -------------------------------------------------------------------
    define({"literal.float", "Float Literal", "Constants", "A constant float you can fan out to several pins.", BlueprintNodeKind::Pure, {}, {},
            {pin("value", ValueType::Float)}, {pin("value", ValueType::Float)}, node_literal, "constant number"});
    define({"literal.int", "Integer Literal", "Constants", "A constant integer.", BlueprintNodeKind::Pure, {}, {},
            {pin("value", ValueType::Int)}, {pin("value", ValueType::Int)}, node_literal, "constant number"});
    define({"literal.bool", "Boolean Literal", "Constants", "A constant boolean.", BlueprintNodeKind::Pure, {}, {},
            {pin("value", ValueType::Bool)}, {pin("value", ValueType::Bool)}, node_literal, "constant true false"});
    define({"literal.string", "String Literal", "Constants", "A constant string.", BlueprintNodeKind::Pure, {}, {},
            {pin("value", ValueType::String)}, {pin("value", ValueType::String)}, node_literal, "constant text"});
    define({"literal.vector", "Vector Literal", "Constants", "A constant vector.", BlueprintNodeKind::Pure, {}, {},
            {pin("value", ValueType::Vector)}, {pin("value", ValueType::Vector)}, node_literal, "constant xyz"});

    register_animation_blueprint_nodes();
    register_script_blueprint_nodes();
    register_ui_blueprint_nodes();
  }
}
