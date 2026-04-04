#ifndef HADES_ENGINE_PHYSICS_WORLD_HPP
#define HADES_ENGINE_PHYSICS_WORLD_HPP

#include <cstdint>
#include <memory>
#include <vector>

namespace JPH
{
  class BodyInterface;
}

namespace hades
{
  struct ContactEvent
  {
    std::uint32_t bodyIdA;
    std::uint32_t bodyIdB;
    bool began; ///< true = contact began, false = contact ended
  };

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

    /// Drain buffered contact events (thread-safe). Returns events and clears the buffer.
    std::vector<ContactEvent> drain_contacts();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };
}

#endif
