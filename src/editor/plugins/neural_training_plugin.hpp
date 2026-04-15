#ifndef HADES_EDITOR_PLUGINS_NEURAL_TRAINING_PLUGIN_HPP
#define HADES_EDITOR_PLUGINS_NEURAL_TRAINING_PLUGIN_HPP

#ifdef HADES_HAS_HNE_TRAINING

#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <hne/training/trainer.hpp>
#include <hne/training/trainer_config.hpp>
#include <hne/training/metrics.hpp>

#ifdef HADES_HAS_HNE_WANDB
#include <hne/imgui/wandb_panel.hpp>
#include <hne/wandb/callback.hpp>
#endif

#include "editor_plugin.hpp"

namespace hades
{
  class PolicyRegistry;

  /// Editor panel that drives PPO training of a NeuralScript attached to a
  /// saved world. Owns a background `hne::Trainer` and a shared
  /// `PolicyRegistry` so parallel envs reuse loaded policies. Metrics are
  /// fed through a lambda callback into a bounded mutex-guarded buffer;
  /// the UI thread drains and renders via `hne::imgui` widgets.
  class NeuralTrainingPlugin : public EditorPlugin
  {
  public:
    NeuralTrainingPlugin();
    ~NeuralTrainingPlugin() override;

    std::string_view id() const override { return "neural-training"; }
    std::string_view display_name() const override { return "Neural Training"; }
    int order() const override { return 60; }

    bool visible(const Editor &editor) const override
    {
      (void)editor;
      return visible_;
    }

    void set_visible(Editor &editor, bool visible) override
    {
      (void)editor;
      visible_ = visible;
      if (visible)
      {
        focusRequested_ = true;
      }
    }

    void activate(Editor &editor) override
    {
      set_visible(editor, true);
    }

    void render(EditorPluginContext &context) override;

  private:
    void draw_panel(EditorPluginContext &context);
    void refresh_worlds(const std::filesystem::path &workspacePath);
    void poll_worlds_dir(const std::filesystem::path &workspacePath);
    void refresh_script_entities(const std::filesystem::path &workspacePath);
    void refresh_policies(const std::filesystem::path &workspacePath);
    void refresh_preview_policies(const std::filesystem::path &workspacePath);
    void start_preview(EditorPluginContext &context);
    void start_training(const std::filesystem::path &workspacePath);
    void stop_training();
    void save_checkpoint(const std::filesystem::path &workspacePath);
    void export_policy(const std::filesystem::path &workspacePath);

    bool visible_ = false;
    bool focusRequested_ = false;

    // UI inputs
    char runName_[64] = "run1";
    char entityName_[128] = "";
    char attachmentClass_[128] = "";

    // Cached directory listings
    std::vector<std::string> worldNames_;
    int selectedWorldIdx_ = -1;
    std::vector<std::string> policyFiles_;

    // Auto-refresh of worlds — re-listed when the worlds directory mtime
    // changes so newly saved worlds show up without a manual button.
    std::filesystem::file_time_type worldsMtime_{};

    // Entities in the selected world that have at least one script attached.
    // Refreshed when the selected world changes.
    std::vector<std::string> scriptEntities_;
    // Parallel to scriptEntities_: the attachment className list for each entity,
    // so the Attachment Class dropdown doesn't need the user to remember/type them.
    std::vector<std::vector<std::string>> entityAttachmentClasses_;
    int selectedEntityIdx_ = -1;
    int selectedClassIdx_ = -1;
    int loadedEntitiesWorldIdx_ = -2;

    // Trainer + config
    hne::TrainerConfig config_;
    std::unique_ptr<hne::Trainer> trainer_;
    std::unique_ptr<PolicyRegistry> sharedPolicies_;

    // Metrics buffer — populated by trainer callback thread, drained by UI.
    mutable std::mutex metricsMutex_;
    std::deque<hne::TrainingMetrics> history_;
    std::deque<float> evalRewards_;

    // Policy Preview: scans the workspace's .hades/policies tree for any
    // `.pt` exports and lets the user pick one to drive the selected
    // NeuralScript attachment in live play mode (free-fly editor camera).
    std::vector<std::filesystem::path> previewPolicies_;
    std::vector<std::string> previewPolicyLabels_;
    int selectedPreviewPolicyIdx_ = -1;

    std::string lastError_;
    std::string lastInfo_;

#ifdef HADES_HAS_HNE_WANDB
    // Persistent wandb panel state. The callback (once created by the user
    // clicking "Start" in the panel) is attached to the next trainer started
    // and kept alive for the run duration.
    hne::imgui::WandbPanelState wandbState_;
#endif
  };
}

#endif // HADES_HAS_HNE_TRAINING
#endif
