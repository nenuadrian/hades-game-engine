#ifndef HADES_ENGINE_RUNTIME_HADES_VALUE_HPP
#define HADES_ENGINE_RUNTIME_HADES_VALUE_HPP

// The value type that crosses the script <-> Blueprint boundary.
//
// It deliberately mirrors `BlueprintValue`'s type lattice without being it:
// `blueprint_value.hpp` pulls in <nlohmann/json.hpp> and the whole Blueprint
// graph surface, and every user script that includes `engine/hades.hpp` would
// then pay for that on every compile. Conversion between the two happens on
// the engine side of the boundary, in `script_blueprint_bridge.cpp` and
// `script_blueprint_nodes.cpp`.
//
// The converting constructors are what make payloads readable at a call site:
//
//   hades::Blueprints::sendEvent(ctx.entityId, "Damaged", {25.0f, "fire"});

#include <cstdint>
#include <string>
#include <variant>

#include "../core/ecs/entity.hpp"
#include "../rendering/math3d.hpp"

namespace hades
{
  enum class ScriptValueType : std::uint8_t
  {
    None = 0,
    Bool,
    Int,
    Float,
    String,
    Vector,
    Entity,
  };

  const char *script_value_type_name(ScriptValueType type);

  class ScriptValue
  {
  public:
    /// Alternative order matches ScriptValueType, so `type()` is an index cast.
    using Storage = std::variant<
        std::monostate,
        bool,
        std::int32_t,
        float,
        std::string,
        math::Vec3,
        Entity::EntityId>;

    ScriptValue() = default;

    // Converting constructors, so a payload can be written as a braced list of
    // plain C++ values. `const char *` beats the bool overload by exact match,
    // which is why string literals do not silently become `true`.
    ScriptValue(bool value) : storage_(value) {}
    ScriptValue(std::int32_t value) : storage_(value) {}
    ScriptValue(float value) : storage_(value) {}
    ScriptValue(double value) : storage_(static_cast<float>(value)) {}
    ScriptValue(const char *value) : storage_(std::string(value == nullptr ? "" : value)) {}
    ScriptValue(std::string value) : storage_(std::move(value)) {}
    ScriptValue(const math::Vec3 &value) : storage_(value) {}

    static ScriptValue fromBool(bool value) { return ScriptValue(value); }
    static ScriptValue fromInt(std::int32_t value) { return ScriptValue(value); }
    static ScriptValue fromFloat(float value) { return ScriptValue(value); }
    static ScriptValue fromString(std::string value) { return ScriptValue(std::move(value)); }
    static ScriptValue fromVector(const math::Vec3 &value) { return ScriptValue(value); }

    /// `EntityId` is an unsigned int, so making this a constructor overload
    /// would drag every unsigned literal into the Entity type. Entities go
    /// through the named factory instead.
    static ScriptValue fromEntity(Entity::EntityId value)
    {
      ScriptValue result;
      result.storage_ = value;
      return result;
    }

    ScriptValueType type() const
    {
      return static_cast<ScriptValueType>(storage_.index());
    }

    bool empty() const { return storage_.index() == 0; }

    /// Lenient accessors: each converts from whatever is stored, and never
    /// throws. A value that cannot convert reads back as that type's zero.
    bool asBool() const;
    std::int32_t asInt() const;
    float asFloat() const;
    std::string asString() const;
    math::Vec3 asVector() const;
    Entity::EntityId asEntity() const;

    bool operator==(const ScriptValue &other) const;
    bool operator!=(const ScriptValue &other) const { return !(*this == other); }

    const Storage &storage() const { return storage_; }

  private:
    Storage storage_;
  };
}

#endif
