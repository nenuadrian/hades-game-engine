#ifndef HADES_EDITOR_PLUGINS_ANIMATION_EDITOR_PLUGIN_HPP
#define HADES_EDITOR_PLUGINS_ANIMATION_EDITOR_PLUGIN_HPP

#include <array>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "../../engine/animation/animation_clip.hpp"
#include "../../engine/animation/rig_asset.hpp"
#include "../../engine/animation/skeleton.hpp"
#include "../../engine/core/ecs/entity.hpp"
#include "../animation_timeline.hpp"
#include "editor_plugin.hpp"

namespace hades
{
  class ModelAsset;

  /// The Animation panel: rig a model, author clips against its skeleton,
  /// and preview them live in the viewport.
  ///
  /// Three tabs share one target (an entity with a ModelComponent, or a model
  /// picked straight from the workspace):
  ///   Animate — skeleton tree, dope sheet, curve editor, transport, events.
  ///   Rig     — create/parent/delete joints, edit rest transforms, bind
  ///             meshes with automatic skin weights.
  ///   Clips   — browse, create, duplicate, rename and delete clip assets,
  ///             and bake an imported animation into an editable clip.
  ///
  /// Rendered at plugin order 20 so it runs BEFORE the World panel (40): a
  /// scrub published this frame is drawn in the same frame, not the next one.
  class AnimationEditorPlugin : public EditorPlugin
  {
  public:
    AnimationEditorPlugin();
    ~AnimationEditorPlugin() override;

    std::string_view id() const override { return "animation-editor"; }
    std::string_view display_name() const override { return "Animation"; }
    int order() const override { return 20; }

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

    void activate(Editor &editor) override { set_visible(editor, true); }

    void render(EditorPluginContext &context) override;

    // ---- Exposed for the headless smoke test ------------------------------
    // src/tools/animation_smoke.cpp renders this panel against Dear ImGui's
    // null backend, where no widget has a knowable screen rectangle. Every
    // mode this panel can draw in sits behind a click, so without a direct
    // way in the only branch CI ever draws is the default view. Nothing in
    // the editor calls any of these.

    /// Timeline view state: the play head, the key selection the curve
    /// editor plots, and the curve-editor toggle itself.
    AnimationTimelineState &timeline_state() { return timeline_; }
    void set_playing(bool playing) { playing_ = playing; }
    void open_clip(EditorPluginContext &context, const std::string &name) { load_clip(context, name); }

    /// 0 Animate, 1 Rig, 2 Clips — whichever tab last drew.
    int active_tab() const { return activeTab_; }
    bool clip_is_loaded() const { return clipLoaded_; }
    int selected_joint() const { return selectedJoint_; }
    std::size_t skeleton_joint_count() const { return skeleton_.size(); }
    std::size_t clip_key_count() const { return clip_.total_key_count(); }
    const std::vector<TimelineRow> &timeline_rows() const { return rows_; }
    std::size_t undo_depth() const { return undoStack_.size(); }
    /// Label of the most recent undo entry, or empty. Each editing surface
    /// labels its own edits, which is how the smoke test tells an edit made
    /// through the curve editor from one made through the dope sheet.
    std::string last_undo_label() const
    {
      return undoStack_.empty() ? std::string() : undoStack_.back().label;
    }

  private:
    // ---- Frame flow ------------------------------------------------------

    void draw_panel(EditorPluginContext &context);
    void draw_target_bar(EditorPluginContext &context);
    void draw_animate_tab(EditorPluginContext &context, const ModelAsset &asset);
    void draw_rig_tab(EditorPluginContext &context, const ModelAsset &asset);
    void draw_clips_tab(EditorPluginContext &context, const ModelAsset &asset);
    void draw_skeleton_tree(const ModelAsset &asset);
    void draw_skeleton_tree_node(int joint);
    void draw_transport(const ModelAsset &asset);
    void draw_key_inspector();
    void draw_events_editor();
    void draw_clip_properties();
    void draw_overlay_options();
    void draw_status_lines();
    void draw_clip_dialogs(EditorPluginContext &context, const ModelAsset &asset);

    /// Point AnimationClipCache at the workspace, and drop everything that
    /// belonged to the previous one. Plugins outlive a workspace switch, so
    /// without this the working clip and clip list of project A stay live in
    /// project B and Save writes A's data over B's assets.
    void sync_asset_roots(EditorPluginContext &context);
    /// Rebuild the skeleton view from the asset and clamp any index that a
    /// re-import may have invalidated.
    void sync_skeleton(const ModelAsset &asset);
    /// Undo/redo, live only while this panel's window tree has focus.
    void handle_shortcuts();
    void handle_timeline_result(const AnimationTimelineResult &result, const ModelAsset &asset);
    void refresh_model_list(EditorPluginContext &context);
    bool subtree_matches_filter(int joint) const;

