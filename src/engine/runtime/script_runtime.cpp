#include "script_runtime.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../components/name_component.hpp"
#include "../components/script_component.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"
#include "../core/ecs/world_utils.hpp"
#include "hades_neural_script.hpp"
#include "hades_script.hpp"
#include "policy_registry.hpp"
#include "script_compiler.hpp"
#include "script_loader.hpp"

namespace hades
{
  namespace
  {
    struct ScriptedAttachment
    {
      std::string className;
      std::string modelPath; // relative to workspace root, empty for legacy
    };

    struct ScriptedEntity
    {
      Entity::EntityId entity = Entity::INVALID;
      std::string name;
      std::vector<ScriptedAttachment> attachments;
    };

    std::string default_class_name(const std::string &scriptPath)
    {
      return std::filesystem::path(scriptPath).stem().string();
    }

    std::filesystem::path resolve_script_source_path(
        const std::string &scriptPath,
        const std::filesystem::path &workspaceRoot)
    {
      const std::filesystem::path rawPath(scriptPath);
      if (rawPath.is_absolute())
      {
        return rawPath.lexically_normal();
      }

      if (!workspaceRoot.empty())
      {
        return (workspaceRoot / rawPath).lexically_normal();
      }

      return std::filesystem::absolute(rawPath).lexically_normal();
    }

    std::filesystem::path resolve_script_build_root(const std::filesystem::path &workspaceRoot)
    {
      if (workspaceRoot.empty())
      {
        return std::filesystem::temp_directory_path();
      }

      std::error_code absoluteError;
      const std::filesystem::path absoluteWorkspacePath = std::filesystem::absolute(workspaceRoot, absoluteError);
      if (absoluteError)
      {
        return workspaceRoot.lexically_normal() / ".hades";
      }

      return absoluteWorkspacePath.lexically_normal() / ".hades";
    }

    std::filesystem::path resolve_policy_path(
        const std::string &modelPath,
        const std::filesystem::path &workspaceRoot)
    {
      const std::filesystem::path raw(modelPath);
      if (raw.is_absolute())
      {
        return raw.lexically_normal();
      }
      if (!workspaceRoot.empty())
      {
        return (workspaceRoot / raw).lexically_normal();
      }
      return std::filesystem::absolute(raw).lexically_normal();
    }

    bool collect_scripted_entities(
        ComponentManager &componentManager,
        EntityManager &entityManager,
        const std::filesystem::path &workspaceRoot,
        std::optional<Entity::EntityId> worldRoot,
        std::vector<ScriptedEntity> &scriptedEntities,
        std::vector<std::filesystem::path> &uniqueSourceFiles,
        std::string *errorMessage)
    {
      scriptedEntities.clear();
      uniqueSourceFiles.clear();

      std::set<std::string> seenPaths;

      for (Entity::EntityId entity : entityManager.getAllEntities())
      {
        if (worldRoot.has_value() && !entity_belongs_to_world(entity, *worldRoot, componentManager))
        {
          continue;
        }

        if (!componentManager.hasComponent<ScriptComponent>(entity))
        {
          continue;
        }

        const auto &scriptComponent = componentManager.getComponent<ScriptComponent>(entity);
        if (scriptComponent.attachments.empty())
        {
          continue;
        }

        ScriptedEntity scriptedEntity;
        scriptedEntity.entity = entity;

        if (componentManager.hasComponent<NameComponent>(entity))
        {
          scriptedEntity.name = componentManager.getComponent<NameComponent>(entity).value;
        }
        else
        {
          scriptedEntity.name = "Entity " + std::to_string(entity);
        }

        for (const auto &attachment : scriptComponent.attachments)
        {
          if (!attachment.enabled)
          {
            continue;
          }

          if (attachment.scriptPath.empty())
          {
            if (errorMessage != nullptr)
            {
              *errorMessage = "A script attachment is missing its .cpp file path.";
            }
            return false;
          }

          const std::filesystem::path sourcePath = resolve_script_source_path(attachment.scriptPath, workspaceRoot);
          if (!std::filesystem::exists(sourcePath))
          {
            if (errorMessage != nullptr)
            {
              *errorMessage = "Script file does not exist: " + sourcePath.string();
            }
            return false;
          }

          if (sourcePath.extension() != ".cpp")
          {
            if (errorMessage != nullptr)
            {
              *errorMessage = "Only .cpp files can be attached as scripts.";
            }
            return false;
          }

          const std::string resolvedClassName =
              attachment.className.empty() ? default_class_name(attachment.scriptPath) : attachment.className;
          if (resolvedClassName.empty())
          {
            if (errorMessage != nullptr)
            {
              *errorMessage = "Unable to infer a class name from the attached script path.";
            }
            return false;
          }

          scriptedEntity.attachments.push_back({resolvedClassName, attachment.modelPath});

          const std::string normalizedPath = sourcePath.lexically_normal().string();
          if (seenPaths.insert(normalizedPath).second)
          {
            uniqueSourceFiles.push_back(sourcePath);
          }
        }

        if (!scriptedEntity.attachments.empty())
        {
          scriptedEntities.push_back(std::move(scriptedEntity));
        }
      }

      return true;
    }
  }

