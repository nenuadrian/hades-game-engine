#include "neural_training_plugin.hpp"

#ifdef HADES_HAS_HNE_TRAINING

#include <algorithm>
#include <chrono>
#include <fstream>
#include <numeric>

#include <hne/imgui/training_widgets.hpp>
#include <hne/training/callbacks.hpp>
#include <nlohmann/json.hpp>

#ifdef HADES_HAS_HNE_WANDB
#include <hne/imgui/wandb_panel.hpp>
#include <hne/wandb/callback.hpp>
#endif

#include "imgui.h"

#include "../../engine/core/ecs/scene_serializer.hpp"
#include "../../engine/runtime/policy_registry.hpp"
#include "../../engine/training/hades_script_env.hpp"

namespace hades
{
  namespace
  {
    constexpr std::size_t kMaxHistory = 512;

    std::filesystem::path policies_root(const std::filesystem::path &workspacePath)
    {
      return workspacePath / ".hades" / "policies";
    }

    std::filesystem::path run_dir(
        const std::filesystem::path &workspacePath, const std::string &runName)
    {
      return policies_root(workspacePath) / runName;
    }

    std::string iso_timestamp_now()
    {
      const auto now = std::chrono::system_clock::to_time_t(
          std::chrono::system_clock::now());
      char buf[32];
      std::tm tmUtc{};
#if defined(_WIN32)
      gmtime_s(&tmUtc, &now);
#else
      gmtime_r(&now, &tmUtc);
#endif
      std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmUtc);
      return std::string(buf);
    }
  }

  NeuralTrainingPlugin::NeuralTrainingPlugin()
      : sharedPolicies_(std::make_unique<PolicyRegistry>())
  {
    // Reasonable defaults for quick iteration on a laptop CPU.
    config_.num_envs = 4;
    config_.rollout_length = 512;
    config_.total_timesteps = 200000;
    config_.eval_interval = 10;
  }

  NeuralTrainingPlugin::~NeuralTrainingPlugin()
  {
    stop_training();
  }

  void NeuralTrainingPlugin::render(EditorPluginContext &context)
  {
    if (!visible_)
    {
      return;
    }

    if (focusRequested_)
    {
      ImGui::SetNextWindowFocus();
      focusRequested_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(520, 720), ImGuiCond_FirstUseEver);
    bool open = visible_;
    if (!ImGui::Begin("Neural Training", &open))
    {
      ImGui::End();
      visible_ = open;
      return;
    }
    visible_ = open;

    draw_panel(context);
    ImGui::End();
  }

  void NeuralTrainingPlugin::draw_panel(EditorPluginContext &context)
  {
    const auto &workspacePath = context.workspacePath;

    if (workspacePath.empty())
    {
      ImGui::TextDisabled("Open a workspace to train a policy.");
      return;
    }

    // ---- Run setup ----------------------------------------------------------
    ImGui::SeparatorText("Run");

    // Auto-detect newly saved worlds by watching the worlds directory mtime.
    poll_worlds_dir(workspacePath);

    if (worldNames_.empty())
    {
      ImGui::TextDisabled("No worlds found in %s/.hades/worlds/",
                          workspacePath.string().c_str());
    }
    else
    {
      if (selectedWorldIdx_ < 0 ||
          selectedWorldIdx_ >= static_cast<int>(worldNames_.size()))
      {
        selectedWorldIdx_ = 0;
      }
      const char *preview = worldNames_[selectedWorldIdx_].c_str();
      if (ImGui::BeginCombo("World", preview))
      {
        for (int i = 0; i < static_cast<int>(worldNames_.size()); ++i)
        {
          ImGui::PushID(i);
          const bool selected = (i == selectedWorldIdx_);
          if (ImGui::Selectable(worldNames_[i].c_str(), selected))
          {
            selectedWorldIdx_ = i;
          }
          if (selected)
          {
            ImGui::SetItemDefaultFocus();
          }
          ImGui::PopID();
        }
        ImGui::EndCombo();
      }
    }

    // Re-parse the selected world for entities-with-scripts whenever the
    // world selection changes.
    if (selectedWorldIdx_ != loadedEntitiesWorldIdx_)
    {
      refresh_script_entities(workspacePath);
    }

    if (scriptEntities_.empty())
    {
      ImGui::TextDisabled("No entities with scripts in the selected world.");
      entityName_[0] = '\0';
    }
    else
    {
      if (selectedEntityIdx_ < 0 ||
          selectedEntityIdx_ >= static_cast<int>(scriptEntities_.size()))
      {
        selectedEntityIdx_ = 0;
      }
      const char *entPreview = scriptEntities_[selectedEntityIdx_].c_str();
      if (ImGui::BeginCombo("Subject Entity", entPreview))
      {
        for (int i = 0; i < static_cast<int>(scriptEntities_.size()); ++i)
        {
          ImGui::PushID(i);
          const bool sel = (i == selectedEntityIdx_);
          if (ImGui::Selectable(scriptEntities_[i].c_str(), sel))
          {
            if (selectedEntityIdx_ != i)
            {
              selectedEntityIdx_ = i;
              // Reset the attachment class selection since the list changes
              // with the entity.
              selectedClassIdx_ = 0;
            }
          }
          if (sel)
          {
            ImGui::SetItemDefaultFocus();
          }
          ImGui::PopID();
        }
        ImGui::EndCombo();
      }
      // Mirror the selected entity name into entityName_ so start_training
      // and the export metadata continue to use it unchanged.
      const auto &sel = scriptEntities_[selectedEntityIdx_];
      std::snprintf(entityName_, sizeof(entityName_), "%s", sel.c_str());

      // Attachment class dropdown — populated from the selected entity's
      // script.attachments so the user doesn't have to remember/type the class.
      const auto &classes = entityAttachmentClasses_[selectedEntityIdx_];
      if (classes.empty())
      {
        attachmentClass_[0] = '\0';
        ImGui::TextDisabled("Selected entity has no script attachments.");
      }
      else
      {
        if (selectedClassIdx_ < 0 ||
            selectedClassIdx_ >= static_cast<int>(classes.size()))
        {
          selectedClassIdx_ = 0;
        }
        const char *clsPreview = classes[selectedClassIdx_].c_str();
        if (ImGui::BeginCombo("Attachment Class", clsPreview))
        {
          for (int i = 0; i < static_cast<int>(classes.size()); ++i)
          {
            ImGui::PushID(i);
            const bool s = (i == selectedClassIdx_);
            if (ImGui::Selectable(classes[i].c_str(), s))
            {
              selectedClassIdx_ = i;
            }
            if (s)
            {
              ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
          }
          ImGui::EndCombo();
        }
        std::snprintf(attachmentClass_, sizeof(attachmentClass_), "%s",
                      classes[selectedClassIdx_].c_str());
      }
    }
    ImGui::TextDisabled(
        "Entity + attachment class must resolve to a NeuralScript in the world.");

    // ---- Trainer config -----------------------------------------------------
    ImGui::SeparatorText("Hyperparameters");
    hne::imgui::render_trainer_config_editor(config_);

#ifdef HADES_HAS_HNE_WANDB
    // ---- Weights & Biases ---------------------------------------------------
    ImGui::SeparatorText("Weights & Biases");
    // Attach the current trainer config as hyperparameters so it lands on the
    // run's config blob when the user clicks Start inside the panel.
    nlohmann::json hyperparams = config_;
    (void)hne::imgui::render_wandb_panel(wandbState_, &hyperparams);
    // The wandb panel's Run Name field is the single source of truth for the
    // run identifier; mirror it onto runName_ so the local artifacts folder
    // and export metadata stay in sync.
    if (wandbState_.run_name_buf[0] != '\0')
    {
      std::snprintf(runName_, sizeof(runName_), "%s", wandbState_.run_name_buf);
    }
#endif

    // ---- Controls -----------------------------------------------------------
    ImGui::SeparatorText("Controls");
    const auto state = trainer_ ? trainer_->state() : hne::Trainer::State::Idle;
    const bool running = state == hne::Trainer::State::Running;
    const bool paused = state == hne::Trainer::State::Paused;
    const std::string action = hne::imgui::render_training_controls(running, paused);
    if (!action.empty())
    {
      if (action == "start")
      {
        start_training(workspacePath);
      }
      else if (action == "stop")
      {
        stop_training();
      }
      else if (action == "pause" && trainer_)
      {
        trainer_->pause();
      }
      else if (action == "resume" && trainer_)
      {
        trainer_->resume();
      }
    }

    // ---- Metrics ------------------------------------------------------------
    ImGui::SeparatorText("Metrics");
    {
      std::lock_guard<std::mutex> lock(metricsMutex_);
      const int32_t iter = trainer_ ? trainer_->current_iteration() : 0;
      const int64_t steps = trainer_ ? trainer_->total_timesteps() : 0;
      hne::imgui::render_metrics_dashboard(history_, iter, steps);
      hne::imgui::render_reward_curve(evalRewards_, "Mean Eval Reward");
    }

    // ---- Artifacts ----------------------------------------------------------
    ImGui::SeparatorText("Artifacts");
    if (ImGui::Button("Save Checkpoint"))
    {
      save_checkpoint(workspacePath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Export Policy"))
    {
      export_policy(workspacePath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh Files"))
    {
      refresh_policies(workspacePath);
    }

    const auto dir = run_dir(workspacePath, runName_);
    ImGui::Text("Folder: %s", dir.string().c_str());
    if (policyFiles_.empty())
    {
      ImGui::TextDisabled("(no files yet)");
    }
    else
    {
      for (const auto &file : policyFiles_)
      {
        ImGui::BulletText("%s", file.c_str());
      }
    }

    // ---- Status -------------------------------------------------------------
    if (!lastError_.empty())
    {
      ImGui::Separator();
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s",
                         lastError_.c_str());
    }
    if (!lastInfo_.empty())
    {
      ImGui::Separator();
      ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", lastInfo_.c_str());
    }
  }

  void NeuralTrainingPlugin::refresh_worlds(const std::filesystem::path &workspacePath)
  {
    worldNames_ = list_saved_worlds(workspacePath);
    if (worldNames_.empty())
    {
      selectedWorldIdx_ = -1;
    }
    else if (selectedWorldIdx_ < 0 ||
             selectedWorldIdx_ >= static_cast<int>(worldNames_.size()))
    {
      selectedWorldIdx_ = 0;
    }
  }

  void NeuralTrainingPlugin::poll_worlds_dir(const std::filesystem::path &workspacePath)
  {
    const auto dir = workspacePath / ".hades" / "worlds";
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
    {
      if (!worldNames_.empty())
      {
        worldNames_.clear();
        selectedWorldIdx_ = -1;
      }
      worldsMtime_ = std::filesystem::file_time_type{};
      return;
    }
    const auto mtime = std::filesystem::last_write_time(dir, ec);
    if (ec)
    {
      return;
    }
    if (worldNames_.empty() || mtime != worldsMtime_)
    {
      refresh_worlds(workspacePath);
      worldsMtime_ = mtime;
    }
  }

  void NeuralTrainingPlugin::refresh_script_entities(
      const std::filesystem::path &workspacePath)
  {
    scriptEntities_.clear();
    entityAttachmentClasses_.clear();
    selectedEntityIdx_ = -1;
    selectedClassIdx_ = -1;
    loadedEntitiesWorldIdx_ = selectedWorldIdx_;

    if (selectedWorldIdx_ < 0 ||
        selectedWorldIdx_ >= static_cast<int>(worldNames_.size()))
    {
      return;
    }

    const auto path = workspacePath / ".hades" / "worlds" /
                      (worldNames_[selectedWorldIdx_] + ".json");
    std::ifstream in(path);
    if (!in)
    {
      return;
    }
    nlohmann::json doc;
    try
    {
      in >> doc;
    }
    catch (const std::exception &)
    {
      return;
    }
    const auto entsIt = doc.find("entities");
    if (entsIt == doc.end() || !entsIt->is_array())
    {
      return;
    }
    for (const auto &ent : *entsIt)
    {
      const auto compsIt = ent.find("components");
      if (compsIt == ent.end() || !compsIt->is_object())
      {
        continue;
      }
      const auto scriptIt = compsIt->find("script");
      if (scriptIt == compsIt->end() || !scriptIt->is_object())
      {
        continue;
      }
      const auto attachIt = scriptIt->find("attachments");
      if (attachIt == scriptIt->end() || !attachIt->is_array() ||
          attachIt->empty())
      {
        continue;
      }
      const auto nameIt = compsIt->find("name");
      if (nameIt == compsIt->end())
      {
        continue;
      }
      const auto valueIt = nameIt->find("value");
      if (valueIt == nameIt->end() || !valueIt->is_string())
      {
        continue;
      }
      std::vector<std::string> classes;
      for (const auto &att : *attachIt)
      {
        const auto classIt = att.find("className");
        if (classIt != att.end() && classIt->is_string())
        {
          classes.push_back(classIt->get<std::string>());
        }
      }
      if (classes.empty())
      {
        continue;
      }
      scriptEntities_.push_back(valueIt->get<std::string>());
      entityAttachmentClasses_.push_back(std::move(classes));
    }
    // Sort entities alphabetically while keeping the classes[i] lockstep with
    // scriptEntities_[i] so the Attachment Class dropdown stays in sync.
    std::vector<std::size_t> order(scriptEntities_.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b)
              { return scriptEntities_[a] < scriptEntities_[b]; });
    std::vector<std::string> sortedEnts;
    std::vector<std::vector<std::string>> sortedClasses;
    sortedEnts.reserve(order.size());
    sortedClasses.reserve(order.size());
    for (auto idx : order)
    {
      sortedEnts.push_back(std::move(scriptEntities_[idx]));
      sortedClasses.push_back(std::move(entityAttachmentClasses_[idx]));
    }
    scriptEntities_ = std::move(sortedEnts);
    entityAttachmentClasses_ = std::move(sortedClasses);
    if (!scriptEntities_.empty())
    {
      selectedEntityIdx_ = 0;
      selectedClassIdx_ = 0;
    }
  }

  void NeuralTrainingPlugin::refresh_policies(const std::filesystem::path &workspacePath)
  {
    policyFiles_.clear();
    const auto dir = run_dir(workspacePath, runName_);
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
    {
      return;
    }
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
    {
      if (entry.is_regular_file(ec))
      {
        policyFiles_.push_back(entry.path().filename().string());
      }
    }
    std::sort(policyFiles_.begin(), policyFiles_.end());
  }

  void NeuralTrainingPlugin::start_training(const std::filesystem::path &workspacePath)
  {
    lastError_.clear();
    lastInfo_.clear();

    if (selectedWorldIdx_ < 0 ||
        selectedWorldIdx_ >= static_cast<int>(worldNames_.size()))
    {
      lastError_ = "Pick a saved world before starting.";
      return;
    }
    if (entityName_[0] == '\0' || attachmentClass_[0] == '\0')
    {
      lastError_ = "Subject entity and attachment class must be set.";
      return;
    }

    // Fresh run folder; if it exists we append artifacts into it.
    const auto runPath = run_dir(workspacePath, runName_);
    std::error_code ec;
    std::filesystem::create_directories(runPath, ec);
    if (ec)
    {
      lastError_ = "Failed to create run folder: " + ec.message();
      return;
    }
    config_.checkpoint_dir = runPath.string();

    // Build a fresh trainer. `unique_ptr` destructor joins the thread cleanly.
    trainer_ = std::make_unique<hne::Trainer>(config_);

    HadesScriptEnv::Config envCfg;
    envCfg.workspacePath = workspacePath;
    envCfg.worldName = worldNames_[selectedWorldIdx_];
    envCfg.subjects.push_back(TrainingSubject{std::string(entityName_),
                                              std::string(attachmentClass_)});
    envCfg.sharedPolicies = sharedPolicies_.get();

    trainer_->set_environment_factory(
        [envCfg]() -> std::unique_ptr<hne::IEnvironment>
        {
          return std::make_unique<HadesScriptEnv>(envCfg);
        });

    // Route metrics + eval rewards into our bounded buffers.
    auto cb = std::make_shared<hne::LambdaCallback>();
    cb->on_update_fn = [this](const hne::TrainingMetrics &m)
    {
      std::lock_guard<std::mutex> lock(metricsMutex_);
      history_.push_back(m);
      while (history_.size() > kMaxHistory)
      {
        history_.pop_front();
      }
    };
    cb->on_evaluation_fn = [this](float meanReward, float /*meanLength*/)
    {
      std::lock_guard<std::mutex> lock(metricsMutex_);
      evalRewards_.push_back(meanReward);
      while (evalRewards_.size() > kMaxHistory)
      {
        evalRewards_.pop_front();
      }
    };
    trainer_->add_callback(cb);

#ifdef HADES_HAS_HNE_WANDB
    // If the user Start-ed a wandb run from the panel above, forward every
    // metric/eval/checkpoint to it. The callback lifetime is tied to
    // wandbState_; it survives across retrained runs until the user stops
    // it from the panel.
    if (wandbState_.callback)
    {
      trainer_->add_callback(wandbState_.callback);
    }
#endif

    // Clear the UI buffers for the new run.
    {
      std::lock_guard<std::mutex> lock(metricsMutex_);
      history_.clear();
      evalRewards_.clear();
    }

    trainer_->train_async();
    lastInfo_ = "Training started in " + runPath.string();
  }

  void NeuralTrainingPlugin::stop_training()
  {
    if (!trainer_)
    {
      return;
    }
    trainer_->request_stop();
    // Destructor joins the background thread.
    trainer_.reset();
  }

  void NeuralTrainingPlugin::save_checkpoint(const std::filesystem::path &workspacePath)
  {
    if (!trainer_)
    {
      lastError_ = "No active trainer. Start training first.";
      return;
    }
    const auto path = run_dir(workspacePath, runName_) / "trainer.ckpt";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (trainer_->save_checkpoint(path.string()))
    {
      lastInfo_ = "Checkpoint saved to " + path.string();
      refresh_policies(workspacePath);
    }
    else
    {
      lastError_ = "save_checkpoint() failed.";
    }
  }

  void NeuralTrainingPlugin::export_policy(const std::filesystem::path &workspacePath)
  {
    if (!trainer_)
    {
      lastError_ = "No active trainer. Start training first.";
      return;
    }
    const auto path = run_dir(workspacePath, runName_) / "policy.pt";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (!trainer_->export_policy(path.string()))
    {
      lastError_ = "export_policy() failed.";
      return;
    }

    // Sidecar metadata — lets `PolicyRegistry::get_validated` cross-check the
    // runtime's reported specs, and future resume-from-checkpoint workflows
    // don't need to reconstruct the trainer config by hand.
    nlohmann::json meta;
    meta["exported_at"] = iso_timestamp_now();
    meta["trainer_config"] = config_;
    meta["run_name"] = std::string(runName_);
    meta["world"] = (selectedWorldIdx_ >= 0)
                        ? worldNames_[selectedWorldIdx_]
                        : std::string{};
    meta["subject_entity"] = std::string(entityName_);
    meta["attachment_class"] = std::string(attachmentClass_);

    const auto metaPath = run_dir(workspacePath, runName_) / "policy.meta.json";
    std::ofstream out(metaPath);
    if (out)
    {
      out << meta.dump(2);
    }

    lastInfo_ = "Policy exported to " + path.string();
    refresh_policies(workspacePath);
  }

  HADES_REGISTER_EDITOR_PLUGIN(NeuralTrainingPlugin, 60)
}

#endif // HADES_HAS_HNE_TRAINING
