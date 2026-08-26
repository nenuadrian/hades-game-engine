// Blueprint node library for the UI system.
//
// Mirrors the structure of `blueprint/blueprint_nodes.cpp`: every node is a
// plain function pointer taking a BlueprintExecContext. The bodies stay thin
// -- widget state lives in UICanvasComponent, so nodes go straight at the
// ECS through the shared ui_widget_ops helpers (the same ones hades::UI
// uses) and tolerate entities that have no canvas or no such widget.

#include "ui_blueprint_nodes.hpp"

#include <string>
#include <utility>
#include <vector>

#include "../blueprint/blueprint_exec_context.hpp"
#include "../blueprint/blueprint_node_registry.hpp"
#include "../components/ui_canvas_component.hpp"
#include "../core/ecs/component_manager.hpp"
#include "ui_widget_ops.hpp"

namespace hades
{
  namespace
  {
    // -----------------------------------------------------------------------
    // Pin construction helpers (file-local by design, copied from
    // blueprint_nodes.cpp like every other category file)
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

    Entity::EntityId resolve_target(BlueprintExecContext &context, int inputIndex)
    {
      const Entity::EntityId target = context.input(inputIndex).as_entity();
      return target == Entity::INVALID ? context.entity() : target;
    }

    /// Every node addresses a widget as (target entity, widget id string) in
    /// its first two data inputs.
    UIWidget *resolve_widget(BlueprintExecContext &context)
    {
      return ui::find_widget(
          context.components(), resolve_target(context, 0), context.input(1).as_string());
    }

    // -----------------------------------------------------------------------
    // Node implementations
    // -----------------------------------------------------------------------

    BlueprintExecResult node_ui_set_text(BlueprintExecContext &context)
    {
      if (UIWidget *widget = resolve_widget(context))
      {
        widget->text = context.input(2).as_string();
      }
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_ui_set_value(BlueprintExecContext &context)
    {
      if (UIWidget *widget = resolve_widget(context))
      {
        widget->value = context.input(2).as_float();
      }
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_ui_set_visible(BlueprintExecContext &context)
    {
      if (UIWidget *widget = resolve_widget(context))
      {
        widget->visible = context.input(2).as_bool();
      }
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_ui_set_color(BlueprintExecContext &context)
    {
      if (UIWidget *widget = resolve_widget(context))
      {
        const math::Vec3 rgb = context.input(2).as_vector();
        widget->colorR = rgb.x;
        widget->colorG = rgb.y;
        widget->colorB = rgb.z;
        widget->colorA = context.input(3).as_float();
      }
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_ui_set_fill_color(BlueprintExecContext &context)
    {
      if (UIWidget *widget = resolve_widget(context))
      {
        const math::Vec3 rgb = context.input(2).as_vector();
        widget->fillColorR = rgb.x;
        widget->fillColorG = rgb.y;
        widget->fillColorB = rgb.z;
        widget->fillColorA = context.input(3).as_float();
      }
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_ui_set_canvas_visible(BlueprintExecContext &context)
    {
      auto &cm = context.components();
      const Entity::EntityId target = resolve_target(context, 0);
      if (cm.hasComponent<UICanvasComponent>(target))
      {
        cm.getComponent<UICanvasComponent>(target).visible = context.input(1).as_bool();
      }
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_ui_get_value(BlueprintExecContext &context)
    {
      const UIWidget *widget = resolve_widget(context);
      context.set_output(0, BlueprintValue::from_float(widget != nullptr ? widget->value : 0.0f));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_ui_get_text(BlueprintExecContext &context)
    {
      const UIWidget *widget = resolve_widget(context);
      context.set_output(0, BlueprintValue::from_string(
                               widget != nullptr ? widget->text : std::string()));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_ui_widget_exists(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_bool(resolve_widget(context) != nullptr));
      return BlueprintExecResult::stop();
    }
  }

  void register_ui_blueprint_nodes()
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

    const auto widgetPin = []()
    {
      BlueprintPinSpec spec = pin("widget", ValueType::String);
      spec.displayName = "Widget Id";
      spec.tooltip = "Id of a widget on the target entity's UI Canvas.";
      return spec;
    };

    define({"ui.set_text", "Set Widget Text", "UI",
            "Writes the text of a widget on the target entity's UI Canvas.",
            BlueprintNodeKind::Exec, execs({"exec"}), execs({"then"}),
            {entityTarget(), widgetPin(), pin("text", ValueType::String)},
            {}, node_ui_set_text, "hud label caption string"});
    define({"ui.set_value", "Set Widget Value", "UI",
            "Writes a widget's 0..1 value -- a bar's fill fraction.",
            BlueprintNodeKind::Exec, execs({"exec"}), execs({"then"}),
            {entityTarget(), widgetPin(),
             pin("value", ValueType::Float, BlueprintValue::from_float(1.0f))},
            {}, node_ui_set_value, "hud bar health progress fraction fill"});
    define({"ui.set_visible", "Set Widget Visible", "UI",
            "Shows or hides a widget and its children.",
            BlueprintNodeKind::Exec, execs({"exec"}), execs({"then"}),
            {entityTarget(), widgetPin(),
             pin("visible", ValueType::Bool, BlueprintValue::from_bool(true))},
            {}, node_ui_set_visible, "hud show hide toggle"});
    define({"ui.set_color", "Set Widget Color", "UI",
            "Sets a widget's primary color (panel/bar background, text glyphs, button face).",
            BlueprintNodeKind::Exec, execs({"exec"}), execs({"then"}),
            {entityTarget(), widgetPin(), pin("rgb", ValueType::Vector),
             pin("alpha", ValueType::Float, BlueprintValue::from_float(1.0f))},
            {}, node_ui_set_color, "hud tint rgba"});
    define({"ui.set_fill_color", "Set Widget Fill Color", "UI",
            "Sets a widget's secondary color (bar fill, button label).",
            BlueprintNodeKind::Exec, execs({"exec"}), execs({"then"}),
            {entityTarget(), widgetPin(), pin("rgb", ValueType::Vector),
             pin("alpha", ValueType::Float, BlueprintValue::from_float(1.0f))},
            {}, node_ui_set_fill_color, "hud bar tint rgba"});
    define({"ui.set_canvas_visible", "Set Canvas Visible", "UI",
            "Shows or hides the target entity's whole UI Canvas.",
            BlueprintNodeKind::Exec, execs({"exec"}), execs({"then"}),
            {entityTarget(),
             pin("visible", ValueType::Bool, BlueprintValue::from_bool(true))},
            {}, node_ui_set_canvas_visible, "hud menu show hide toggle"});

    define({"ui.get_value", "Get Widget Value", "UI",
            "Reads a widget's 0..1 value. 0 when the widget is missing.",
            BlueprintNodeKind::Pure, {}, {},
            {entityTarget(), widgetPin()},
            {pin("value", ValueType::Float)}, node_ui_get_value, "hud bar read"});
    define({"ui.get_text", "Get Widget Text", "UI",
            "Reads a widget's text. Empty when the widget is missing.",
            BlueprintNodeKind::Pure, {}, {},
            {entityTarget(), widgetPin()},
            {pin("text", ValueType::String)}, node_ui_get_text, "hud label read"});
    define({"ui.widget_exists", "Widget Exists", "UI",
            "Whether the target entity's UI Canvas has a widget with this id.",
            BlueprintNodeKind::Pure, {}, {},
            {entityTarget(), widgetPin()},
            {pin("exists", ValueType::Bool)}, node_ui_widget_exists, "hud has valid"});
  }
}