  struct ScriptInstance
  {
    std::unique_ptr<HadesScript> script;
    ScriptInstanceMode mode = ScriptInstanceMode::Legacy;
#if defined(HADES_HAS_HNE_INFERENCE)
    std::shared_ptr<hne::InferenceRuntime> policy;
    hne::Tensor obsBuffer;
#endif
  };

  struct EntityInstances
  {
    Entity::EntityId entity = Entity::INVALID;
    std::vector<ScriptInstance> scripts;
  };

  struct ScriptRuntime::Impl
  {
    std::vector<ScriptedEntity> trackedEntities;
    ScriptCompiler compiler;
    ScriptLoader loader;
    std::vector<EntityInstances> instances;
    ComponentManager *componentManager = nullptr;
    EntityManager *entityManager = nullptr;
    std::filesystem::path workspaceRoot;
    std::string lastError;
    bool running = false;
    bool faulted = false;
    float viewportWidth = 0.0f;
    float viewportHeight = 0.0f;

    ScriptRuntimeRole role = ScriptRuntimeRole::Editor;
    PolicyRegistry localPolicies;
    PolicyRegistry *policies = nullptr; // non-owning; defaults to &localPolicies

    ScriptContext makeContext(Entity::EntityId entityId)
    {
      return {entityId, *componentManager, *entityManager, viewportWidth, viewportHeight};
    }

    EntityInstances *find_entity(Entity::EntityId entityId)
    {
      for (auto &entry : instances)
      {
        if (entry.entity == entityId)
        {
          return &entry;
        }
      }
      return nullptr;
    }

    const EntityInstances *find_entity(Entity::EntityId entityId) const
    {
      for (const auto &entry : instances)
      {
        if (entry.entity == entityId)
        {
          return &entry;
        }
      }
      return nullptr;
    }
  };

  ScriptRuntime::ScriptRuntime() : impl_(std::make_unique<Impl>())
  {
    impl_->policies = &impl_->localPolicies;
  }

  ScriptRuntime::ScriptRuntime(ScriptRuntimeRole role) : impl_(std::make_unique<Impl>())
  {
    impl_->role = role;
    impl_->policies = &impl_->localPolicies;
  }

  ScriptRuntime::~ScriptRuntime()
  {
    stop();
  }

  bool ScriptRuntime::start(
      ComponentManager &componentManager,
      EntityManager &entityManager,
      std::string *errorMessage)
  {
    return start(componentManager, entityManager, std::filesystem::path(), std::nullopt, nullptr, errorMessage);
  }

  bool ScriptRuntime::start(
      ComponentManager &componentManager,
      EntityManager &entityManager,
      const std::filesystem::path &workspaceRoot,
      std::string *errorMessage)
  {
    return start(componentManager, entityManager, workspaceRoot, std::nullopt, nullptr, errorMessage);
  }

  bool ScriptRuntime::start(
      ComponentManager &componentManager,
      EntityManager &entityManager,
      const std::filesystem::path &workspaceRoot,
      std::optional<Entity::EntityId> worldRoot,
      std::string *errorMessage)
  {
    return start(componentManager, entityManager, workspaceRoot, worldRoot, nullptr, errorMessage);
  }

