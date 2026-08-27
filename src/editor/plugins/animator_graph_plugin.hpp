#ifndef HADES_EDITOR_PLUGINS_ANIMATOR_GRAPH_PLUGIN_HPP
#define HADES_EDITOR_PLUGINS_ANIMATOR_GRAPH_PLUGIN_HPP

#include <array>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

#include "../../engine/animation/animator_graph.hpp"
#include "../../engine/animation/animator_instance.hpp"
#include "../../engine/core/ecs/entity.hpp"
#include "editor_plugin.hpp"

/// Declared here so the elaborated `struct ImVec2` below always resolves to
/// Dear ImGui's type, whatever include order a translation unit uses.
struct ImVec2;

namespace hades
{
  /// The Animator panel: a state-machine canvas over an AnimatorGraph asset.
  ///
  /// States are draggable nodes, transitions are arrows between them, and the
  /// left rail holds the parameters gameplay code drives. During play mode
  /// the active state and running transition are highlighted live, which is
  /// the fastest way to debug why a character is stuck in a state.
  class AnimatorGraphPlugin : public EditorPlugin
  {
  public:
    std::string_view id() const override { return "animator-graph"; }
    std::string_view display_name() const override { return "Animator"; }
    int order() const override { return 45; }

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

    ~AnimatorGraphPlugin() override;

    void render(EditorPluginContext &context) override;

    // ---- Exposed for the headless smoke test ------------------------------
    // See the matching block in animation_editor_plugin.hpp: the graph combo
    // is the only way into a loaded graph, and a null-backend harness cannot
    // click it. The rest is read-only, so the smoke test can assert that
    // canvas hit-testing really reaches nodes rather than trusting that a
    // frame which drew without asserting was also interactive.

    void open_graph(EditorPluginContext &context, const std::string &name) { load_graph(context, name); }

    bool graph_is_loaded() const { return graphLoaded_; }
    int selected_state() const { return selectedState_; }
    int selected_transition() const { return selectedTransition_; }
    /// True while the canvas is mirroring a live animator: the active state
    /// highlight, its progress bar and the running-transition colour.
    bool debug_overlay_active() const { return debugAvailable_; }
    /// The live animator's layer state, exactly as the canvas read it this
    /// frame. The smoke test needs these to tell "the overlay was available"
    /// from "the overlay actually matched a node and drew its progress bar",
    /// which are different branches and have failed independently.
    const std::string &debug_state_name() const { return debugStateName_; }
    float debug_normalized_time() const { return debugNormalizedTime_; }
    bool debug_transitioning() const { return debugTransitioning_; }
    /// Parameter rows the rail drew from the running instance instead of from
    /// the authored graph, on the last frame.
    int live_parameter_rows() const { return liveParameterRows_; }
    /// True while the panel is driving its own animator over an entity in
    /// edit mode. Distinct from debug_overlay_active(), which reports the
    /// play-mode instance: the two feed the same canvas highlight and have to
    /// be separable in a test.
    bool preview_active() const { return publishedPreviewEntity_ != Entity::INVALID; }
    void set_preview_enabled(bool enabled) { previewEnabled_ = enabled; }
    std::string preview_state_name() const { return previewInstance_.current_state(activeLayer_); }
    float preview_normalized_time() const { return previewInstance_.normalized_time(activeLayer_); }
    std::size_t state_node_count() const { return nodeRects_.size(); }
    std::size_t undo_depth() const { return undoStack_.size(); }
    std::size_t redo_depth() const { return redoStack_.size(); }

    /// Authored position of state `index` in the active layer. Read from the
    /// graph model rather than from the canvas, so an undo that fails to
    /// restore the model cannot hide behind a stale rectangle.
    bool state_position(int index, float &outX, float &outY) const
    {
      if (activeLayer_ < 0 || static_cast<std::size_t>(activeLayer_) >= graph_.layers.size())
      {
        return false;
      }
      const AnimLayer &layer = graph_.layers[static_cast<std::size_t>(activeLayer_)];
      if (index < 0 || static_cast<std::size_t>(index) >= layer.states.size())
      {
        return false;
      }
      outX = layer.states[static_cast<std::size_t>(index)].x;
      outY = layer.states[static_cast<std::size_t>(index)].y;
      return true;
    }

