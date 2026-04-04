#ifndef HADES_ENGINE_SYSTEMS_PHYSICS_SYSTEM_HPP
#define HADES_ENGINE_SYSTEMS_PHYSICS_SYSTEM_HPP

#include <cstdint>
#include <optional>
#include <unordered_map>

#include "../core/ecs/entity.hpp"
#include "../core/ecs/system.hpp"
#include "../core/ecs/system_context.hpp"

namespace hades
{
  class PhysicsWorld;

  class PhysicsSystem : public System
  {
  public:
    void setPhysicsWorld(PhysicsWorld *physicsWorld);
    void set_active_world(std::optional<Entity::EntityId> activeWorld);
    void update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager) override;
    void update(float deltaTime, SystemContext &context) override;
    void clear_bodies();

  private:
    void ensure_bodies(ComponentManager &componentManager, EntityManager &entityManager);
    void remove_stale_bodies(ComponentManager &componentManager, EntityManager &entityManager);
    void sync_transforms(ComponentManager &componentManager);
    void create_body_for_entity(Entity::EntityId entity, ComponentManager &componentManager);
    void destroy_body_for_entity(Entity::EntityId entity, ComponentManager &componentManager);

    PhysicsWorld *physicsWorld_ = nullptr;
    std::optional<Entity::EntityId> activeWorld_;
    std::unordered_map<Entity::EntityId, std::uint32_t> entityToBody_;
    std::unordered_map<std::uint32_t, Entity::EntityId> bodyToEntity_;
    float timeAccumulator_ = 0.0f;

    static constexpr float FIXED_TIMESTEP = 1.0f / 60.0f;
  };
}

#endif