  bool ScriptRuntime::start(
      ComponentManager &componentManager,
      EntityManager &entityManager,
      const std::filesystem::path &workspaceRoot,
      std::optional<Entity::EntityId> worldRoot,
      PolicyRegistry *sharedPolicies,
      std::string *errorMessage)
  {
    stop();

    impl_->workspaceRoot = workspaceRoot;
    impl_->policies = (sharedPolicies != nullptr) ? sharedPolicies : &impl_->localPolicies;

    std::vector<ScriptedEntity> scriptedEntities;
    std::vector<std::filesystem::path> sourceFiles;
    std::string localError;
    if (!collect_scripted_entities(
            componentManager,
            entityManager,
            workspaceRoot,
            worldRoot,
            scriptedEntities,
            sourceFiles,
            &localError))
    {
      impl_->lastError = localError;
      impl_->faulted = true;
      if (errorMessage != nullptr)
      {
        *errorMessage = localError;
      }
      return false;
    }

    impl_->trackedEntities = std::move(scriptedEntities);
    if (impl_->trackedEntities.empty())
    {
      impl_->lastError.clear();
      impl_->faulted = false;
      impl_->running = false;
      if (errorMessage != nullptr)
      {
        errorMessage->clear();
      }
      return true;
    }

    // Compile user scripts into a shared library.
    const std::filesystem::path buildDir =
        resolve_script_build_root(workspaceRoot) / "script-host" / "current";

    if (!impl_->compiler.compile(sourceFiles, buildDir, &localError))
    {
      impl_->lastError = localError;
      impl_->faulted = true;
      if (errorMessage != nullptr)
      {
        *errorMessage = localError;
      }
      return false;
    }

    // Best-effort write of compile_commands.json / .clangd so clangd-based IDEs
    // pick up the engine and HNE include paths automatically. Failure here is
    // not fatal — it only degrades IDE autocomplete.
    if (!workspaceRoot.empty())
    {
      std::string ccErr;
      (void)impl_->compiler.writeCompileCommands(sourceFiles, workspaceRoot, &ccErr);
    }

    // Load the compiled library.
    if (!impl_->loader.load(impl_->compiler.libraryPath(), &localError))
    {
      impl_->lastError = localError;
      impl_->faulted = true;
      if (errorMessage != nullptr)
      {
        *errorMessage = localError;
      }
      return false;
    }

    // Instantiate scripts for each entity and assign dispatch modes.
    impl_->instances.clear();
    for (const auto &trackedEntity : impl_->trackedEntities)
    {
      EntityInstances entityEntry;
      entityEntry.entity = trackedEntity.entity;

      for (const auto &attachment : trackedEntity.attachments)
      {
        HadesScript *raw = impl_->loader.createScript(attachment.className);
        if (raw == nullptr)
        {
          localError = "Script class not found: " + attachment.className +
                       ". Ensure the script uses HADES_REGISTER_SCRIPT(" +
                       attachment.className + ").";
          impl_->lastError = localError;
          impl_->faulted = true;
          if (errorMessage != nullptr)
          {
            *errorMessage = localError;
          }
          return false;
        }

        ScriptInstance instance;
        instance.script.reset(raw);
        instance.mode = ScriptInstanceMode::Legacy;

        auto *neural = dynamic_cast<NeuralScript *>(raw);
        if (neural != nullptr)
        {
          if (!attachment.modelPath.empty())
          {
#if defined(HADES_HAS_HNE_INFERENCE)
            const auto absPath = resolve_policy_path(attachment.modelPath, workspaceRoot);
            const auto obsSpec = neural->observationSpace();
            const auto actSpec = neural->actionSpace();
            auto result = impl_->policies->get_validated(absPath, obsSpec, actSpec);
            if (!result.runtime)
            {
              localError =
                  "Neural script '" + attachment.className + "' on entity '" +
                  trackedEntity.name + "': " + result.error;
              impl_->lastError = localError;
              impl_->faulted = true;
              if (errorMessage != nullptr)
              {
                *errorMessage = localError;
              }
              return false;
            }
            instance.policy = result.runtime;
            instance.obsBuffer.data.assign(static_cast<std::size_t>(hne::flat_size(obsSpec)), 0.0f);
            instance.obsBuffer.shape = std::visit(
                [](const auto &s) -> std::vector<int32_t>
                {
                  using T = std::decay_t<decltype(s)>;
                  if constexpr (std::is_same_v<T, hne::BoxSpace>)
                  {
                    return s.shape;
                  }
                  else
                  {
                    return {hne::flat_size(hne::SpaceSpec{s})};
                  }
                },
                obsSpec);
            instance.mode = ScriptInstanceMode::Inference;
#else
            localError =
                "Neural script '" + attachment.className + "' on entity '" +
                trackedEntity.name +
                "' declares modelPath but HadesEngine was built without HNE inference support.";
            impl_->lastError = localError;
            impl_->faulted = true;
            if (errorMessage != nullptr)
            {
              *errorMessage = localError;
            }
            return false;
#endif
          }
          else if (impl_->role == ScriptRuntimeRole::TrainingHost)
          {
            // Will be promoted explicitly via mark_training_owned() once the
            // env adapter knows which entity is the subject.
            instance.mode = ScriptInstanceMode::Legacy;
          }
          else
          {
            localError =
                "Neural script '" + attachment.className + "' on entity '" +
                trackedEntity.name +
                "' has no modelPath. Either train and attach a policy (.hades/policies/<run>/policy.pt)"
                " or attach this entity via the Neural Training panel.";
            impl_->lastError = localError;
            impl_->faulted = true;
            if (errorMessage != nullptr)
            {
              *errorMessage = localError;
            }
            return false;
          }
        }

        entityEntry.scripts.push_back(std::move(instance));
      }

      impl_->instances.push_back(std::move(entityEntry));
    }

    // Call onStart on all script instances.
    for (auto &entry : impl_->instances)
    {
      ScriptContext ctx{entry.entity, componentManager, entityManager, impl_->viewportWidth, impl_->viewportHeight};
      for (auto &instance : entry.scripts)
      {
        instance.script->onStart(ctx);
      }
    }

    HadesAPI::clear();

    impl_->componentManager = &componentManager;
    impl_->entityManager = &entityManager;
    impl_->lastError.clear();
    impl_->faulted = false;
    impl_->running = true;
    if (errorMessage != nullptr)
    {
      errorMessage->clear();
    }
    return true;
  }

