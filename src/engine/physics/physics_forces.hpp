#ifndef HADES_ENGINE_PHYSICS_FORCES_HPP
#define HADES_ENGINE_PHYSICS_FORCES_HPP

#include "../core/ecs/entity.hpp"

namespace hades
{
  class ComponentManager;
  class PhysicsWorld;

  namespace physics
  {
    void apply_force(PhysicsWorld &world, ComponentManager &cm, Entity::EntityId entity, float fx, float fy, float fz);
    void apply_impulse(PhysicsWorld &world, ComponentManager &cm, Entity::EntityId entity, float ix, float iy, float iz);
    void set_linear_velocity(PhysicsWorld &world, ComponentManager &cm, Entity::EntityId entity, float vx, float vy, float vz);
  }
}

#endif
