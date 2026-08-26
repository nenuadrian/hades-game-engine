#include "script_blueprint.hpp"

#include <mutex>

#include "blueprint_runtime.hpp"
#include "script_blueprint_bridge.hpp"

namespace hades
{
  namespace
  {
    std::mutex &bridge_mutex()
    {
      static std::mutex mutex;
      return mutex;
    }

    BlueprintRuntime *&registered_runtime()
    {
      static BlueprintRuntime *runtime = nullptr;
      return runtime;
    }

    std::vector<PendingBlueprintEvent> &pending_events()
    {
      static std::vector<PendingBlueprintEvent> events;
      return events;
    }

    /// Beyond this the queue is assumed to be a script/graph ping-pong that
    /// never settles. Excess is dropped rather than grown without bound.
    constexpr std::size_t kMaxPendingEvents = 4096;

    /// The runtime, but only while it is actually playing. Every facade entry
    /// point goes through here so a stale pointer or a stopped runtime turns
    /// the call into a no-op instead of a crash.
    BlueprintRuntime *live_runtime()
    {
      std::lock_guard<std::mutex> lock(bridge_mutex());
      BlueprintRuntime *runtime = registered_runtime();
      return runtime != nullptr && runtime->is_running() ? runtime : nullptr;
    }

    void enqueue(Entity::EntityId entity, const std::string &eventName,
                 const std::vector<ScriptValue> &payload)
    {
      if (eventName.empty())
      {
        return;
      }

      PendingBlueprintEvent event;
      event.entity = entity;
      event.eventName = eventName;
      event.payload.reserve(payload.size());
      for (const auto &value : payload)
      {
        event.payload.push_back(to_blueprint_value(value));
      }

      std::lock_guard<std::mutex> lock(bridge_mutex());
      if (pending_events().size() >= kMaxPendingEvents)
      {
        return;
      }
      pending_events().push_back(std::move(event));
    }

    BlueprintValue read_variable(Entity::EntityId entity, const std::string &name, bool *found)
    {
      if (found != nullptr)
      {
        *found = false;
      }

      BlueprintRuntime *runtime = live_runtime();
      if (runtime == nullptr || name.empty())
      {
        return BlueprintValue();
      }

      return runtime->get_variable(entity, name, found);
    }

    bool write_variable(Entity::EntityId entity, const std::string &name, const BlueprintValue &value)
    {
      BlueprintRuntime *runtime = live_runtime();
      if (runtime == nullptr || name.empty())
      {
        return false;
      }

      return runtime->set_variable(entity, name, value) > 0;
    }
  }

  // -------------------------------------------------------------------------
  // Bridge plumbing
  // -------------------------------------------------------------------------

  void register_script_blueprint_runtime(BlueprintRuntime *runtime)
  {
    std::lock_guard<std::mutex> lock(bridge_mutex());
    registered_runtime() = runtime;
  }

  BlueprintRuntime *script_blueprint_runtime()
  {
    std::lock_guard<std::mutex> lock(bridge_mutex());
    return registered_runtime();
  }

  std::vector<PendingBlueprintEvent> drain_pending_blueprint_events()
  {
    std::lock_guard<std::mutex> lock(bridge_mutex());
    std::vector<PendingBlueprintEvent> drained;
    drained.swap(pending_events());
    return drained;
  }

  void clear_pending_blueprint_events()
  {
    std::lock_guard<std::mutex> lock(bridge_mutex());
    pending_events().clear();
  }

  BlueprintValue to_blueprint_value(const ScriptValue &value)
  {
    switch (value.type())
    {
    case ScriptValueType::Bool:
      return BlueprintValue::from_bool(value.asBool());
    case ScriptValueType::Int:
      return BlueprintValue::from_int(value.asInt());
    case ScriptValueType::Float:
      return BlueprintValue::from_float(value.asFloat());
    case ScriptValueType::String:
      return BlueprintValue::from_string(value.asString());
    case ScriptValueType::Vector:
      return BlueprintValue::from_vector(value.asVector());
    case ScriptValueType::Entity:
      return BlueprintValue::from_entity(value.asEntity());
    case ScriptValueType::None:
    default:
      return BlueprintValue();
    }
  }

