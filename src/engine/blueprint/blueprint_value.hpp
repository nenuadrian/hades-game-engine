#ifndef HADES_ENGINE_BLUEPRINT_BLUEPRINT_VALUE_HPP
#define HADES_ENGINE_BLUEPRINT_BLUEPRINT_VALUE_HPP

#include <cstdint>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>

#include "../core/ecs/entity.hpp"
#include "../rendering/math3d.hpp"

namespace hades
{
  /// The type lattice a Blueprint pin can carry.
  ///
  /// `Exec` is the white execution wire — it never carries a value, it only
  /// orders node execution. `Wildcard` is used by node signatures that adapt
  /// to whatever they are wired to (variable get/set, `Select`, ...); it is
  /// resolved to a concrete type at compile time and must never survive into
  /// a compiled graph.
  enum class ValueType : std::uint8_t
  {
    Exec = 0,
    Bool,
    Int,
    Float,
    String,
    Vector,
    Entity,
    Wildcard,
  };

  const char *value_type_name(ValueType type);
  bool value_type_from_name(const std::string &name, ValueType &out);

  /// True when a value of `from` may flow into a pin of type `to`.
  /// Identity always converts; the rest of the table mirrors Unreal's
  /// autocast set (numeric widening/narrowing plus anything-to-string).
  bool value_type_convertible(ValueType from, ValueType to);

  /// True when the conversion loses information (float -> int, vector ->
  /// string, ...). The compiler surfaces these as warnings, not errors.
  bool value_type_conversion_is_lossy(ValueType from, ValueType to);

  /// A dynamically typed Blueprint value.
  ///
  /// Deliberately a closed set: the graph editor has to be able to render an
  /// inline literal editor for every type, and the serializer has to be able
  /// to round-trip every type through JSON.
  class BlueprintValue
  {
  public:
    using Storage = std::variant<
        std::monostate,
        bool,
        std::int32_t,
        float,
        std::string,
        math::Vec3,
        Entity::EntityId>;

    BlueprintValue() = default;

    static BlueprintValue from_bool(bool value);
    static BlueprintValue from_int(std::int32_t value);
    static BlueprintValue from_float(float value);
    static BlueprintValue from_string(std::string value);
    static BlueprintValue from_vector(const math::Vec3 &value);
    static BlueprintValue from_entity(Entity::EntityId value);

    /// A zero-initialised value of the given type. `Exec` and `Wildcard`
    /// produce an empty value.
    static BlueprintValue default_for(ValueType type);

    ValueType type() const;
    bool empty() const { return storage_.index() == 0; }

    /// Lenient accessors: each one converts from whatever is stored using the
    /// same rules as `coerced_to`. They never throw.
    bool as_bool() const;
    std::int32_t as_int() const;
    float as_float() const;
    std::string as_string() const;
    math::Vec3 as_vector() const;
    Entity::EntityId as_entity() const;

    /// Convert to `target`. Returns an empty value for `Exec`; returns *this
    /// unchanged for `Wildcard`.
    BlueprintValue coerced_to(ValueType target) const;

    bool operator==(const BlueprintValue &other) const;
    bool operator!=(const BlueprintValue &other) const { return !(*this == other); }

    const Storage &storage() const { return storage_; }

    nlohmann::json to_json() const;
    static BlueprintValue from_json(const nlohmann::json &value, ValueType expected);

    /// Compact human-readable form used by the editor, tooltips and
    /// `Print String`. Floats print with up to 3 decimals, trailing zeros
    /// trimmed, so `1.5` reads as "1.5" rather than "1.500000".
    std::string to_display_string() const;

  private:
    explicit BlueprintValue(Storage storage) : storage_(std::move(storage)) {}

    Storage storage_;
  };
}

#endif
