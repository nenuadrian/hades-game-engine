#include "blueprint_runtime.hpp"

#include <algorithm>
#include <utility>

#include "../components/blueprint_component.hpp"
#include "../components/name_component.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"
#include "../core/ecs/world_utils.hpp"
#include "../core/events/event_bus.hpp"
#include "../core/events/events.hpp"
#include "../core/log.hpp"
#include "blueprint_host.hpp"
#include "script_blueprint_bridge.hpp"

namespace hades
{
  struct BlueprintRuntime::Impl
  {
    struct Entry
    {
      Entity::EntityId entity = Entity::INVALID;
      std::string entityName;
      std::string assetPath;
      BlueprintInstance instance;
      /// Latched once the owning entity disappears. EntityManager recycles ids,
      /// so without this a brand-new entity reusing the id would inherit this
      /// instance's variables and node state.
      bool retired = false;
    };

    BlueprintAssetCache cache;
    std::vector<std::unique_ptr<Entry>> entries;
    BlueprintHost *host = nullptr;
    EventBus *subscribedBus = nullptr;
    std::unique_ptr<BlueprintVM> vm;
    std::uint32_t randomSeed = 0x9E3779B9u;

    // Input and collision callbacks arrive without managers, so the runtime
    // holds on to the pair it was started with.
    ComponentManager *componentManager = nullptr;
    EntityManager *entityManager = nullptr;

    bool running = false;
    bool faulted = false;
    std::string lastError;

    BlueprintHost &effective_host()
    {
      return host != nullptr ? *host : null_blueprint_host();
    }

    /// Instances whose entity has been destroyed (or had its Blueprint
    /// component stripped) stop receiving events.
    bool entry_is_live(Entry &entry, ComponentManager &componentManager) const
    {
      if (entry.retired || entry.instance.faulted)
      {
        return false;
      }

      if (!componentManager.hasComponent<BlueprintComponent>(entry.entity))
      {
        entry.retired = true;
        return false;
      }

      return true;
    }

    void broadcast(
        ComponentManager &componentManager,
        const std::string &eventName,
        const std::vector<BlueprintValue> &payload)
    {
      if (!running || vm == nullptr)
      {
        return;
      }

      for (auto &entry : entries)
      {
        if (!entry_is_live(*entry, componentManager))
        {
          continue;
        }

        vm->dispatch(entry->instance, eventName, payload);
        note_fault(*entry);
      }
    }

    void dispatch_to(
        ComponentManager &componentManager,
        Entity::EntityId entity,
        const std::string &eventName,
        const std::vector<BlueprintValue> &payload)
    {
      if (!running || vm == nullptr || entity == Entity::INVALID)
      {
        return;
      }

      for (auto &entry : entries)
      {
        if (entry->entity != entity || !entry_is_live(*entry, componentManager))
        {
          continue;
        }

        vm->dispatch(entry->instance, eventName, payload);
        note_fault(*entry);
      }
    }

    /// Deliver everything `hades::Blueprints::sendEvent` queued since the last
    /// drain. A handler may itself call a script that queues more, so this
    /// loops — bounded, because two graphs that answer each other would
    /// otherwise spin the frame forever. Whatever is left over waits for the
    /// next update rather than being dropped.
    static constexpr int kMaxDrainRounds = 8;

    void drain_script_events(ComponentManager &componentManager)
    {
      if (!running || vm == nullptr)
      {
        return;
      }

      for (int round = 0; round < kMaxDrainRounds; ++round)
      {
        const auto queued = drain_pending_blueprint_events();
        if (queued.empty())
        {
          return;
        }

        for (const auto &event : queued)
        {
          const std::string eventName = "custom:" + event.eventName;
          if (event.entity == Entity::INVALID)
          {
            broadcast(componentManager, eventName, event.payload);
          }
          else
          {
            dispatch_to(componentManager, event.entity, eventName, event.payload);
          }
        }
      }
    }

    /// Slot `name` occupies in `entry`, or -1 when that Blueprint does not
    /// declare it. Retired instances never match: their entity is gone, and
    /// the id may already belong to something else.
    static int variable_slot(const Entry &entry, const std::string &name)
    {
      if (entry.retired || entry.instance.compiled == nullptr)
      {
        return -1;
      }

      const int index = entry.instance.compiled->blueprint.variable_index(name);
      if (index < 0 || static_cast<std::size_t>(index) >= entry.instance.variables.size())
      {
        return -1;
      }

      return index;
    }

    void note_fault(const Entry &entry)
    {
      if (!entry.instance.faulted || faulted)
      {
        return;
      }

      faulted = true;
      lastError = entry.entityName + " / " + entry.assetPath + ": " + entry.instance.error;
    }