  ScriptValue to_script_value(const BlueprintValue &value)
  {
    switch (value.type())
    {
    case ValueType::Bool:
      return ScriptValue::fromBool(value.as_bool());
    case ValueType::Int:
      return ScriptValue::fromInt(value.as_int());
    case ValueType::Float:
      return ScriptValue::fromFloat(value.as_float());
    case ValueType::String:
      return ScriptValue::fromString(value.as_string());
    case ValueType::Vector:
      return ScriptValue::fromVector(value.as_vector());
    case ValueType::Entity:
      return ScriptValue::fromEntity(value.as_entity());
    case ValueType::Exec:
    case ValueType::Wildcard:
    default:
      return ScriptValue();
    }
  }

  // -------------------------------------------------------------------------
  // hades::Blueprints
  // -------------------------------------------------------------------------

  bool Blueprints::isRunning()
  {
    return live_runtime() != nullptr;
  }

  bool Blueprints::has(Entity::EntityId entity)
  {
    return count(entity) > 0;
  }

  int Blueprints::count(Entity::EntityId entity)
  {
    BlueprintRuntime *runtime = live_runtime();
    return runtime == nullptr ? 0 : runtime->instance_count_for(entity);
  }

  void Blueprints::sendEvent(Entity::EntityId entity, const std::string &eventName,
                             const std::vector<ScriptValue> &payload)
  {
    if (entity == Entity::INVALID)
    {
      return;
    }
    enqueue(entity, eventName, payload);
  }

  void Blueprints::broadcastEvent(const std::string &eventName,
                                  const std::vector<ScriptValue> &payload)
  {
    enqueue(Entity::INVALID, eventName, payload);
  }

  bool Blueprints::hasVariable(Entity::EntityId entity, const std::string &name)
  {
    bool found = false;
    read_variable(entity, name, &found);
    return found;
  }

  ScriptValue Blueprints::getVariable(Entity::EntityId entity, const std::string &name)
  {
    bool found = false;
    const BlueprintValue value = read_variable(entity, name, &found);
    return found ? to_script_value(value) : ScriptValue();
  }

  bool Blueprints::setVariable(Entity::EntityId entity, const std::string &name, const ScriptValue &value)
  {
    return write_variable(entity, name, to_blueprint_value(value));
  }

  float Blueprints::getFloat(Entity::EntityId entity, const std::string &name)
  {
    return read_variable(entity, name, nullptr).as_float();
  }

  int Blueprints::getInt(Entity::EntityId entity, const std::string &name)
  {
    return static_cast<int>(read_variable(entity, name, nullptr).as_int());
  }

  bool Blueprints::getBool(Entity::EntityId entity, const std::string &name)
  {
    return read_variable(entity, name, nullptr).as_bool();
  }

  std::string Blueprints::getString(Entity::EntityId entity, const std::string &name)
  {
    bool found = false;
    const BlueprintValue value = read_variable(entity, name, &found);
    return found ? value.as_string() : std::string();
  }

  math::Vec3 Blueprints::getVector(Entity::EntityId entity, const std::string &name)
  {
    bool found = false;
    const BlueprintValue value = read_variable(entity, name, &found);
    return found ? value.as_vector() : math::Vec3();
  }

  bool Blueprints::setFloat(Entity::EntityId entity, const std::string &name, float value)
  {
    return write_variable(entity, name, BlueprintValue::from_float(value));
  }

  bool Blueprints::setInt(Entity::EntityId entity, const std::string &name, int value)
  {
    return write_variable(entity, name, BlueprintValue::from_int(static_cast<std::int32_t>(value)));
  }

  bool Blueprints::setBool(Entity::EntityId entity, const std::string &name, bool value)
  {
    return write_variable(entity, name, BlueprintValue::from_bool(value));
  }

  bool Blueprints::setString(Entity::EntityId entity, const std::string &name, const std::string &value)
  {
    return write_variable(entity, name, BlueprintValue::from_string(value));
  }

  bool Blueprints::setVector(Entity::EntityId entity, const std::string &name, const math::Vec3 &value)
  {
    return write_variable(entity, name, BlueprintValue::from_vector(value));
  }
}