    /// Resolve the model behind the current target, or nullptr. Never cached
    /// across frames: ModelAssetCache::clear() runs at the top of every
    /// editor frame on a workspace change.
    const ModelAsset *resolve_target_model(EditorPluginContext &context);
    void set_target_entity(EditorPluginContext &context, Entity::EntityId entity);
    /// Put the targeted workspace model into the open world so there is
    /// something for the viewport to pose. Previewing needs a real entity:
    /// the renderer draws entities, not asset files.
    void spawn_preview_entity(EditorPluginContext &context);
    void clear_target();

    // ---- Preview ---------------------------------------------------------

    /// Sample the working clip at the play head, publish the palette to
    /// AnimationRuntime so the viewport draws it, and publish joint globals
    /// to the shared AnimationEditState for the skeleton overlay.
    void publish_preview(const ModelAsset &asset);
    void advance_playback(float deltaTime, const ModelAsset &asset);
    /// Consume a joint pick or gizmo edit the viewport published this frame.
    void consume_viewport_input(const ModelAsset &asset);

    /// Drop the published palette and overlay. Idempotent, and called from
    /// every path that stops rendering the panel — a preview left behind
    /// would freeze the character for the rest of the session.
    void release_preview();
    /// Resample the clip at the play head into `previewPose_`, then lay the
    /// manual pose override on top.
    void update_preview_pose();
    /// The clip's pose at an arbitrary time, without the override.
    Pose pose_at(float time) const;
    /// Route a pose edit (inspector field or viewport gizmo) into the
    /// override, the clip (with auto-key) and the undo stack.
    void apply_joint_edit(int joint, const math::Vec3 &translation, const math::Quat &rotation,
                          const math::Vec3 &scale, bool finished);
    void clear_pose_override();
    void insert_key_from_row(int row, float time);
    int current_frame() const;
    int frame_count() const;
    void set_frame(int frame);

    // ---- Clip editing ----------------------------------------------------

    void new_clip(EditorPluginContext &context, const ModelAsset &asset);
    void load_clip(EditorPluginContext &context, const std::string &name);
    void save_clip(EditorPluginContext &context);
    void save_clip_as(EditorPluginContext &context, const std::string &name);
    void bake_imported_clip(EditorPluginContext &context, const ModelAsset &asset, int clipIndex);
    void key_selected_joint(float time);
    void key_whole_pose(float time);
    /// True when `index` is the joint `Skeleton::find` resolves its own name
    /// to. Clip tracks bind by joint NAME, so a later joint sharing a name
    /// with an earlier one cannot own a track: keying it would overwrite the
    /// first one's track and animate a joint the user never touched. Every
    /// index-to-name write in the panel has to ask this first.
    bool joint_is_keyable(std::size_t index) const;
    void delete_selected_keys();
    void refresh_clip_list(EditorPluginContext &context);

    // ---- Undo ------------------------------------------------------------

    /// Snapshot-based: the clip is small enough that serialising it is far
    /// cheaper than a command pattern, and it cannot drift out of sync.
    void push_undo(const std::string &label);
    /// For edits the timeline widget already applied: pushes the baseline
    /// captured at the last snapshot, so one drag becomes one undo entry
    /// instead of one per mouse-move.
    void push_undo_recorded(const std::string &label);
    void undo();
    void redo();
    void mark_dirty();
    void reset_undo();
    /// Re-take the baseline once the frame's edits have landed and no drag
    /// is still running. push_undo() snapshots *before* its mutation, so
    /// without this the baseline stays one edit behind.
    void refresh_undo_baseline();

    struct UndoEntry
    {
      std::string label;
      nlohmann::json clip;
    };

    // ---- Rigging ---------------------------------------------------------

    void load_or_seed_rig(EditorPluginContext &context, const ModelAsset &asset);
    void save_rig(EditorPluginContext &context);
    void add_joint(const ModelAsset &asset);
    void delete_joint(int rigJoint);
    void reparent_joint(int rigJoint, const std::string &newParent);
    void auto_weight(EditorPluginContext &context, const ModelAsset &asset);
    void rename_rig_joint(int rigJoint, const std::string &newName);
    /// True when `candidate` sits under `ancestor` in the rig hierarchy —
    /// the guard that keeps reparenting from making a cycle.
    bool rig_joint_is_descendant(int candidate, int ancestor) const;
    /// Palette entries `apply_rig` will need for `rig_` as it stands: the
    /// quantity it refuses on, which is NOT the joint count. A joint whose
    /// name is an imported node re-points the palette entry that node
    /// already owns, and a joint nothing is weighted to gets no entry at
    /// all, so a seeded rig on a 64-bone character costs nothing extra.
    /// Counted against the raw import (`ModelAssetCache::importedBoneCount`)
    /// because `asset` already carries whatever rig is on disk.
    std::size_t rig_required_bones(const ModelAsset &asset) const;

