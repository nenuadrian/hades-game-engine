#include "physics_world.hpp"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <thread>

#include "../core/log.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/ContactListener.h>

#include "physics_layers.hpp"

namespace
{
  static void jolt_trace(const char *fmt, ...)
  {
    std::va_list args;
    va_start(args, fmt);
    char buf[1024];
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    hades::Log::info_tagged("jolt", "%s", buf);
  }

#ifdef JPH_ENABLE_ASSERTS
  static bool jolt_assert_failed(const char *expression, const char *message, const char *file, JPH::uint line)
  {
    hades::Log::error_tagged("jolt", "Assertion failed: %s:%u: (%s) %s", file, line, expression, message ? message : "");
    return true; // trigger breakpoint
  }
#endif
}

namespace hades
{
  class HadesContactListener : public JPH::ContactListener
  {
  public:
    JPH::ValidateResult OnContactValidate(
        const JPH::Body & /*body1*/,
        const JPH::Body & /*body2*/,
        JPH::RVec3Arg /*baseOffset*/,
        const JPH::CollideShapeResult & /*collisionResult*/) override
    {
      return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void OnContactAdded(
        const JPH::Body &body1,
        const JPH::Body &body2,
        const JPH::ContactManifold & /*manifold*/,
        JPH::ContactSettings & /*settings*/) override
    {
      std::lock_guard<std::mutex> lock(mutex_);
      contacts_.push_back({body1.GetID().GetIndexAndSequenceNumber(),
                           body2.GetID().GetIndexAndSequenceNumber(),
                           true});
    }

    void OnContactRemoved(const JPH::SubShapeIDPair &subShapePair) override
    {
      std::lock_guard<std::mutex> lock(mutex_);
      contacts_.push_back({subShapePair.GetBody1ID().GetIndexAndSequenceNumber(),
                           subShapePair.GetBody2ID().GetIndexAndSequenceNumber(),
                           false});
    }

    std::vector<ContactEvent> drain()
    {
      std::lock_guard<std::mutex> lock(mutex_);
      std::vector<ContactEvent> result;
      result.swap(contacts_);
      return result;
    }

  private:
    std::mutex mutex_;
    std::vector<ContactEvent> contacts_;
  };

  struct PhysicsWorld::Impl
  {
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem;
    HadesContactListener contactListener;

    physics::BroadPhaseLayerInterfaceImpl broadPhaseLayerInterface;
    physics::ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseFilter;
    physics::ObjectLayerPairFilterImpl objectLayerPairFilter;

    bool initialized = false;
  };

  PhysicsWorld::PhysicsWorld() : impl_(std::make_unique<Impl>()) {}

  PhysicsWorld::~PhysicsWorld()
  {
    shutdown();
  }

  bool PhysicsWorld::init()
  {
    if (impl_->initialized)
    {
      return true;
    }

    JPH::RegisterDefaultAllocator();
    JPH::Trace = jolt_trace;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = jolt_assert_failed;)

    if (JPH::Factory::sInstance == nullptr)
    {
      JPH::Factory::sInstance = new JPH::Factory();
    }
    JPH::RegisterTypes();

    impl_->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(32 * 1024 * 1024);

    const unsigned int numThreads =
        std::max(1u, std::thread::hardware_concurrency() - 1);
    impl_->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(numThreads));

    constexpr JPH::uint maxBodies = 65536;
    constexpr JPH::uint numBodyMutexes = 0; // default
    constexpr JPH::uint maxBodyPairs = 65536;
    constexpr JPH::uint maxContactConstraints = 16384;

    impl_->physicsSystem = std::make_unique<JPH::PhysicsSystem>();
    impl_->physicsSystem->Init(
        maxBodies,
        numBodyMutexes,
        maxBodyPairs,
        maxContactConstraints,
        impl_->broadPhaseLayerInterface,
        impl_->objectVsBroadPhaseFilter,
        impl_->objectLayerPairFilter);

    impl_->physicsSystem->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
    impl_->physicsSystem->SetContactListener(&impl_->contactListener);

    impl_->initialized = true;
    hades::Log::info_tagged("jolt", "Jolt Physics initialized (%u threads)", numThreads);
    return true;
  }

  void PhysicsWorld::shutdown()
  {
    if (!impl_->initialized)
    {
      return;
    }

    impl_->physicsSystem.reset();
    impl_->jobSystem.reset();
    impl_->tempAllocator.reset();

    JPH::UnregisterTypes();
    if (JPH::Factory::sInstance != nullptr)
    {
      delete JPH::Factory::sInstance;
      JPH::Factory::sInstance = nullptr;
    }

    impl_->initialized = false;
  }

  bool PhysicsWorld::is_initialized() const
  {
    return impl_->initialized;
  }

  void PhysicsWorld::step(float fixedDeltaTime, int collisionSteps)
  {
    if (!impl_->initialized)
    {
      return;
    }
    impl_->physicsSystem->Update(
        fixedDeltaTime,
        collisionSteps,
        impl_->tempAllocator.get(),
        impl_->jobSystem.get());
  }

  JPH::BodyInterface &PhysicsWorld::body_interface()
  {
    return impl_->physicsSystem->GetBodyInterface();
  }

  void PhysicsWorld::set_gravity(float x, float y, float z)
  {
    if (impl_->initialized)
    {
      impl_->physicsSystem->SetGravity(JPH::Vec3(x, y, z));
    }
  }

  std::vector<ContactEvent> PhysicsWorld::drain_contacts()
  {
    if (!impl_->initialized)
    {
      return {};
    }
    return impl_->contactListener.drain();
  }
}
