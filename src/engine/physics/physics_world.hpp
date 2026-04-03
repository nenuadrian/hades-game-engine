#ifndef HADES_ENGINE_PHYSICS_WORLD_HPP
#define HADES_ENGINE_PHYSICS_WORLD_HPP

#include <memory>

namespace JPH
{
  class BodyInterface;
}

namespace hades
{
  class PhysicsWorld
  {
  public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld &) = delete;
    PhysicsWorld &operator=(const PhysicsWorld &) = delete;

    bool init();
    void shutdown();
    bool is_initialized() const;

    void step(float fixedDeltaTime, int collisionSteps = 1);

    JPH::BodyInterface &body_interface();
    void set_gravity(float x, float y, float z);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };
}

#endif
