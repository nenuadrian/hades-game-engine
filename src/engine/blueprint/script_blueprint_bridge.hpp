#ifndef HADES_ENGINE_BLUEPRINT_SCRIPT_BLUEPRINT_BRIDGE_HPP
#define HADES_ENGINE_BLUEPRINT_SCRIPT_BLUEPRINT_BRIDGE_HPP

// Engine-side half of the `hades::Blueprints` facade.
//
// Separate from `script_blueprint.hpp` because everything here needs
// `BlueprintValue`, which drags in <nlohmann/json.hpp>. User scripts include
// only the light header; the runtime includes this one.

#include <string>
#include <vector>

#include "../core/ecs/entity.hpp"
#include "../runtime/hades_value.hpp"
#include "blueprint_value.hpp"

namespace hades
{
  class BlueprintRuntime;

  /// One `Blueprints::sendEvent` / `broadcastEvent` call waiting to be
  /// delivered. Queued rather than dispatched on the spot so a graph that
  /// calls a script that sends an event back cannot re-enter the VM
  /// mid-execution and clobber the instance's registers.
  struct PendingBlueprintEvent
  {
    /// `Entity::INVALID` means broadcast to every running instance.
    Entity::EntityId entity = Entity::INVALID;
    std::string eventName;
    std::vector<BlueprintValue> payload;
  };

  /// Point the facade at the runtime that is currently playing. Called by
  /// `BlueprintRuntime::start`; pass nullptr (or let `stop` do it) to make
  /// every facade call inert again. Last one to start wins, matching
  /// `register_script_audio_engine`.
  void register_script_blueprint_runtime(BlueprintRuntime *runtime);
  BlueprintRuntime *script_blueprint_runtime();

  /// Hand over everything queued since the last call and empty the queue.
  std::vector<PendingBlueprintEvent> drain_pending_blueprint_events();
  /// Drop anything queued without delivering it. Called on start and stop so
  /// events from a previous play session cannot leak into the next one.
  void clear_pending_blueprint_events();

  BlueprintValue to_blueprint_value(const ScriptValue &value);
  ScriptValue to_script_value(const BlueprintValue &value);
}

#endif
