#include "hades_script_env.hpp"

#include <utility>

#include "../components/name_component.hpp"
#include "../components/script_component.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"
#include "../core/ecs/scene_serializer.hpp"
#include "../core/ecs/world_utils.hpp"
#include "../physics/physics_world.hpp"
#include "../runtime/hades_neural_script.hpp"
#include "../runtime/script_runtime.hpp"
#include "../systems/physics_system.hpp"

namespace hades
{
  namespace
  {
    std::filesystem::path world_file_path(
        const std::filesystem::path &workspacePath,
        const std::string &worldName)
    {
      std::filesystem::path path = workspacePath / ".hades" / "worlds" / (worldName + ".json");
      return path.lexically_normal();
    }
  }

  HadesScriptEnv::HadesScriptEnv(Config config) : config_(std::move(config))
  {
    std::string err;
    if (!build_environment(err))
    {
      lastError_ = err;
      ready_ = false;
      return;
    }
    ready_ = true;
  }

  HadesScriptEnv::~HadesScriptEnv()
  {
    if (scriptRuntime_)
    {
      scriptRuntime_->stop();
    }
    if (physicsSystem_)
    {
      physicsSystem_->clear_bodies();
    }
    if (physicsWorld_)
    {
      physicsWorld_->shutdown();
    }
  }

  bool HadesScriptEnv::build_environment(std::string &errorMessage)
  {
    if (config_.subjects.empty())
    {
      errorMessage = "HadesScriptEnv requires at least one TrainingSubject.";
      return false;
    }

    entityManager_ = std::make_unique<EntityManager>();
    componentManager_ = std::make_unique<ComponentManager>();
    physicsWorld_ = std::make_unique<PhysicsWorld>();
    if (!physicsWorld_->init())
    {
      errorMessage = "Failed to initialize PhysicsWorld for training env.";
      return false;
    }
    physicsSystem_ = std::make_unique<PhysicsSystem>();
    physicsSystem_->setPhysicsWorld(physicsWorld_.get());

    const auto worldPath = world_file_path(config_.workspacePath, config_.worldName);
    auto loaded = load_world_from_file(worldPath, *entityManager_, *componentManager_, &errorMessage);
    if (!loaded.has_value())
    {
      return false;
    }
    worldRoot_ = *loaded;

    // Capture a snapshot right after the fresh load so reset() can rewind
    // without re-parsing the file each time.
    worldSnapshot_ = snapshot_all_worlds(*entityManager_, *componentManager_);

    subjectEntity_ = resolve_subject_entity();
    if (subjectEntity_ == Entity::INVALID)
    {
      errorMessage =
          "Training subject entity '" + config_.subjects.front().entityName +
          "' not found in world '" + config_.worldName + "'.";
      return false;
    }

    scriptRuntime_ = std::make_unique<ScriptRuntime>(ScriptRuntimeRole::TrainingHost);
    if (!scriptRuntime_->start(
            *componentManager_,
            *entityManager_,
            config_.workspacePath,
            worldRoot_,
            config_.sharedPolicies,
            &errorMessage))
    {
      return false;
    }

    scriptRuntime_->mark_training_owned(subjectEntity_);
    physicsSystem_->set_active_world(worldRoot_);

    HadesScript *scriptPtr = scriptRuntime_->find_script(subjectEntity_);
    subjectScript_ = dynamic_cast<NeuralScript *>(scriptPtr);
    if (subjectScript_ == nullptr)
    {
      errorMessage =
          "Subject entity's attachment is not a NeuralScript: " +
          config_.subjects.front().className;
      return false;
    }

    obsSpec_ = subjectScript_->observationSpace();
    actSpec_ = subjectScript_->actionSpace();
    return true;
  }

  Entity::EntityId HadesScriptEnv::resolve_subject_entity() const
  {
    const auto &target = config_.subjects.front().entityName;
    for (Entity::EntityId id : entityManager_->getAllEntities())
    {
      if (!entity_belongs_to_world(id, worldRoot_, *componentManager_))
      {
        continue;
      }
      if (!componentManager_->hasComponent<NameComponent>(id))
      {
        continue;
      }
      if (componentManager_->getComponent<NameComponent>(id).value == target)
      {
        return id;
      }
    }
    return Entity::INVALID;
  }

