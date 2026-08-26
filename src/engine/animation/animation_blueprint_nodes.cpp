// Blueprint node library for skeletal animation.
//
// Mirrors the structure of `blueprint/blueprint_nodes.cpp`: every node is a
// plain function pointer taking a BlueprintExecContext, pure nodes write
// their outputs and return `stop()`, exec nodes return the exec pin the VM
// should follow next. The bodies are deliberately thin — all of the real work
// lives behind `hades::Animation`, which tolerates entities that have no
// model, so nothing here has to check first.

#include "animation_blueprint_nodes.hpp"

#include <string>
#include <utility>
#include <vector>

#include "../blueprint/blueprint_node_registry.hpp"
#include "script_animation.hpp"

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

    // -----------------------------------------------------------------------
    // Node implementations — clip playback
    // -----------------------------------------------------------------------

    BlueprintExecResult node_anim_play(BlueprintExecContext &context)
    {
      Animation::play(resolve_target(context, 0), context.input(1).as_string(),
                      context.input(2).as_float(), context.input(3).as_bool());
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_anim_play_once(BlueprintExecContext &context)
    {
      Animation::playOnce(resolve_target(context, 0), context.input(1).as_string(),
                          context.input(2).as_float());
      return BlueprintExecResult::next(0);
    }

    /// The default layer of -1 stops every layer, which is what an author
    /// dropping a bare "Stop Animation" node expects.
    BlueprintExecResult node_anim_stop(BlueprintExecContext &context)
    {
      Animation::stop(resolve_target(context, 0));
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_anim_set_speed(BlueprintExecContext &context)
    {
      Animation::setSpeed(resolve_target(context, 0), context.input(1).as_float());
      return BlueprintExecResult::next(0);
    }

    // -----------------------------------------------------------------------
    // Node implementations — animator graph
    // -----------------------------------------------------------------------

    /// Still an exec node even though it reports success: the transition is a
    /// side effect, and authors want to chain off it whether or not the state
    /// existed. `success` is there for the failure branch, not for flow.
    BlueprintExecResult node_anim_goto_state(BlueprintExecContext &context)
    {
      const bool success = Animation::gotoState(
          resolve_target(context, 0), context.input(1).as_string(), context.input(2).as_float());
      context.set_output(0, BlueprintValue::from_bool(success));
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_anim_current_state(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_string(
                               Animation::currentState(resolve_target(context, 0))));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_anim_normalized_time(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_float(
                               Animation::normalizedTime(resolve_target(context, 0))));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_anim_is_playing(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_bool(
                               Animation::isPlaying(resolve_target(context, 0))));
      return BlueprintExecResult::stop();
    }

    // -----------------------------------------------------------------------
    // Node implementations — parameters and events
    // -----------------------------------------------------------------------

    BlueprintExecResult node_anim_set_float(BlueprintExecContext &context)
    {
      Animation::setFloat(resolve_target(context, 0), context.input(1).as_string(),
                          context.input(2).as_float());
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_anim_set_bool(BlueprintExecContext &context)
    {
      Animation::setBool(resolve_target(context, 0), context.input(1).as_string(),
                         context.input(2).as_bool());
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_anim_set_int(BlueprintExecContext &context)
    {
      Animation::setInt(resolve_target(context, 0), context.input(1).as_string(),
                        static_cast<int>(context.input(2).as_int()));
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_anim_set_trigger(BlueprintExecContext &context)
    {
      Animation::setTrigger(resolve_target(context, 0), context.input(1).as_string());
      return BlueprintExecResult::next(0);
    }

    BlueprintExecResult node_anim_get_float(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_float(Animation::getFloat(
                               resolve_target(context, 0), context.input(1).as_string())));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_anim_get_int(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_int(Animation::getInt(
                               resolve_target(context, 0), context.input(1).as_string())));
      return BlueprintExecResult::stop();
    }

    BlueprintExecResult node_anim_get_bool(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_bool(Animation::getBool(
                               resolve_target(context, 0), context.input(1).as_string())));
      return BlueprintExecResult::stop();
    }

    /// Safe to re-evaluate: `Animation::eventFired` is a non-destructive query
    /// over the events of the current frame, so the VM re-running this pure
    /// node once per consuming exec node (and once per loop iteration) yields
    /// the same answer every time, and fanning it out to several Branches
    /// works the way an author expects.
    BlueprintExecResult node_anim_event_fired(BlueprintExecContext &context)
    {
      context.set_output(0, BlueprintValue::from_bool(Animation::eventFired(
                               resolve_target(context, 0), context.input(1).as_string())));
      return BlueprintExecResult::stop();
    }
  }

  void register_animation_blueprint_nodes()
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
    // Clip playback
    // -------------------------------------------------------------------
    define({"anim.play", "Play Animation", "Animation",
            "Crossfades to an animation clip by name. Blend is the crossfade length in seconds; "
            "0 snaps, and a negative value (the default) uses the Animator component's Default Blend.",
            BlueprintNodeKind::Exec, execs({"exec"}), execs({"then"}),
            {entityTarget(),
             pin("clip", ValueType::String),
             pin("blend", ValueType::Float, BlueprintValue::from_float(Animation::kComponentBlend)),
             pin("loop", ValueType::Bool, BlueprintValue::from_bool(true))},
            {}, node_anim_play, "animation clip start crossfade blend loop skeletal"});

    define({"anim.play_once", "Play Animation Once", "Animation",
            "Crossfades to a clip, plays it a single time and holds the last frame. Blend is the "
            "crossfade length in seconds; 0 snaps, and a negative value (the default) uses the "
            "Animator component's Default Blend.",
            BlueprintNodeKind::Exec, execs({"exec"}), execs({"then"}),
            {entityTarget(),
             pin("clip", ValueType::String),
             pin("blend", ValueType::Float, BlueprintValue::from_float(Animation::kComponentBlend))},
            {}, node_anim_play_once, "animation clip oneshot single hold attack skeletal"});

    define({"anim.stop", "Stop Animation", "Animation",
            "Stops playback on every animation layer and leaves the skeleton on its current pose.",
            BlueprintNodeKind::Exec, execs({"exec"}), execs({"then"}),
            {entityTarget()}, {}, node_anim_stop, "animation halt pause freeze clip"});

    define({"anim.set_speed", "Set Animation Speed", "Animation",
            "Scales playback rate. 1 is authored speed, 0 freezes, negative values play backwards.",
            BlueprintNodeKind::Exec, execs({"exec"}), execs({"then"}),
            {entityTarget(), pin("speed", ValueType::Float, BlueprintValue::from_float(1.0f))},
            {}, node_anim_set_speed, "animation rate playback timescale slow fast"});

    // -------------------------------------------------------------------
    // Animator graph
    // -------------------------------------------------------------------
    define({"anim.goto_state", "Go To Animation State", "Animation",
            "Crossfades the animator graph to a named state, bypassing its transition rules. "
            "Success is false when the graph has no such state.",
            BlueprintNodeKind::Exec, execs({"exec"}), execs({"then"}),
            {entityTarget(),
             pin("state", ValueType::String),
             pin("blend", ValueType::Float, BlueprintValue::from_float(0.2f))},
            {pin("success", ValueType::Bool)},
            node_anim_goto_state, "animation animator graph state machine transition force"});

    define({"anim.current_state", "Get Animation State", "Animation",
            "Name of the animator graph state currently playing, or empty when there is no graph.",
            BlueprintNodeKind::Pure, {}, {},
            {entityTarget()}, {pin("state", ValueType::String)},
            node_anim_current_state, "animation animator graph state machine name current"});

    define({"anim.normalized_time", "Get Animation Time", "Animation",
            "Playback position of the active clip in 0..1, so 0.5 is halfway through.",
            BlueprintNodeKind::Pure, {}, {},
            {entityTarget()}, {pin("normalized", ValueType::Float)},
            node_anim_normalized_time, "animation progress phase percent position clip time"});

    define({"anim.is_playing", "Is Animation Playing", "Animation",
            "True while the entity's animation is advancing.",
            BlueprintNodeKind::Pure, {}, {},
            {entityTarget()}, {pin("playing", ValueType::Bool)},
            node_anim_is_playing, "animation active paused stopped running"});

    // -------------------------------------------------------------------
    // Parameters
    // -------------------------------------------------------------------
    define({"anim.set_float", "Set Animation Float", "Animation",
            "Writes a float parameter the animator graph's transitions read, such as movement speed.",
            BlueprintNodeKind::Exec, execs({"exec"}), execs({"then"}),
            {entityTarget(), pin("name", ValueType::String), pin("value", ValueType::Float)},
            {}, node_anim_set_float, "animation animator parameter variable float speed blend"});

    define({"anim.set_bool", "Set Animation Bool", "Animation",
            "Writes a boolean parameter the animator graph's transitions read, such as grounded.",
            BlueprintNodeKind::Exec, execs({"exec"}), execs({"then"}),
            {entityTarget(), pin("name", ValueType::String), pin("value", ValueType::Bool)},
            {}, node_anim_set_bool, "animation animator parameter variable bool flag condition"});

    define({"anim.set_int", "Set Animation Int", "Animation",
            "Writes an integer parameter the animator graph's transitions read, such as a weapon index.",
            BlueprintNodeKind::Exec, execs({"exec"}), execs({"then"}),
            {entityTarget(), pin("name", ValueType::String), pin("value", ValueType::Int)},
            {}, node_anim_set_int, "animation animator parameter variable int index count"});

    define({"anim.set_trigger", "Set Animation Trigger", "Animation",
            "Fires a one-shot trigger parameter. The graph consumes it on the next transition it enables.",
            BlueprintNodeKind::Exec, execs({"exec"}), execs({"then"}),
            {entityTarget(), pin("name", ValueType::String)},
            {}, node_anim_set_trigger, "animation animator parameter trigger fire pulse oneshot"});

    define({"anim.get_float", "Get Animation Float", "Animation",
            "Reads back a float parameter, or 0 when the parameter does not exist.",
            BlueprintNodeKind::Pure, {}, {},
            {entityTarget(), pin("name", ValueType::String)},
            {pin("value", ValueType::Float)},
            node_anim_get_float, "animation animator parameter variable float read"});

    define({"anim.get_int", "Get Animation Int", "Animation",
            "Reads back an integer parameter, or 0 when the parameter does not exist.",
            BlueprintNodeKind::Pure, {}, {},
            {entityTarget(), pin("name", ValueType::String)},
            {pin("value", ValueType::Int)},
            node_anim_get_int, "animation animator parameter variable int whole number read"});

    define({"anim.get_bool", "Get Animation Bool", "Animation",
            "Reads back a boolean parameter, or false when the parameter does not exist.",
            BlueprintNodeKind::Pure, {}, {},
            {entityTarget(), pin("name", ValueType::String)},
            {pin("value", ValueType::Bool)},
            node_anim_get_bool, "animation animator parameter variable bool flag read"});

    // -------------------------------------------------------------------
    // Events
    // -------------------------------------------------------------------
    define({"anim.event_fired", "Animation Event Fired", "Animation",
            "True on the frame a named animation event fires, for syncing footsteps, hit windows and VFX.",
            BlueprintNodeKind::Pure, {}, {},
            {entityTarget(), pin("name", ValueType::String)},
            {pin("fired", ValueType::Bool)},
            node_anim_event_fired, "animation event notify keyframe footstep hit marker"});
  }
}