    /// Screen-space centre of state node `index` as of the last frame drawn.
    /// False when the index addresses no node.
    bool state_node_centre(int index, float &outX, float &outY) const
    {
      if (index < 0 || static_cast<std::size_t>(index) >= nodeRects_.size())
      {
        return false;
      }
      const NodeRect &rect = nodeRects_[static_cast<std::size_t>(index)];
      outX = (rect.minX + rect.maxX) * 0.5f;
      outY = (rect.minY + rect.maxY) * 0.5f;
      return true;
    }

  private:
    void draw_panel(EditorPluginContext &context);
    void draw_toolbar(EditorPluginContext &context);
    void draw_parameters(EditorPluginContext &context);
    void draw_canvas(EditorPluginContext &context);
    void draw_state_node(int stateIndex, const struct ImVec2 &canvasOrigin);
    void draw_transitions(const struct ImVec2 &canvasOrigin);
    void draw_details(EditorPluginContext &context);
    void draw_state_details(AnimState &state, EditorPluginContext &context);
    void draw_transition_details(AnimTransition &transition);

    void refresh_graph_list(EditorPluginContext &context);
    void load_graph(EditorPluginContext &context, const std::string &name);
    void save_graph(EditorPluginContext &context);
    void new_graph(EditorPluginContext &context, const std::string &name);
    void add_state(float x, float y);
    void delete_state(int stateIndex);
    void begin_transition(int fromState);
    void complete_transition(int toState);
    void delete_transition(int transitionIndex);
    void push_undo(const std::string &label);
    void undo();
    void redo();
    /// Forget both histories. Every path that swaps the open graph out from
    /// under the panel has to call this: an entry restored into a different
    /// graph would resurrect states that never belonged to it.
    void reset_history();

    AnimLayer *active_layer();
    /// Entity whose animator is being debugged during play, or INVALID.
    Entity::EntityId debug_entity(EditorPluginContext &context) const;

    /// Point the shared clip cache at the editor workspace. Idempotent, and
    /// cheap enough to call every frame.
    void sync_asset_root(EditorPluginContext &context);
    /// Re-list graphs and clips when the asset directories change on disk.
    void poll_asset_lists(EditorPluginContext &context);
    /// Drop selections that no longer address anything. Called every frame
    /// because states and transitions are deleted mid-draw.
    void clamp_selection();
    /// Rename a parameter and every reference to it, so conditions and blend
    /// trees survive the edit.
    void rename_parameter(int index, const std::string &newName);
    /// Structural edits queued while the canvas was iterating its own nodes.
    void apply_pending_canvas_actions();
    void add_state_of_kind(float x, float y, AnimStateKind kind);
    void capture_debug_state(EditorPluginContext &context);

    // ---- Edit-mode preview -------------------------------------------------

    /// Advance the panel's own animator by one frame and publish its palette,
    /// or tear the preview down when it cannot run.
    ///
    /// This is what makes the graph mean something before play mode: without
    /// it a state machine is a diagram, and the only way to see a transition
    /// fire is to press Play, select the right entity and hope.
    void update_preview(EditorPluginContext &context);
    /// Drop the published palette and the staged graph. Idempotent, and
    /// called from every path that stops rendering the panel.
    void release_preview();
    void draw_preview_bar(EditorPluginContext &context);
    /// Resolve "follow selection", drop a target that has gone away, and fall
    /// back to the first model in the world.
    void resolve_preview_target(EditorPluginContext &context);
    /// Re-list the animation inside the target character's model, so a state
    /// can name one without it being baked into `.hades/animations` first.
    void refresh_imported_clip_list(EditorPluginContext &context);
    /// Point `entity`'s AnimatorComponent at the open graph, adding the
    /// component when it has none.
    void assign_graph_to_entity(EditorPluginContext &context, Entity::EntityId entity);
    /// The instance the parameter rail and the canvas highlight read: the
    /// live one in play mode, the panel's own while previewing, or null.
    AnimatorInstance *active_instance(EditorPluginContext &context);

    struct UndoEntry
    {
      std::string label;
      nlohmann::json graph;
    };