    /// Drop every instance and reset the error state. Deliberately does not
    /// touch the script bridge: `start` reuses this to clear the previous
    /// session without discarding events a script queued for the new one.
    void teardown()
    {
      entries.clear();
      cache.clear();
      vm.reset();
      running = false;
      faulted = false;
      lastError.clear();

      componentManager = nullptr;
      entityManager = nullptr;
    }
  };

  BlueprintRuntime::BlueprintRuntime() : impl_(std::make_unique<Impl>())
  {
  }

  BlueprintRuntime::~BlueprintRuntime()
  {
    // Not `= default`: the script facade holds a raw pointer to whichever
    // runtime started last, and a runtime destroyed without an explicit stop
    // would leave it dangling.
    stop();
  }

  void BlueprintRuntime::set_host(BlueprintHost *host)
  {
    impl_->host = host;
    if (impl_->vm != nullptr)
    {
      // Swap it in place: destroying the VM here would silently stop every
      // running instance, because update() bails out when there is no VM.
      impl_->vm->set_host(impl_->effective_host());
    }
  }

  void BlueprintRuntime::set_random_seed(std::uint32_t seed)
  {
    impl_->randomSeed = seed;
    if (impl_->vm != nullptr)
    {
      impl_->vm->set_random_seed(seed);
    }
  }

  void BlueprintRuntime::attach_event_bus(EventBus &eventBus)
  {
    if (impl_->subscribedBus == &eventBus)
    {
      return;
    }

    impl_->subscribedBus = &eventBus;

    eventBus.subscribe<CollisionBeginEvent>(
        [this](const CollisionBeginEvent &event)
        {
          on_collision_begin(event.entityA, event.entityB);
        });

    eventBus.subscribe<CollisionEndEvent>(
        [this](const CollisionEndEvent &event)
        {
          on_collision_end(event.entityA, event.entityB);
        });
  }

  void BlueprintRuntime::forget_event_bus()
  {
    impl_->subscribedBus = nullptr;
  }

  bool BlueprintRuntime::start(
      ComponentManager &componentManager,
      EntityManager &entityManager,
      const std::filesystem::path &workspaceRoot,
      std::optional<Entity::EntityId> worldRoot,
      std::string *errorMessage)
  {
    // Not the full `stop()`: scripts start before Blueprints do in both frame
    // loops, so an `onStart` that called `Blueprints::sendEvent` has already
    // queued events meant for the instances about to be built. Tearing the
    // previous session down must not throw those away.
    if (script_blueprint_runtime() == this)
    {
      register_script_blueprint_runtime(nullptr);
    }
    impl_->teardown();

    impl_->componentManager = &componentManager;
    impl_->entityManager = &entityManager;

    impl_->vm = std::make_unique<BlueprintVM>(componentManager, entityManager, impl_->effective_host());
    impl_->vm->set_random_seed(impl_->randomSeed);

    for (Entity::EntityId entity : entityManager.getAllEntities())
    {
      if (worldRoot.has_value() && !entity_belongs_to_world(entity, *worldRoot, componentManager))
      {
        continue;
      }

      if (!componentManager.hasComponent<BlueprintComponent>(entity))
      {
        continue;
      }

      const auto &component = componentManager.getComponent<BlueprintComponent>(entity);
      std::string entityName = "Entity " + std::to_string(entity);
      if (componentManager.hasComponent<NameComponent>(entity))
      {
        entityName = componentManager.getComponent<NameComponent>(entity).value;
      }

      for (const auto &attachment : component.attachments)
      {
        if (!attachment.enabled)
        {
          continue;
        }

        if (attachment.assetPath.empty())
        {
          if (errorMessage != nullptr)
          {
            *errorMessage = entityName + " has a Blueprint attachment with no asset selected.";
          }
          stop();
          return false;
        }

        std::string loadError;
        const CompiledBlueprint *compiled =
            impl_->cache.acquire(workspaceRoot, attachment.assetPath, &loadError);

        if (compiled == nullptr)
        {
          if (errorMessage != nullptr)
          {
            *errorMessage = entityName + ": " + loadError;
          }
          stop();
          return false;
        }

        if (!compiled->succeeded)
        {
          if (errorMessage != nullptr)
          {
            *errorMessage =
                entityName + ": Blueprint '" + attachment.assetPath + "' has compile errors:\n" +
                compiled->error_summary();
          }
          stop();
          return false;
        }

        auto entry = std::make_unique<Impl::Entry>();
        entry->entity = entity;
        entry->entityName = entityName;
        entry->assetPath = attachment.assetPath;
        entry->instance = make_blueprint_instance(*compiled, entity);

        // Per-entity overrides win over the asset's defaults.
        for (const auto &[name, text] : attachment.variableOverrides)
        {
          const int index = compiled->blueprint.variable_index(name);
          if (index < 0)
          {
            Log::warn_tagged(
                "blueprint",
                "%s: override for unknown variable '%s' in %s",
                entityName.c_str(),
                name.c_str(),
                attachment.assetPath.c_str());
            continue;
          }

          entry->instance.variables[static_cast<std::size_t>(index)] =
              BlueprintValue::parse(text, compiled->blueprint.variables[static_cast<std::size_t>(index)].type);
        }

        impl_->entries.push_back(std::move(entry));
      }
    }

    impl_->running = true;
    impl_->faulted = false;
    impl_->lastError.clear();

    register_script_blueprint_runtime(this);

    for (auto &entry : impl_->entries)
    {
      entry->instance.started = true;
      impl_->vm->set_delta_time(0.0f);
      impl_->vm->dispatch(entry->instance, "begin_play");
      impl_->note_fault(*entry);
    }

    if (impl_->faulted)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = impl_->lastError;
      }

