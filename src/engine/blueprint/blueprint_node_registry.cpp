#include "blueprint_node_registry.hpp"

#include <algorithm>

namespace hades
{
  int BlueprintNodeSignature::find_exec_input(const std::string &name) const
  {
    for (std::size_t i = 0; i < execInputs.size(); ++i)
    {
      if (execInputs[i] == name)
      {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  int BlueprintNodeSignature::find_exec_output(const std::string &name) const
  {
    for (std::size_t i = 0; i < execOutputs.size(); ++i)
    {
      if (execOutputs[i] == name)
      {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  int BlueprintNodeSignature::find_data_input(const std::string &name) const
  {
    for (std::size_t i = 0; i < dataInputs.size(); ++i)
    {
      if (dataInputs[i].name == name)
      {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  int BlueprintNodeSignature::find_data_output(const std::string &name) const
  {
    for (std::size_t i = 0; i < dataOutputs.size(); ++i)
    {
      if (dataOutputs[i].name == name)
      {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  BlueprintNodeRegistry &BlueprintNodeRegistry::instance()
  {
    static BlueprintNodeRegistry registry;
    return registry;
  }

  void BlueprintNodeRegistry::register_type(BlueprintNodeType type)
  {
    if (type.name.empty() || byName_.count(type.name) != 0)
    {
      return;
    }

    if (type.displayName.empty())
    {
      type.displayName = type.name;
    }

    auto owned = std::make_unique<BlueprintNodeType>(std::move(type));
    const BlueprintNodeType *raw = owned.get();
    byName_.emplace(raw->name, raw);
    ordered_.push_back(raw);
    storage_.push_back(std::move(owned));
  }

  const BlueprintNodeType *BlueprintNodeRegistry::find(const std::string &name) const
  {
    const auto it = byName_.find(name);
    return it == byName_.end() ? nullptr : it->second;
  }

  std::vector<std::string> BlueprintNodeRegistry::categories() const
  {
    std::vector<std::string> result;
    for (const auto *type : ordered_)
    {
      if (type->hidden)
      {
        continue;
      }
      if (std::find(result.begin(), result.end(), type->category) == result.end())
      {
        result.push_back(type->category);
      }
    }
    return result;
  }

  bool resolve_blueprint_node_signature(
      const BlueprintSignatureContext &context,
      const BlueprintNode &node,
      BlueprintNodeSignature &out)
  {
    register_builtin_blueprint_nodes();

    const BlueprintNodeType *type = BlueprintNodeRegistry::instance().find(node.type);
    if (type == nullptr)
    {
      out = BlueprintNodeSignature();
      return false;
    }

    if (type->signatureFn != nullptr)
    {
      out = BlueprintNodeSignature();
      type->signatureFn(context, node, out);
      return true;
    }

    out = type->signature;
    return true;
  }

  BlueprintValue blueprint_pin_literal(
      const BlueprintNode &node,
      const BlueprintPinSpec &pin)
  {
    const auto it = node.pinDefaults.find(pin.name);
    if (it != node.pinDefaults.end())
    {
      return it->second.coerced_to(pin.type);
    }

    if (!pin.defaultValue.empty())
    {
      return pin.defaultValue.coerced_to(pin.type);
    }

    return BlueprintValue::default_for(pin.type);
  }
}
