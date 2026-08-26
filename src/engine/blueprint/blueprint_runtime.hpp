#ifndef HADES_ENGINE_BLUEPRINT_BLUEPRINT_RUNTIME_HPP
#define HADES_ENGINE_BLUEPRINT_BLUEPRINT_RUNTIME_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../core/ecs/entity.hpp"
#include "blueprint_asset.hpp"
#include "blueprint_vm.hpp"

namespace hades
{
  class ComponentManager;
  class EntityManager;
  class EventBus;
  class BlueprintHost;

  /// Drives every Blueprint attached to entities in the running world.
  ///
  /// The shape deliberately mirrors `ScriptRuntime`, so the editor and the
  /// standalone runtime can drive both from the same places in their frame
  /// loops: `start` at play, `update` per frame, input forwarding, `stop`.
  class BlueprintRuntime
  {
  public:
    struct InstanceView
    {
      Entity::EntityId entity = Entity::INVALID;
      std::string entityName;
      std::string assetPath;
      const BlueprintInstance *instance = nullptr;
    };

    BlueprintRuntime();
    ~BlueprintRuntime();

    BlueprintRuntime(const BlueprintRuntime &) = delete;
    BlueprintRuntime &operator=(const BlueprintRuntime &) = delete;

    /// Install the services graphs reach for (printing, physics, audio,
    /// observations). Not owned; must outlive the runtime. Passing nullptr
    /// restores the inert default host.
    void set_host(BlueprintHost *host);

    /// Deterministic seeding for the random nodes. Tests rely on this.
    void set_random_seed(std::uint32_t seed);

    /// Subscribe to physics contacts so `Event Collision Begin/End` fire.
    /// Safe to call repeatedly with the same bus: it only subscribes once.
    /// Call `forget_event_bus` if the bus's subscriptions are ever cleared.
    void attach_event_bus(EventBus &eventBus);
    void forget_event_bus();

    /// Load, compile and instantiate every Blueprint in the world, then fire
    /// `Event BeginPlay`. Returns false (and fills `errorMessage`) if any
    /// referenced asset is missing or fails to compile.
    bool start(
        ComponentManager &componentManager,
        EntityManager &entityManager,
        const std::filesystem::path &workspaceRoot,
        std::optional<Entity::EntityId> worldRoot = std::nullopt,
        std::string *errorMessage = nullptr);

    void update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager);
    void stop();

    bool is_running() const;
    bool faulted() const;
    const std::string &last_error() const;

    void on_key_down(int keyCode);
    void on_key_up(int keyCode);
    void on_mouse_down(int button, float screenX, float screenY);
    void on_mouse_up(int button, float screenX, float screenY);
    void on_mouse_move(float screenX, float screenY);
    void on_collision_begin(Entity::EntityId a, Entity::EntityId b);
    void on_collision_end(Entity::EntityId a, Entity::EntityId b);

    /// Fire a Custom Event by name on every running instance.
    void send_custom_event(const std::string &eventName,
                           const std::vector<BlueprintValue> &payload = {});

    /// Fire a Custom Event on the Blueprints attached to one entity only.
    void send_custom_event_to(Entity::EntityId entity, const std::string &eventName,
                              const std::vector<BlueprintValue> &payload = {});

    // -----------------------------------------------------------------------
    // Variable access, for the `hades::Blueprints` script facade
    // -----------------------------------------------------------------------

    /// Value of graph variable `name` on the first Blueprint attached to
    /// `entity` that declares it. `found` reports whether any did.
    BlueprintValue get_variable(Entity::EntityId entity, const std::string &name,
                                bool *found = nullptr) const;

    /// Write `value` into every Blueprint on `entity` that declares `name`,
    /// coerced to the declared type. Returns how many were written.
    int set_variable(Entity::EntityId entity, const std::string &name, const BlueprintValue &value);

    /// Live instances, for the editor's debug overlay.
    std::vector<InstanceView> instances() const;
    int instance_count() const;
    /// Running instances attached to one entity — one per enabled attachment.
    int instance_count_for(Entity::EntityId entity) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };
}

#endif
