#include "blueprint_engine_host.hpp"

#include "../audio/audio_engine.hpp"
#include "../components/audio_source_component.hpp"
#include "../components/position_component_3d.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/log.hpp"
#include "../physics/physics_forces.hpp"
#include "../physics/physics_world.hpp"
#include "../runtime/hades_script.hpp"
#include "../runtime/script_runtime.hpp"

namespace hades
{
  void EngineBlueprintHost::bind(
      ComponentManager *componentManager,
      PhysicsWorld *physicsWorld,
      AudioEngine *audioEngine)
  {
    componentManager_ = componentManager;
    physicsWorld_ = physicsWorld;
    audioEngine_ = audioEngine;
  }

  void EngineBlueprintHost::set_script_runtime(ScriptRuntime *scriptRuntime)
  {
    scriptRuntime_ = scriptRuntime;
  }

  void EngineBlueprintHost::set_log_sink(LogSink sink)
  {
    logSink_ = std::move(sink);
  }

  void EngineBlueprintHost::print(
      Entity::EntityId entity,
      const std::string &text,
      BlueprintLogLevel level)
  {
    (void)entity;

    if (logSink_)
    {
      logSink_(level, text);
      return;
    }

    switch (level)
    {
    case BlueprintLogLevel::Warning:
      Log::warn_tagged("blueprint", "%s", text.c_str());
      break;
    case BlueprintLogLevel::Error:
      Log::error_tagged("blueprint", "%s", text.c_str());
      break;
    case BlueprintLogLevel::Info:
    default:
      Log::info_tagged("blueprint", "%s", text.c_str());
      break;
    }
  }

  void EngineBlueprintHost::report_error(Entity::EntityId entity, const std::string &text)
  {
    print(entity, text, BlueprintLogLevel::Error);
  }

  void EngineBlueprintHost::apply_force(Entity::EntityId entity, const math::Vec3 &force)
  {
    if (physicsWorld_ == nullptr || componentManager_ == nullptr)
    {
      return;
    }

    physics::apply_force(*physicsWorld_, *componentManager_, entity, force.x, force.y, force.z);
  }

  void EngineBlueprintHost::apply_impulse(Entity::EntityId entity, const math::Vec3 &impulse)
  {
    if (physicsWorld_ == nullptr || componentManager_ == nullptr)
    {
      return;
    }

    physics::apply_impulse(*physicsWorld_, *componentManager_, entity, impulse.x, impulse.y, impulse.z);
  }

  void EngineBlueprintHost::set_linear_velocity(Entity::EntityId entity, const math::Vec3 &velocity)
  {
    if (physicsWorld_ == nullptr || componentManager_ == nullptr)
    {
      return;
    }

    physics::set_linear_velocity(*physicsWorld_, *componentManager_, entity, velocity.x, velocity.y, velocity.z);
  }

  void EngineBlueprintHost::load_world(const std::string &worldName)
  {
    // Same queue the C++ scripting API uses, so both paths are drained by the
    // host's existing world-switch handling.
    HadesAPI::loadWorld(worldName);
  }

  void EngineBlueprintHost::observe(const std::string &key, const BlueprintValue &value)
  {
    switch (value.type())
    {
    case ValueType::Bool:
      HadesAPI::observe(key, value.as_bool());
      break;
    case ValueType::Int:
      HadesAPI::observe(key, value.as_int());
      break;
    case ValueType::Float:
      HadesAPI::observe(key, value.as_float());
      break;
    default:
      HadesAPI::observe(key, value.as_string());
      break;
    }
  }

  void EngineBlueprintHost::play_audio(Entity::EntityId entity)
  {
    if (audioEngine_ == nullptr || componentManager_ == nullptr ||
        !componentManager_->hasComponent<AudioSourceComponent>(entity))
    {
      return;
    }

    const auto &source = componentManager_->getComponent<AudioSourceComponent>(entity);
    const PositionComponent3D *position = nullptr;
    if (componentManager_->hasComponent<PositionComponent3D>(entity))
    {
      position = &componentManager_->getComponent<PositionComponent3D>(entity);
    }

    audioEngine_->play_source(entity, source, position);
  }

  void EngineBlueprintHost::stop_audio(Entity::EntityId entity)
  {
    if (audioEngine_ == nullptr)
    {
      return;
    }

    audioEngine_->stop_source(entity);
  }

  ScriptValue EngineBlueprintHost::send_script_message(
      Entity::EntityId entity,
      const std::string &name,
      const ScriptValue &value)
  {
    if (scriptRuntime_ == nullptr || name.empty())
    {
      return ScriptValue();
    }

    if (entity == Entity::INVALID)
    {
      return scriptRuntime_->broadcast_message(name, value);
    }

    return scriptRuntime_->send_message(entity, name, value);
  }
}