  void ScriptRuntime::update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager)
  {
    if (!impl_->running || impl_->instances.empty())
    {
      return;
    }

    // TODO(batching): when multiple instances share the same policy pointer,
    // group their observation tensors and call `policy->evaluate_batch(...)`
    // once per unique policy. Benchmark first — MVP does per-instance calls.
    for (auto &entry : impl_->instances)
    {
      ScriptContext ctx{entry.entity, componentManager, entityManager, impl_->viewportWidth, impl_->viewportHeight};
      for (auto &instance : entry.scripts)
      {
        switch (instance.mode)
        {
        case ScriptInstanceMode::Legacy:
          instance.script->onUpdate(ctx, deltaTime);
          break;

        case ScriptInstanceMode::Inference:
        {
#if defined(HADES_HAS_HNE_INFERENCE)
          auto *neural = static_cast<NeuralScript *>(instance.script.get());
          neural->readObservation(ctx, instance.obsBuffer);
          if (instance.policy)
          {
            hne::Action action = instance.policy->evaluate(instance.obsBuffer, /*deterministic=*/true);
            neural->applyAction(ctx, action, deltaTime);
          }
#endif
          break;
        }

        case ScriptInstanceMode::TrainingOwned:
          // The training env adapter drives this script directly; skip.
          break;
        }
      }
    }
  }

  void ScriptRuntime::stop()
  {
    impl_->instances.clear();
    impl_->loader.unload();
    impl_->trackedEntities.clear();
    impl_->componentManager = nullptr;
    impl_->entityManager = nullptr;
    impl_->running = false;
    impl_->faulted = false;
    impl_->lastError.clear();
    HadesAPI::clear();
  }

  bool ScriptRuntime::is_running() const
  {
    return impl_->running;
  }

  bool ScriptRuntime::faulted() const
  {
    return impl_->faulted;
  }

  const std::string &ScriptRuntime::last_error() const
  {
    return impl_->lastError;
  }

  void ScriptRuntime::on_key_down(int keyCode)
  {
    if (!impl_->running || impl_->instances.empty() ||
        impl_->componentManager == nullptr || impl_->entityManager == nullptr)
    {
      return;
    }

    for (auto &entry : impl_->instances)
    {
      ScriptContext ctx = impl_->makeContext(entry.entity);
      for (auto &instance : entry.scripts)
      {
        instance.script->onKeyDown(ctx, keyCode);
      }
    }
  }

  void ScriptRuntime::on_key_up(int keyCode)
  {
    if (!impl_->running || impl_->instances.empty() ||
        impl_->componentManager == nullptr || impl_->entityManager == nullptr)
    {
      return;
    }

    for (auto &entry : impl_->instances)
    {
      ScriptContext ctx = impl_->makeContext(entry.entity);
      for (auto &instance : entry.scripts)
      {
        instance.script->onKeyUp(ctx, keyCode);
      }
    }
  }

  void ScriptRuntime::on_mouse_down(int button, float screenX, float screenY)
  {
    if (!impl_->running || impl_->instances.empty() ||
        impl_->componentManager == nullptr || impl_->entityManager == nullptr)
    {
      return;
    }

    for (auto &entry : impl_->instances)
    {
      ScriptContext ctx = impl_->makeContext(entry.entity);
      for (auto &instance : entry.scripts)
      {
        instance.script->onMouseDown(ctx, button, screenX, screenY);
      }
    }
  }

  void ScriptRuntime::on_mouse_up(int button, float screenX, float screenY)
  {
    if (!impl_->running || impl_->instances.empty() ||
        impl_->componentManager == nullptr || impl_->entityManager == nullptr)
    {
      return;
    }

    for (auto &entry : impl_->instances)
    {
      ScriptContext ctx = impl_->makeContext(entry.entity);
      for (auto &instance : entry.scripts)
      {
        instance.script->onMouseUp(ctx, button, screenX, screenY);
      }
    }
  }

  void ScriptRuntime::on_mouse_move(float screenX, float screenY)
  {
    if (!impl_->running || impl_->instances.empty() ||
        impl_->componentManager == nullptr || impl_->entityManager == nullptr)
    {
      return;
    }

    for (auto &entry : impl_->instances)
    {
      ScriptContext ctx = impl_->makeContext(entry.entity);
      for (auto &instance : entry.scripts)
      {
        instance.script->onMouseMove(ctx, screenX, screenY);
      }
    }
  }

  void ScriptRuntime::set_viewport_size(float width, float height)
  {
    impl_->viewportWidth = width;
    impl_->viewportHeight = height;
  }

  ScriptValue ScriptRuntime::send_message(
      Entity::EntityId entity,
      const std::string &name,
      const ScriptValue &value)
  {
    if (!impl_->running || impl_->componentManager == nullptr || impl_->entityManager == nullptr)
    {
      return ScriptValue();
    }

    EntityInstances *entry = impl_->find_entity(entity);
    if (entry == nullptr)
    {
      return ScriptValue();
    }

    ScriptContext ctx = impl_->makeContext(entry->entity);
    for (auto &instance : entry->scripts)
    {
      ScriptValue reply = instance.script->onMessage(ctx, name, value);
      if (!reply.empty())
      {
        return reply;
      }
    }

    return ScriptValue();
  }

  ScriptValue ScriptRuntime::broadcast_message(const std::string &name, const ScriptValue &value)
  {
    if (!impl_->running || impl_->componentManager == nullptr || impl_->entityManager == nullptr)
    {
      return ScriptValue();
    }

    ScriptValue result;
    for (auto &entry : impl_->instances)
    {
      ScriptContext ctx = impl_->makeContext(entry.entity);
      for (auto &instance : entry.scripts)
      {
        ScriptValue reply = instance.script->onMessage(ctx, name, value);
        // Every script still hears the broadcast; only the first answer is
        // kept, so a listener cannot silence the ones behind it.
        if (result.empty() && !reply.empty())
        {
          result = std::move(reply);
        }
      }
    }

    return result;
  }

  std::optional<std::string> ScriptRuntime::consume_pending_world_load()
  {
    return HadesAPI::consumePendingWorldLoad();
  }

  std::string ScriptRuntime::collect_observations() const
  {
    if (!impl_->running)
    {
      return "{}";
    }

    return HadesAPI::serializeJson();
  }

  bool ScriptRuntime::compile(
      const std::vector<std::filesystem::path> &sourceFiles,
      std::string *errorMessage)
  {
    if (sourceFiles.empty())
    {
      return true;
    }

    ScriptCompiler compiler;
    const auto buildDir = std::filesystem::temp_directory_path() / "hades-script-compile-check";
    return compiler.compile(sourceFiles, buildDir, errorMessage);
  }

  HadesScript *ScriptRuntime::find_script(Entity::EntityId entityId) const
  {
    const auto *entry = impl_->find_entity(entityId);
    if (entry == nullptr || entry->scripts.empty())
    {
      return nullptr;
    }
    return entry->scripts.front().script.get();
  }

  void ScriptRuntime::mark_training_owned(Entity::EntityId entityId)
  {
    auto *entry = impl_->find_entity(entityId);
    if (entry == nullptr)
    {
      return;
    }
    for (auto &instance : entry->scripts)
    {
      instance.mode = ScriptInstanceMode::TrainingOwned;
    }
  }

  ScriptInstanceMode ScriptRuntime::mode_for(Entity::EntityId entityId) const
  {
    const auto *entry = impl_->find_entity(entityId);
    if (entry == nullptr || entry->scripts.empty())
    {
      return ScriptInstanceMode::Legacy;
    }
    return entry->scripts.front().mode;
  }
}
