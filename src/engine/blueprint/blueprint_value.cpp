#include "blueprint_value.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace hades
{
  namespace
  {
    constexpr std::array<const char *, 8> kTypeNames = {
        "exec", "bool", "int", "float", "string", "vector", "entity", "wildcard"};

    std::string format_float(float value)
    {
      char buffer[64];
      std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(value));
      std::string text(buffer);

      // Trim trailing zeros so 1.500 reads as 1.5 and 2.000 as 2.
      const auto dot = text.find('.');
      if (dot != std::string::npos)
      {
        auto last = text.find_last_not_of('0');
        if (last == dot)
        {
          last = dot - 1;
        }
        text.erase(last + 1);
      }

      if (text == "-0")
      {
        text = "0";
      }

      return text;
    }
  }

  const char *value_type_name(ValueType type)
  {
    const auto index = static_cast<std::size_t>(type);
    return index < kTypeNames.size() ? kTypeNames[index] : "unknown";
  }

  bool value_type_from_name(const std::string &name, ValueType &out)
  {
    for (std::size_t i = 0; i < kTypeNames.size(); ++i)
    {
      if (name == kTypeNames[i])
      {
        out = static_cast<ValueType>(i);
        return true;
      }
    }

    return false;
  }

  bool value_type_convertible(ValueType from, ValueType to)
  {
    if (from == to)
    {
      return true;
    }

    // Exec wires never carry data and data wires never carry exec.
    if (from == ValueType::Exec || to == ValueType::Exec)
    {
      return false;
    }

    // Wildcards are resolved by the compiler before this is consulted for a
    // final verdict, but during editing they connect to anything.
    if (from == ValueType::Wildcard || to == ValueType::Wildcard)
    {
      return true;
    }

    // Everything renders as a string.
    if (to == ValueType::String)
    {
      return true;
    }

    switch (from)
    {
    case ValueType::Bool:
      return to == ValueType::Int || to == ValueType::Float;
    case ValueType::Int:
      return to == ValueType::Float || to == ValueType::Bool || to == ValueType::Entity;
    case ValueType::Float:
      return to == ValueType::Int || to == ValueType::Bool;
    case ValueType::Entity:
      return to == ValueType::Int || to == ValueType::Bool;
    case ValueType::String:
    case ValueType::Vector:
    default:
      return false;
    }
  }

  bool value_type_conversion_is_lossy(ValueType from, ValueType to)
  {
    if (from == to || !value_type_convertible(from, to))
    {
      return false;
    }

    if (from == ValueType::Wildcard || to == ValueType::Wildcard)
    {
      return false;
    }

    // Widening a bool or an int into a float, or an int into an entity handle,
    // is exact. Everything else drops precision or structure.
    if (from == ValueType::Bool && (to == ValueType::Int || to == ValueType::Float))
    {
      return false;
    }
    if (from == ValueType::Int && (to == ValueType::Float || to == ValueType::Entity))
    {
      return false;
    }

    return true;
  }

  BlueprintValue BlueprintValue::from_bool(bool value)
  {
    return BlueprintValue(Storage(std::in_place_type<bool>, value));
  }

  BlueprintValue BlueprintValue::from_int(std::int32_t value)
  {
    return BlueprintValue(Storage(std::in_place_type<std::int32_t>, value));
  }

  BlueprintValue BlueprintValue::from_float(float value)
  {
    return BlueprintValue(Storage(std::in_place_type<float>, value));
  }

  BlueprintValue BlueprintValue::from_string(std::string value)
  {
    return BlueprintValue(Storage(std::in_place_type<std::string>, std::move(value)));
  }

  BlueprintValue BlueprintValue::from_vector(const math::Vec3 &value)
  {
    return BlueprintValue(Storage(std::in_place_type<math::Vec3>, value));
  }

  BlueprintValue BlueprintValue::from_entity(Entity::EntityId value)
  {
    return BlueprintValue(Storage(std::in_place_type<Entity::EntityId>, value));
  }

  BlueprintValue BlueprintValue::default_for(ValueType type)
  {
    switch (type)
    {
    case ValueType::Bool:
      return from_bool(false);
    case ValueType::Int:
      return from_int(0);
    case ValueType::Float:
      return from_float(0.0f);
    case ValueType::String:
      return from_string(std::string());
    case ValueType::Vector:
      return from_vector(math::Vec3());
    case ValueType::Entity:
      return from_entity(Entity::INVALID);
    case ValueType::Exec:
    case ValueType::Wildcard:
    default:
      return BlueprintValue();
    }
  }

  ValueType BlueprintValue::type() const
  {
    switch (storage_.index())
    {
    case 1:
      return ValueType::Bool;
    case 2:
      return ValueType::Int;
    case 3:
      return ValueType::Float;
    case 4:
      return ValueType::String;
    case 5:
      return ValueType::Vector;
    case 6:
      return ValueType::Entity;
    case 0:
    default:
      return ValueType::Wildcard;
    }
  }

  bool BlueprintValue::as_bool() const
  {
    switch (storage_.index())
    {
    case 1:
      return std::get<bool>(storage_);
    case 2:
      return std::get<std::int32_t>(storage_) != 0;
    case 3:
      return std::get<float>(storage_) != 0.0f;
    case 4:
    {
      const auto &text = std::get<std::string>(storage_);
      return !text.empty() && text != "0" && text != "false" && text != "False";
    }
    case 5:
      return std::get<math::Vec3>(storage_).lengthSquared() > 0.0f;
    case 6:
      return std::get<Entity::EntityId>(storage_) != Entity::INVALID;
    default:
      return false;
    }
  }

  std::int32_t BlueprintValue::as_int() const
  {
    switch (storage_.index())
    {
    case 1:
      return std::get<bool>(storage_) ? 1 : 0;
    case 2:
      return std::get<std::int32_t>(storage_);
    case 3:
      return static_cast<std::int32_t>(std::get<float>(storage_));
    case 4:
    {
      // Strings parse leniently: "12abc" is 12, anything unparsable is 0.
      try
      {
        return static_cast<std::int32_t>(std::stol(std::get<std::string>(storage_)));
      }
      catch (const std::exception &)
      {
        return 0;
      }
    }
    case 6:
      return static_cast<std::int32_t>(std::get<Entity::EntityId>(storage_));
    default:
      return 0;
    }
  }

  float BlueprintValue::as_float() const
  {
    switch (storage_.index())
    {
    case 1:
      return std::get<bool>(storage_) ? 1.0f : 0.0f;
    case 2:
      return static_cast<float>(std::get<std::int32_t>(storage_));
    case 3:
      return std::get<float>(storage_);
    case 6:
      return static_cast<float>(std::get<Entity::EntityId>(storage_));
    default:
      return 0.0f;
    }
  }

  std::string BlueprintValue::as_string() const
  {
    switch (storage_.index())
    {
    case 1:
      return std::get<bool>(storage_) ? "true" : "false";
    case 2:
      return std::to_string(std::get<std::int32_t>(storage_));
    case 3:
      return format_float(std::get<float>(storage_));
    case 4:
      return std::get<std::string>(storage_);
    case 5:
    {
      const auto &v = std::get<math::Vec3>(storage_);
      return "(" + format_float(v.x) + ", " + format_float(v.y) + ", " + format_float(v.z) + ")";
    }
    case 6:
    {
      const auto id = std::get<Entity::EntityId>(storage_);
      return id == Entity::INVALID ? "None" : ("Entity " + std::to_string(id));
    }
    default:
      return std::string();
    }
  }

  math::Vec3 BlueprintValue::as_vector() const
  {
    if (storage_.index() == 5)
    {
      return std::get<math::Vec3>(storage_);
    }

    // Scalars splat across all three axes, which is what `Vector * float`
    // style graphs expect when someone wires a float straight in.
    const float scalar = as_float();
    return math::Vec3(scalar, scalar, scalar);
  }

  Entity::EntityId BlueprintValue::as_entity() const
  {
    switch (storage_.index())
    {
    case 6:
      return std::get<Entity::EntityId>(storage_);
    case 2:
      return static_cast<Entity::EntityId>(std::get<std::int32_t>(storage_));
    default:
      return Entity::INVALID;
    }
  }

  BlueprintValue BlueprintValue::coerced_to(ValueType target) const
  {
    switch (target)
    {
    case ValueType::Bool:
      return from_bool(as_bool());
    case ValueType::Int:
      return from_int(as_int());
    case ValueType::Float:
      return from_float(as_float());
    case ValueType::String:
      return from_string(as_string());
    case ValueType::Vector:
      return from_vector(as_vector());
    case ValueType::Entity:
      return from_entity(as_entity());
    case ValueType::Exec:
      return BlueprintValue();
    case ValueType::Wildcard:
    default:
      return *this;
    }
  }

  bool BlueprintValue::operator==(const BlueprintValue &other) const
  {
    if (storage_.index() != other.storage_.index())
    {
      return false;
    }

    switch (storage_.index())
    {
    case 0:
      return true;
    case 1:
      return std::get<bool>(storage_) == std::get<bool>(other.storage_);
    case 2:
      return std::get<std::int32_t>(storage_) == std::get<std::int32_t>(other.storage_);
    case 3:
      return std::get<float>(storage_) == std::get<float>(other.storage_);
    case 4:
      return std::get<std::string>(storage_) == std::get<std::string>(other.storage_);
    case 5:
    {
      const auto &a = std::get<math::Vec3>(storage_);
      const auto &b = std::get<math::Vec3>(other.storage_);
      return a.x == b.x && a.y == b.y && a.z == b.z;
    }
    case 6:
      return std::get<Entity::EntityId>(storage_) == std::get<Entity::EntityId>(other.storage_);
    default:
      return false;
    }
  }

  nlohmann::json BlueprintValue::to_json() const
  {
    switch (storage_.index())
    {
    case 1:
      return std::get<bool>(storage_);
    case 2:
      return std::get<std::int32_t>(storage_);
    case 3:
      return std::get<float>(storage_);
    case 4:
      return std::get<std::string>(storage_);
    case 5:
    {
      const auto &v = std::get<math::Vec3>(storage_);
      return nlohmann::json{{"x", v.x}, {"y", v.y}, {"z", v.z}};
    }
    case 6:
      return std::get<Entity::EntityId>(storage_);
    default:
      return nullptr;
    }
  }

  BlueprintValue BlueprintValue::from_json(const nlohmann::json &value, ValueType expected)
  {
    if (value.is_null())
    {
      return default_for(expected);
    }

    switch (expected)
    {
    case ValueType::Bool:
      return from_bool(value.is_boolean() ? value.get<bool>() : false);
    case ValueType::Int:
      return from_int(value.is_number() ? value.get<std::int32_t>() : 0);
    case ValueType::Float:
      return from_float(value.is_number() ? value.get<float>() : 0.0f);
    case ValueType::String:
      return from_string(value.is_string() ? value.get<std::string>() : std::string());
    case ValueType::Vector:
    {
      if (!value.is_object())
      {
        return from_vector(math::Vec3());
      }
      return from_vector(math::Vec3(
          value.value("x", 0.0f),
          value.value("y", 0.0f),
          value.value("z", 0.0f)));
    }
    case ValueType::Entity:
      return from_entity(value.is_number() ? value.get<Entity::EntityId>() : Entity::INVALID);
    case ValueType::Exec:
    case ValueType::Wildcard:
    default:
      return BlueprintValue();
    }
  }

  std::string BlueprintValue::to_display_string() const
  {
    if (storage_.index() == 0)
    {
      return "<none>";
    }

    return as_string();
  }
}