      // A BeginPlay fault is a failed start, so tear everything down rather
      // than leaving the runtime reporting is_running() with dead instances.
      const std::string faultMessage = impl_->lastError;
      stop();
      impl_->faulted = true;
      impl_->lastError = faultMessage;
      return false;
    }

    return true;
  }

  void BlueprintRuntime::update(
      float deltaTime,
      ComponentManager &componentManager,
      EntityManager &entityManager)
  {
    if (!impl_->running || impl_->vm == nullptr)
    {
      return;
    }

    impl_->componentManager = &componentManager;
    impl_->entityManager = &entityManager;

    impl_->vm->set_delta_time(deltaTime);

    // Scripts run before Blueprints in both frame loops, so an event a script
    // sent from `onUpdate` is handled here, in the same frame, ahead of Tick.
    impl_->drain_script_events(componentManager);

    for (auto &entry : impl_->entries)
    {
      if (!impl_->entry_is_live(*entry, componentManager))
      {
        continue;
      }

      entry->instance.elapsedSeconds += deltaTime;
      impl_->vm->advance_latent_actions(entry->instance, deltaTime);
      impl_->note_fault(*entry);

      if (entry->instance.faulted)
      {
        continue;
      }

      impl_->vm->dispatch(entry->instance, "tick", {BlueprintValue::from_float(deltaTime)});
      impl_->note_fault(*entry);
    }
  }

  void BlueprintRuntime::stop()
  {
    if (script_blueprint_runtime() == this)
    {
      register_script_blueprint_runtime(nullptr);
    }
    // Nothing is listening once play stops, so anything still queued is dead
    // mail, and the entity ids it names get recycled by the next session.
    clear_pending_blueprint_events();

    impl_->teardown();
  }

  bool BlueprintRuntime::is_running() const
  {
    return impl_->running;
  }

  bool BlueprintRuntime::faulted() const
  {
    return impl_->faulted;
  }

  const std::string &BlueprintRuntime::last_error() const
  {
    return impl_->lastError;
  }

  void BlueprintRuntime::on_key_down(int keyCode)
  {
    if (impl_->componentManager == nullptr)
    {
      return;
    }

    impl_->broadcast(*impl_->componentManager, "key_down", {BlueprintValue::from_int(keyCode)});
  }

  void BlueprintRuntime::on_key_up(int keyCode)
  {
    if (impl_->componentManager == nullptr)
    {
      return;
    }

    impl_->broadcast(*impl_->componentManager, "key_up", {BlueprintValue::from_int(keyCode)});
  }

  void BlueprintRuntime::on_mouse_down(int button, float screenX, float screenY)
  {
    if (impl_->componentManager == nullptr)
    {
      return;
    }

    impl_->broadcast(
        *impl_->componentManager,
        "mouse_down",
        {BlueprintValue::from_int(button),
         BlueprintValue::from_float(screenX),
         BlueprintValue::from_float(screenY)});
  }

  void BlueprintRuntime::on_mouse_up(int button, float screenX, float screenY)
  {
    if (impl_->componentManager == nullptr)
    {
      return;
    }

    impl_->broadcast(
        *impl_->componentManager,
        "mouse_up",
        {BlueprintValue::from_int(button),
         BlueprintValue::from_float(screenX),
         BlueprintValue::from_float(screenY)});
  }

  void BlueprintRuntime::on_mouse_move(float screenX, float screenY)
  {
    if (impl_->componentManager == nullptr)
    {
      return;
    }

    impl_->broadcast(
        *impl_->componentManager,
        "mouse_move",
        {BlueprintValue::from_float(screenX), BlueprintValue::from_float(screenY)});
  }

  void BlueprintRuntime::on_collision_begin(Entity::EntityId a, Entity::EntityId b)
  {
    if (!impl_->running || impl_->vm == nullptr || impl_->componentManager == nullptr)
    {
      return;
    }

    for (auto &entry : impl_->entries)
    {
      if (!impl_->entry_is_live(*entry, *impl_->componentManager))
      {
        continue;
      }

      if (entry->entity == a)
      {
        impl_->vm->dispatch(entry->instance, "collision_begin", {BlueprintValue::from_entity(b)});
      }
      else if (entry->entity == b)
      {
        impl_->vm->dispatch(entry->instance, "collision_begin", {BlueprintValue::from_entity(a)});
      }
      impl_->note_fault(*entry);
    }
  }

  void BlueprintRuntime::on_collision_end(Entity::EntityId a, Entity::EntityId b)
  {
    if (!impl_->running || impl_->vm == nullptr || impl_->componentManager == nullptr)
    {
      return;
    }

    for (auto &entry : impl_->entries)
    {
      if (!impl_->entry_is_live(*entry, *impl_->componentManager))
      {
        continue;
      }

      if (entry->entity == a)
      {
        impl_->vm->dispatch(entry->instance, "collision_end", {BlueprintValue::from_entity(b)});
      }
      else if (entry->entity == b)
      {
        impl_->vm->dispatch(entry->instance, "collision_end", {BlueprintValue::from_entity(a)});
      }
      impl_->note_fault(*entry);
    }
  }

  void BlueprintRuntime::send_custom_event(
      const std::string &eventName,
      const std::vector<BlueprintValue> &payload)
  {
    if (impl_->componentManager == nullptr)
    {
      return;
    }

    impl_->broadcast(*impl_->componentManager, "custom:" + eventName, payload);
  }

  void BlueprintRuntime::send_custom_event_to(
      Entity::EntityId entity,
      const std::string &eventName,
      const std::vector<BlueprintValue> &payload)
  {
    if (impl_->componentManager == nullptr)
    {
      return;
    }

    impl_->dispatch_to(*impl_->componentManager, entity, "custom:" + eventName, payload);
  }

  BlueprintValue BlueprintRuntime::get_variable(
      Entity::EntityId entity,
      const std::string &name,
      bool *found) const
  {
    if (found != nullptr)
    {
      *found = false;
    }

    for (const auto &entry : impl_->entries)
    {
      if (entry->entity != entity)
      {
        continue;
      }

      const int slot = Impl::variable_slot(*entry, name);
      if (slot < 0)
      {
        continue;
      }

      if (found != nullptr)
      {
        *found = true;
      }
      return entry->instance.variables[static_cast<std::size_t>(slot)];
    }

    return BlueprintValue();
  }

  int BlueprintRuntime::set_variable(
      Entity::EntityId entity,
      const std::string &name,
      const BlueprintValue &value)
  {
    int written = 0;

    for (auto &entry : impl_->entries)
    {
      if (entry->entity != entity)
      {
        continue;
      }

      const int slot = Impl::variable_slot(*entry, name);
      if (slot < 0)
      {
        continue;
      }

      // Coerce to the declared type so a graph reading the variable through a
      // typed pin never sees something the compiler said could not be there.
      const ValueType declared =
          entry->instance.compiled->blueprint.variables[static_cast<std::size_t>(slot)].type;
      entry->instance.variables[static_cast<std::size_t>(slot)] = value.coerced_to(declared);
      ++written;
    }

    return written;
  }

  std::vector<BlueprintRuntime::InstanceView> BlueprintRuntime::instances() const
  {
    std::vector<InstanceView> views;
    views.reserve(impl_->entries.size());

    for (const auto &entry : impl_->entries)
    {
      InstanceView view;
      view.entity = entry->entity;
      view.entityName = entry->entityName;
      view.assetPath = entry->assetPath;
      view.instance = &entry->instance;
      views.push_back(std::move(view));
    }

    return views;
  }

  int BlueprintRuntime::instance_count() const
  {
    return static_cast<int>(impl_->entries.size());
  }

  int BlueprintRuntime::instance_count_for(Entity::EntityId entity) const
  {
    int count = 0;
    for (const auto &entry : impl_->entries)
    {
      if (entry->entity == entity && !entry->retired)
      {
        ++count;
      }
    }
    return count;
  }
}