    // ---- State -----------------------------------------------------------

    bool visible_ = false;
    bool focusRequested_ = false;

    /// Target: an entity when one is selected, otherwise a model path picked
    /// from the workspace.
    std::optional<Entity::EntityId> targetEntity_;
    std::string targetModelPath_;
    bool followSelection_ = true;

    Skeleton skeleton_;
    /// Asset `skeleton_` was built from. Rebuilding it every frame costs 810
    /// allocations at 200 joints and repairs nothing, so it is rebuilt only
    /// when the asset actually changes underneath the panel. Compared with
    /// the node count as well, because the cache can drop an asset and
    /// re-import the replacement onto the same address.
    const ModelAsset *skeletonSource_ = nullptr;
    std::size_t skeletonNodeCount_ = 0;
    int selectedJoint_ = -1;
    std::string jointFilter_;
    std::array<char, 128> jointFilterBuffer_{};

    AnimationClipAsset clip_;
    std::string clipName_;
    bool clipLoaded_ = false;
    bool clipDirty_ = false;
    std::vector<std::string> clipList_;
    std::vector<TimelineRow> rows_;
    AnimationTimelineState timeline_;

    /// The pose the play head produces, kept so gizmo edits start from it.
    Pose previewPose_;
    std::vector<math::Mat4> previewGlobals_;
    std::vector<math::Mat4> previewPalette_;

    bool playing_ = false;
    bool loopPlayback_ = true;
    float playbackSpeed_ = 1.0f;
    bool autoKey_ = true;
    bool showOnlySelectedTrack_ = false;

    std::deque<UndoEntry> undoStack_;
    std::deque<UndoEntry> redoStack_;

    RigAsset rig_;
    bool rigLoaded_ = false;
    bool rigDirty_ = false;
    int selectedRigJoint_ = -1;
    AutoWeightMode weightMode_ = AutoWeightMode::Envelope;
    float weightFalloff_ = 0.5f;
    int weightInfluences_ = 4;

    std::array<char, 128> newClipNameBuffer_{};
    std::array<char, 128> newJointNameBuffer_{};
    std::string statusMessage_;
    std::string errorMessage_;
    int activeTab_ = 0;

    // ---- Preview bookkeeping ---------------------------------------------

    /// Entity the published palette belongs to, so a target switch clears the
    /// old one rather than leaving it frozen mid-scrub.
    Entity::EntityId previewEntity_ = Entity::INVALID;
    bool previewPublished_ = false;
    /// Model path the per-target state (rig, selection, override) was built
    /// for; a mismatch means the target moved and the state must be reset.
    std::string boundModelPath_;

    /// A hand-posed override laid over the sampled pose, so posing without
    /// auto-key survives until the play head moves. Empty mask == no
    /// override.
    Pose overridePose_;
    std::vector<bool> overrideMask_;
    bool hasOverride_ = false;
    float overrideTime_ = 0.0f;

    /// Clip state at the last undo snapshot. See push_undo_recorded().
    nlohmann::json undoBaseline_;
    /// Set while `undoBaseline_` is older than the clip, i.e. between an
    /// entry being pushed and the frame settling. It doubles as "an entry
    /// for the gesture in progress is already open", which is what keeps a
    /// curve drag to one undo entry instead of one per mouse-move.
    bool undoBaselineStale_ = false;

    // ---- Browsing --------------------------------------------------------

    std::vector<Entity::EntityId> targetCandidates_;
    std::vector<std::string> modelFiles_;
    std::filesystem::path scannedWorkspace_;
    /// Workspace the panel's clip/rig state belongs to. Owned by the panel
    /// rather than sampled from AnimationClipCache, because the editor
    /// re-roots that cache before any plugin renders — by the time
    /// sync_asset_roots runs the root already matches and a comparison
    /// against it can never notice the switch.
    std::filesystem::path configuredWorkspace_;
    bool clipListLoaded_ = false;

    // ---- Dialogs ---------------------------------------------------------

    bool openNewClipPopup_ = false;
    /// The New Clip dialog is also the "name this unsaved clip" dialog; when
    /// this is set it saves the working clip instead of replacing it.
    bool saveWorkingClipOnCreate_ = false;
    bool openRenameClipPopup_ = false;
    bool openDeleteClipPopup_ = false;
    bool openSwitchClipPopup_ = false;
    /// Clip the user asked to load while the working copy was dirty.
    std::string pendingClipSwitch_;
    std::string pendingClipTarget_;
    std::array<char, 128> renameClipBuffer_{};
    std::array<char, 128> jointRenameBuffer_{};
    std::string clipListError_;
    std::string rigError_;
    int selectedMeshIndex_ = 0;
  };
}

#endif
