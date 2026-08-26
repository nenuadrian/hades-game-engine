#ifndef HADES_ENGINE_BLUEPRINT_BLUEPRINT_HOST_HPP
#define HADES_ENGINE_BLUEPRINT_BLUEPRINT_HOST_HPP

#include <string>

#include "../core/ecs/entity.hpp"
#include "../rendering/math3d.hpp"
#include "../runtime/hades_value.hpp"
#include "blueprint_value.hpp"

namespace hades
{
  enum class BlueprintLogLevel
  {
    Info,
    Warning,
    Error,
  };

  /// Everything a running graph needs that lives outside the ECS.
  ///
  /// The default implementation is inert, which is what unit tests want: a
  /// graph can be executed against nothing but an EntityManager and a
  /// ComponentManager. The editor and the standalone runtime install a real
  /// host that forwards to the debug console, the physics world and HadesAPI.
  class BlueprintHost
  {
  public:
    virtual ~BlueprintHost() = default;

    /// `Print String`. `entity` is the entity whose graph produced the line.
    virtual void print(Entity::EntityId entity, const std::string &text, BlueprintLogLevel level)
    {
      (void)entity;
      (void)text;
      (void)level;
    }

    /// Raised when a graph hits a runtime error (infinite loop guard, call
    /// depth exceeded, ...). The host usually stops play mode.
    virtual void report_error(Entity::EntityId entity, const std::string &text)
    {
      print(entity, text, BlueprintLogLevel::Error);
    }

    virtual void apply_force(Entity::EntityId entity, const math::Vec3 &force)
    {
      (void)entity;
      (void)force;
    }

    virtual void apply_impulse(Entity::EntityId entity, const math::Vec3 &impulse)
    {
      (void)entity;
      (void)impulse;
    }

    virtual void set_linear_velocity(Entity::EntityId entity, const math::Vec3 &velocity)
    {
      (void)entity;
      (void)velocity;
    }

    /// Queue a world switch, mirroring `HadesAPI::loadWorld`.
    virtual void load_world(const std::string &worldName)
    {
      (void)worldName;
    }

    /// Publish an observation for the REST API / RL training loop.
    virtual void observe(const std::string &key, const BlueprintValue &value)
    {
      (void)key;
      (void)value;
    }

    /// Play the audio source already configured on `entity`.
    virtual void play_audio(Entity::EntityId entity)
    {
      (void)entity;
    }

    virtual void stop_audio(Entity::EntityId entity)
    {
      (void)entity;
    }

    /// `Send Script Message` / `Call Script Function`. Delivers `name` and
    /// `value` to `HadesScript::onMessage` on every C++ script attached to
    /// `entity`, and hands back the first non-empty reply. `entity` of
    /// `Entity::INVALID` means broadcast to every scripted entity.
    ///
    /// Inert by default, which is what the unit tests want: a graph can run
    /// against nothing but an EntityManager and a ComponentManager, and the
    /// script nodes then behave as if no script handled the message.
    virtual ScriptValue send_script_message(
        Entity::EntityId entity,
        const std::string &name,
        const ScriptValue &value)
    {
      (void)entity;
      (void)name;
      (void)value;
      return ScriptValue();
    }
  };

  /// Process-wide inert host used when nobody installed a real one.
  BlueprintHost &null_blueprint_host();
}

#endif
