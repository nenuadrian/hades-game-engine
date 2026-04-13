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
#include "hades_script.hpp"
#include "script_compiler.hpp"
#include "script_loader.hpp"

namespace hades
{
  namespace
  {
    struct ScriptedEntity
    {
      Entity::EntityId entity = Entity::INVALID;
      std::string name;
      std::vector<std::string> classNames;
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

          scriptedEntity.classNames.push_back(resolvedClassName);

          const std::string normalizedPath = sourcePath.lexically_normal().string();
          if (seenPaths.insert(normalizedPath).second)
          {
            uniqueSourceFiles.push_back(sourcePath);
          }
        }

        if (!scriptedEntity.classNames.empty())
        {
          scriptedEntities.push_back(std::move(scriptedEntity));
        }
      }

      return true;
    }
  }

  struct ScriptRuntime::Impl
  {
    std::vector<ScriptedEntity> trackedEntities;
    ScriptCompiler compiler;
    ScriptLoader loader;
    std::vector<std::pair<Entity::EntityId, std::vector<std::unique_ptr<HadesScript>>>> instances;
    ComponentManager *componentManager = nullptr;
    EntityManager *entityManager = nullptr;
    std::string lastError;
    bool running = false;
    bool faulted = false;
    float viewportWidth = 0.0f;
    float viewportHeight = 0.0f;

    ScriptContext makeContext(Entity::EntityId entityId)
    {
      return {entityId, *componentManager, *entityManager, viewportWidth, viewportHeight};
    }
  };

  ScriptRuntime::ScriptRuntime() : impl_(std::make_unique<Impl>()) {}
  ScriptRuntime::~ScriptRuntime()
  {
    stop();
  }

  bool ScriptRuntime::start(
      ComponentManager &componentManager,
      EntityManager &entityManager,
      std::string *errorMessage)
  {
    return start(componentManager, entityManager, std::filesystem::path(), std::nullopt, errorMessage);
  }

  bool ScriptRuntime::start(
      ComponentManager &componentManager,
      EntityManager &entityManager,
      const std::filesystem::path &workspaceRoot,
      std::string *errorMessage)
  {
    return start(componentManager, entityManager, workspaceRoot, std::nullopt, errorMessage);
  }

  bool ScriptRuntime::start(
      ComponentManager &componentManager,
      EntityManager &entityManager,
      const std::filesystem::path &workspaceRoot,
      std::optional<Entity::EntityId> worldRoot,
      std::string *errorMessage)
  {
    stop();

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

    // Instantiate scripts for each entity.
    impl_->instances.clear();
    for (const auto &trackedEntity : impl_->trackedEntities)
    {
      std::vector<std::unique_ptr<HadesScript>> entityScripts;

      for (const auto &className : trackedEntity.classNames)
      {
        HadesScript *script = impl_->loader.createScript(className);
        if (script == nullptr)
        {
          localError = "Script class not found: " + className +
                       ". Ensure the script uses HADES_REGISTER_SCRIPT(" + className + ").";
          impl_->lastError = localError;
          impl_->faulted = true;
          if (errorMessage != nullptr)
          {
            *errorMessage = localError;
          }
          return false;
        }

        entityScripts.emplace_back(script);
      }

      impl_->instances.emplace_back(trackedEntity.entity, std::move(entityScripts));
    }

    // Call onStart on all script instances.
    for (auto &[entityId, scripts] : impl_->instances)
    {
      ScriptContext ctx{entityId, componentManager, entityManager, impl_->viewportWidth, impl_->viewportHeight};
      for (auto &script : scripts)
      {
        script->onStart(ctx);
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

    for (auto &[entityId, scripts] : impl_->instances)
    {
      ScriptContext ctx{entityId, componentManager, entityManager, impl_->viewportWidth, impl_->viewportHeight};
      for (auto &script : scripts)
      {
        script->onUpdate(ctx, deltaTime);
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

    for (auto &[entityId, scripts] : impl_->instances)
    {
      ScriptContext ctx = impl_->makeContext(entityId);
      for (auto &script : scripts)
      {
        script->onKeyDown(ctx, keyCode);
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

    for (auto &[entityId, scripts] : impl_->instances)
    {
      ScriptContext ctx = impl_->makeContext(entityId);
      for (auto &script : scripts)
      {
        script->onKeyUp(ctx, keyCode);
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

    for (auto &[entityId, scripts] : impl_->instances)
    {
      ScriptContext ctx = impl_->makeContext(entityId);
      for (auto &script : scripts)
      {
        script->onMouseDown(ctx, button, screenX, screenY);
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

    for (auto &[entityId, scripts] : impl_->instances)
    {
      ScriptContext ctx = impl_->makeContext(entityId);
      for (auto &script : scripts)
      {
        script->onMouseUp(ctx, button, screenX, screenY);
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

    for (auto &[entityId, scripts] : impl_->instances)
    {
      ScriptContext ctx = impl_->makeContext(entityId);
      for (auto &script : scripts)
      {
        script->onMouseMove(ctx, screenX, screenY);
      }
    }
  }

  void ScriptRuntime::set_viewport_size(float width, float height)
  {
    impl_->viewportWidth = width;
    impl_->viewportHeight = height;
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
}
