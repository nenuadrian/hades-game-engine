#ifndef HADES_ENGINE_BLUEPRINT_SCRIPT_BLUEPRINT_HPP
#define HADES_ENGINE_BLUEPRINT_SCRIPT_BLUEPRINT_HPP

// Scripting-friendly Blueprint facade — the script -> Blueprint direction of
// the bridge. The Blueprint -> script direction is the "Scripts" node category
// (`script.send` / `script.call`), which lands on `HadesScript::onMessage`.
//
// Use this from HadesScript subclasses to drive graphs without touching
// BlueprintComponent or BlueprintRuntime:
//
//   void onUpdate(ScriptContext &ctx, float dt) override
//   {
//     if (health_ <= 0.0f)
//     {
//       hades::Blueprints::sendEvent(ctx.entityId, "Died", {killerId_});
//     }
//     hades::Blueprints::setFloat(ctx.entityId, "health", health_);
//   }
//
// Every call is a no-op (or returns a default) when no Blueprint runtime is
// running, or when the entity has no Blueprint, so scripts do not have to
// guard.
//
// Deliberately mirrors `hades::Animation`: static, entity-first, and safe to
// call from `onStart` before the runtime has ever looked at the entity.
//
// This header stays free of `blueprint_value.hpp` on purpose — that one pulls
// in <nlohmann/json.hpp>, and every user script compile would pay for it.
// Values cross as `ScriptValue`; the conversion happens engine-side.

#include <string>
#include <vector>

#include "../core/ecs/entity.hpp"
#include "../rendering/math3d.hpp"
#include "../runtime/hades_value.hpp"

namespace hades
{
  class Blueprints
  {
  public:
    // ---- Queries ---------------------------------------------------------

    /// True while a Blueprint runtime is started. False in the editor outside
    /// play mode, and in headless tests that never started one.
    static bool isRunning();

    /// True when `entity` has at least one running Blueprint instance.
    static bool has(Entity::EntityId entity);

    /// Running Blueprint instances on `entity` — one per enabled attachment.
    static int count(Entity::EntityId entity);

    // ---- Events ----------------------------------------------------------

    /// Fire the Custom Event named `eventName` on every Blueprint attached to
    /// `entity`. `payload` fills the event node's output pins positionally;
    /// extra values are dropped and missing ones keep their previous value.
    ///
    /// Delivery is queued, not immediate: the runtime drains the queue at the
    /// top of its next `update`. A call from `onUpdate` therefore lands in the
    /// same frame (scripts update before Blueprints), and a call made from
    /// inside `onMessage` — that is, from within a running graph — lands
    /// without re-entering the VM.
    static void sendEvent(Entity::EntityId entity, const std::string &eventName,
                          const std::vector<ScriptValue> &payload = {});

    /// Same, but every Blueprint instance in the world hears it.
    static void broadcastEvent(const std::string &eventName,
                               const std::vector<ScriptValue> &payload = {});

    // ---- Variables -------------------------------------------------------
    //
    // Reads and writes are immediate — they touch the instance's variable
    // slots directly. A name that no Blueprint on the entity declares is a
    // no-op for the setters and a default for the getters.
    //
    // When an entity carries several Blueprints that each declare `name`, the
    // getters read the first one and the setters write all of them.

    static bool hasVariable(Entity::EntityId entity, const std::string &name);
    static ScriptValue getVariable(Entity::EntityId entity, const std::string &name);
    /// Returns false when no Blueprint on `entity` declares `name`.
    static bool setVariable(Entity::EntityId entity, const std::string &name, const ScriptValue &value);

    static float getFloat(Entity::EntityId entity, const std::string &name);
    static int getInt(Entity::EntityId entity, const std::string &name);
    static bool getBool(Entity::EntityId entity, const std::string &name);
    static std::string getString(Entity::EntityId entity, const std::string &name);
    static math::Vec3 getVector(Entity::EntityId entity, const std::string &name);

    static bool setFloat(Entity::EntityId entity, const std::string &name, float value);
    static bool setInt(Entity::EntityId entity, const std::string &name, int value);
    static bool setBool(Entity::EntityId entity, const std::string &name, bool value);
    static bool setString(Entity::EntityId entity, const std::string &name, const std::string &value);
    static bool setVector(Entity::EntityId entity, const std::string &name, const math::Vec3 &value);
  };
}

#endif