    /// Screen-space rectangle of one state node, rebuilt every frame. Kept as
    /// plain floats so the header needs no ImGui types.
    struct NodeRect
    {
      float minX = 0.0f;
      float minY = 0.0f;
      float maxX = 0.0f;
      float maxY = 0.0f;
    };

    bool visible_ = false;
    bool focusRequested_ = false;

    AnimatorGraph graph_;
    std::string graphName_;
    bool graphLoaded_ = false;
    bool graphDirty_ = false;
    std::vector<std::string> graphList_;
    std::vector<std::string> clipList_;
    /// "model.fbx#Walk" references for the target character's own model, and
    /// the model path they were listed for.
    std::vector<std::string> importedClipList_;
    std::string importedClipModel_;

    int activeLayer_ = 0;
    int selectedState_ = -1;
    int selectedTransition_ = -1;
    int draggingState_ = -1;
    int pendingTransitionFrom_ = -1;
    float panX_ = 0.0f;
    float panY_ = 0.0f;
    float zoom_ = 1.0f;

    std::deque<UndoEntry> undoStack_;
    std::deque<UndoEntry> redoStack_;

    std::array<char, 128> newGraphNameBuffer_{};
    std::array<char, 128> newParameterNameBuffer_{};
    int newParameterType_ = 0;
    std::string statusMessage_;
    std::string errorMessage_;
    std::vector<std::string> validationProblems_;

    // ---- Canvas working state ---------------------------------------------

    std::vector<NodeRect> nodeRects_;
    int hoveredTransition_ = -1;
    /// True while a right-drag is panning, so releasing does not also open
    /// the canvas context menu.
    bool rightDragPanned_ = false;

    // Structural edits are deferred: a context menu can ask to delete the very
    // node whose rectangle the canvas loop is still walking.
    int pendingDeleteState_ = -1;
    int pendingDuplicateState_ = -1;
    int pendingDefaultState_ = -1;
    int pendingDeleteTransition_ = -1;
    bool pendingAddState_ = false;
    AnimStateKind pendingAddKind_ = AnimStateKind::Clip;
    float pendingAddX_ = 0.0f;
    float pendingAddY_ = 0.0f;
    bool pendingPaste_ = false;

    AnimState clipboardState_;
    bool clipboardValid_ = false;

    // ---- Edit-mode preview -------------------------------------------------

    bool previewEnabled_ = false;
    bool previewFollowSelection_ = true;
    bool previewPlaying_ = true;
    float previewSpeed_ = 1.0f;
    Entity::EntityId previewEntity_ = Entity::INVALID;
    /// Entity the published palette belongs to, so a target switch clears the
    /// old one instead of leaving it frozen mid-blend.
    Entity::EntityId publishedPreviewEntity_ = Entity::INVALID;
    std::vector<Entity::EntityId> previewCandidates_;
    std::string previewError_;
    /// The panel's own player, deliberately NOT one of AnimationRuntime's:
    /// those belong to play mode and carry the state a running game built up,
    /// and driving one from the editor would hand the game back a character
    /// mid-preview.
    AnimatorInstance previewInstance_;
    bool previewInstanceBound_ = false;

    // ---- Dialogs and buffers ----------------------------------------------

    bool openNewGraphPopup_ = false;
    bool openDeleteGraphPopup_ = false;
    std::array<char, 2048> maskBonesBuffer_{};
    int maskBufferLayer_ = -1;

    // ---- Play-mode debug snapshot, captured once per frame ----------------

    bool debugAvailable_ = false;
    std::string debugStateName_;
    float debugNormalizedTime_ = 0.0f;
    bool debugTransitioning_ = false;
    /// Parameter rows the rail last drew against the live instance. Recorded
    /// so the headless test can prove the live-value path ran rather than
    /// inferring it from the overlay flag.
    int liveParameterRows_ = 0;

    // ---- Asset list polling ------------------------------------------------

    float listPollAccumulator_ = 0.0f;
    std::filesystem::path listedRoot_;
    std::filesystem::file_time_type graphsStamp_{};
    std::filesystem::file_time_type clipsStamp_{};
  };
}

#endif