  void HadesScriptEnv::restore_from_snapshot()
  {
    // Stop the script runtime before touching entities — otherwise destroy +
    // re-create would dangle instance pointers.
    if (scriptRuntime_)
    {
      scriptRuntime_->stop();
    }

    physicsSystem_->clear_bodies();

    // Blow away the current world and rebuild it from the captured snapshot.
    destroy_world_tree(worldRoot_, *entityManager_, *componentManager_);
    std::unordered_map<Entity::EntityId, Entity::EntityId> remap;
    restore_all_worlds_from_snapshot(worldSnapshot_, *entityManager_, *componentManager_, &remap);

    // The world root may have been renumbered — re-resolve by matching its
    // snapshot id through the remap.
    auto rootIt = remap.find(worldRoot_);
    if (rootIt != remap.end())
    {
      worldRoot_ = rootIt->second;
    }

    subjectEntity_ = resolve_subject_entity();

    std::string err;
    scriptRuntime_->start(
        *componentManager_,
        *entityManager_,
        config_.workspacePath,
        worldRoot_,
        config_.sharedPolicies,
        &err);
    if (!err.empty())
    {
      lastError_ = err;
    }
    scriptRuntime_->mark_training_owned(subjectEntity_);
    physicsSystem_->set_active_world(worldRoot_);

    HadesScript *scriptPtr = scriptRuntime_->find_script(subjectEntity_);
    subjectScript_ = dynamic_cast<NeuralScript *>(scriptPtr);
  }

  hne::SpaceSpec HadesScriptEnv::observation_space() const
  {
    return obsSpec_;
  }

  hne::SpaceSpec HadesScriptEnv::action_space() const
  {
    return actSpec_;
  }

  hne::Tensor HadesScriptEnv::reset(int32_t /*seed*/)
  {
    hne::Tensor obs;
    obs.data.assign(static_cast<std::size_t>(hne::flat_size(obsSpec_)), 0.0f);

    if (!ready_)
    {
      return obs;
    }

    restore_from_snapshot();
    stepsThisEpisode_ = 0;

    if (subjectScript_ == nullptr)
    {
      return obs;
    }

    ScriptContext ctx{subjectEntity_, *componentManager_, *entityManager_, 0.0f, 0.0f};
    subjectScript_->onReset(ctx);
    subjectScript_->readObservation(ctx, obs);
    return obs;
  }

  hne::StepResult HadesScriptEnv::step(const hne::Action &action)
  {
    hne::StepResult result;
    result.observation.data.assign(static_cast<std::size_t>(hne::flat_size(obsSpec_)), 0.0f);

    if (!ready_ || subjectScript_ == nullptr)
    {
      result.terminated = true;
      return result;
    }

    ScriptContext ctx{subjectEntity_, *componentManager_, *entityManager_, 0.0f, 0.0f};

    // 1. Apply the agent's action.
    subjectScript_->applyAction(ctx, action, config_.tickDt);

    // 2. Tick all other scripts (legacy + inference); the subject is
    //    TrainingOwned so it's skipped here.
    scriptRuntime_->update(config_.tickDt, *componentManager_, *entityManager_);

    // 3. Advance physics by one fixed step.
    physicsWorld_->step(config_.tickDt, 1);

    // 4. Read the post-step observation, reward, and done flag.
    subjectScript_->readObservation(ctx, result.observation);
    result.reward = subjectScript_->computeReward(ctx, config_.tickDt);
    result.terminated = subjectScript_->isDone(ctx);

    ++stepsThisEpisode_;
    if (!result.terminated && stepsThisEpisode_ >= config_.maxStepsPerEpisode)
    {
      result.truncated = true;
    }

    return result;
  }

  std::string HadesScriptEnv::name() const
  {
    return "HadesScriptEnv(" + config_.worldName + "/" +
           (config_.subjects.empty() ? std::string("<no-subject>")
                                     : config_.subjects.front().entityName) +
           ")";
  }
}
