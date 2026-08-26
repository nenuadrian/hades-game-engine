#include "hades_value.hpp"

#include <array>
#include <cstdio>
#include <exception>
#include <limits>

namespace hades
{
  namespace
  {
    constexpr std::array<const char *, 7> kTypeNames = {
        "none", "bool", "int", "float", "string", "vector", "entity"};

    // Same formatting as BlueprintValue::as_string, so a float reads the same
    // whichever side of the bridge prints it.
    std::string format_float(float value)
    {
      char buffer[64];
      std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(value));
      std::string text(buffer);

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

  const char *script_value_type_name(ScriptValueType type)
  {
    const auto index = static_cast<std::size_t>(type);
    return index < kTypeNames.size() ? kTypeNames[index] : "unknown";
  }

  bool ScriptValue::asBool() const
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

  std::int32_t ScriptValue::asInt() const
  {
    switch (storage_.index())
    {
    case 1:
      return std::get<bool>(storage_) ? 1 : 0;
    case 2:
      return std::get<std::int32_t>(storage_);
    case 3:
    {
      // Casting a float outside int32's range is undefined, so saturate.
      const float value = std::get<float>(storage_);
      constexpr float kMin = -2147483648.0f;
      constexpr float kMax = 2147483520.0f; // largest float below INT32_MAX
      if (!(value > kMin))
      {
        return (std::numeric_limits<std::int32_t>::min)();
      }
      if (value > kMax)
      {
        return (std::numeric_limits<std::int32_t>::max)();
      }
      return static_cast<std::int32_t>(value);
    }
    case 4:
      try
      {
        return static_cast<std::int32_t>(std::stol(std::get<std::string>(storage_)));
      }
      catch (const std::exception &)
      {
        return 0;
      }
    case 6:
      return static_cast<std::int32_t>(std::get<Entity::EntityId>(storage_));
    default:
      return 0;
    }
  }

  float ScriptValue::asFloat() const
  {
    switch (storage_.index())
    {
    case 1:
      return std::get<bool>(storage_) ? 1.0f : 0.0f;
    case 2:
      return static_cast<float>(std::get<std::int32_t>(storage_));
    case 3:
      return std::get<float>(storage_);
    case 4:
      try
      {
        return std::stof(std::get<std::string>(storage_));
      }
      catch (const std::exception &)
      {
        return 0.0f;
      }
    case 6:
      return static_cast<float>(std::get<Entity::EntityId>(storage_));
    default:
      return 0.0f;
    }
  }

  std::string ScriptValue::asString() const
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

  math::Vec3 ScriptValue::asVector() const
  {
    if (storage_.index() == 5)
    {
      return std::get<math::Vec3>(storage_);
    }

    // Scalars splat across all three axes, matching BlueprintValue.
    const float scalar = asFloat();
    return math::Vec3(scalar, scalar, scalar);
  }

  Entity::EntityId ScriptValue::asEntity() const
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

  bool ScriptValue::operator==(const ScriptValue &other) const
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
}
