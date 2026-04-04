#ifndef HADES_ENGINE_CORE_ECS_COMPONENT_REGISTRY_HPP
#define HADES_ENGINE_CORE_ECS_COMPONENT_REGISTRY_HPP

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "entity.hpp"
#include "type_id.hpp"

namespace nlohmann
{
  template <typename, typename>
  class basic_json;
  using json = basic_json<std::map, std::vector, std::string,
                          bool, std::int64_t, std::uint64_t, double,
                          std::allocator, void, void>;
}

namespace hades
{
  class ComponentManager;

  using SerializeFn = std::function<bool(
      Entity::EntityId entity,
      ComponentManager &componentManager,
      nlohmann::json &out)>;

  using DeserializeFn = std::function<bool(
      Entity::EntityId entity,
      ComponentManager &componentManager,
      const nlohmann::json &in,
      const std::unordered_map<Entity::EntityId, Entity::EntityId> &idMap)>;

  struct ComponentRegistration
  {
    ComponentId typeId;
    std::string jsonKey;
    SerializeFn serialize;
    DeserializeFn deserialize;
  };

  class ComponentRegistry
  {
  public:
    static ComponentRegistry &instance();

    template <typename T>
    void registerComponent(
        const std::string &jsonKey,
        SerializeFn serialize,
        DeserializeFn deserialize)
    {
      registrations_.push_back(
          {ComponentTypeId::get<T>(),
           jsonKey,
           std::move(serialize),
           std::move(deserialize)});
    }

    const std::vector<ComponentRegistration> &all() const
    {
      return registrations_;
    }

  private:
    ComponentRegistry() = default;
    std::vector<ComponentRegistration> registrations_;
  };
}

#endif
