#ifndef HADES_ENGINE_BLUEPRINT_BLUEPRINT_ENGINE_HOST_HPP
#define HADES_ENGINE_BLUEPRINT_BLUEPRINT_ENGINE_HOST_HPP

#include <functional>
#include <string>

#include "blueprint_host.hpp"

namespace hades
{
  class AudioEngine;
  class ComponentManager;
  class PhysicsWorld;
  class ScriptRuntime;

  /// The `BlueprintHost` the real game uses.
  ///
  /// Forwards `Print String` to whatever log sink the embedder installs (the
  /// editor's debug console, or `Log::info` in the standalone runtime), physics
  /// nodes to Jolt through `physics::`, audio nodes to the live `AudioEngine`,
  /// and `Observe` / `Load World` to `HadesAPI` so Blueprints reach the same
  /// REST/RL surface that C++ scripts do.
  ///
  /// Every dependency is optional: an unbound host silently drops the calls,
  /// which is what happens in headless runs without audio or physics.
  class EngineBlueprintHost final : public BlueprintHost
  {
  public:
    using LogSink = std::function<void(BlueprintLogLevel, const std::string &)>;

    void bind(ComponentManager *componentManager, PhysicsWorld *physicsWorld, AudioEngine *audioEngine);
    /// The runtime the script nodes reach into. Separate from `bind` because
    /// the editor rebinds it around play mode while the rest stays put.
    void set_script_runtime(ScriptRuntime *scriptRuntime);
    void set_log_sink(LogSink sink);

    void print(Entity::EntityId entity, const std::string &text, BlueprintLogLevel level) override;
    void report_error(Entity::EntityId entity, const std::string &text) override;
    void apply_force(Entity::EntityId entity, const math::Vec3 &force) override;
    void apply_impulse(Entity::EntityId entity, const math::Vec3 &impulse) override;
    void set_linear_velocity(Entity::EntityId entity, const math::Vec3 &velocity) override;
    void load_world(const std::string &worldName) override;
    void observe(const std::string &key, const BlueprintValue &value) override;
    void play_audio(Entity::EntityId entity) override;
    void stop_audio(Entity::EntityId entity) override;
    ScriptValue send_script_message(
        Entity::EntityId entity,
        const std::string &name,
        const ScriptValue &value) override;

  private:
    ComponentManager *componentManager_ = nullptr;
    PhysicsWorld *physicsWorld_ = nullptr;
    AudioEngine *audioEngine_ = nullptr;
    ScriptRuntime *scriptRuntime_ = nullptr;
    LogSink logSink_;
  };
}

#endif
