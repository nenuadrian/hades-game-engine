#ifndef HADES_EDITOR_BLUEPRINT_BLUEPRINT_EDITOR_PANEL_HPP
#define HADES_EDITOR_BLUEPRINT_BLUEPRINT_EDITOR_PANEL_HPP

#include <array>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "imgui.h"

#include "../../engine/blueprint/blueprint_compiler.hpp"
#include "../../engine/blueprint/blueprint_graph.hpp"
#include "../../engine/blueprint/blueprint_node_registry.hpp"
#include "../plugins/editor_plugin.hpp"

namespace hades
{
  inline constexpr const char *kBlueprintEditorPluginId = "blueprint-editor";

  /// Ask the Blueprint editor to open this workspace-relative asset on its
  /// next frame. The workspace tree and the inspector both use this; pair it
  /// with `Editor::show_plugin(kBlueprintEditorPluginId)` to raise the panel.
  void request_blueprint_editor_open(std::string relativePath);

  /// A node graph editor in the spirit of Unreal's Blueprint editor: a
  /// pan/zoom canvas of nodes wired by execution and data pins, a variables
  /// and functions sidebar, an inline details panel, and a compile log.
  class BlueprintEditorPlugin final : public EditorPlugin
  {
  public:
    std::string_view id() const override { return kBlueprintEditorPluginId; }
    std::string_view display_name() const override { return "Blueprint Editor"; }
    int order() const override { return 55; }

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

    /// Nodes selected on the active graph. Exposed so the headless editor
    /// smoke test can assert that canvas hit-testing actually reaches nodes —
    /// ImGui silently drops overlapping items, so "it drew without asserting"
    /// is not evidence that the canvas is interactive.
    std::size_t selected_node_count() const { return selection_.size(); }

    /// Screen-space rectangle the graph canvas occupied on the last rendered
    /// frame, so the smoke test can aim synthetic clicks at the canvas rather
    /// than at the surrounding sidebar and details panels.
    ImVec2 canvas_origin() const { return canvasOrigin_; }
    ImVec2 canvas_size() const { return canvasSize_; }

    /// Ids of the nodes on the event graph, paired with the public
    /// `select_only` below. The smoke test walks them so every node's
    /// details-panel editor is rendered at least once; synthetic clicks only
    /// reach whatever the probe grid happens to land on.
    std::vector<BlueprintNodeId> event_graph_node_ids() const;

    /// Replace the selection with a single node.
    /// Fit the whole active graph in the canvas on the next frame. Node sizes
    /// are only known once they are laid out, so this is deferred rather than
    /// computed up front.
    void request_frame_all();

    void select_only(BlueprintNodeId node);

  private:
    /// One drawable pin, resolved into canvas space for the current frame.
    struct PinView
    {
      std::string name;
      std::string label;
      ValueType type = ValueType::Float;
      bool isExec = false;
      bool isOutput = false;
      bool connected = false;
      int index = 0;
      ImVec2 center{};
      /// Where an inline literal editor goes when the pin has no wire.
      ImVec2 editorPosition{};
      float editorWidth = 0.0f;
      bool showEditor = false;
    };

    struct NodeView
    {
      BlueprintNodeId id = kInvalidBlueprintNode;
      BlueprintNode *node = nullptr;
      const BlueprintNodeType *type = nullptr;
      BlueprintNodeSignature signature;
      std::string title;
      ImVec2 min{};
      ImVec2 size{};
      float headerHeight = 0.0f;
      std::vector<PinView> pins;
      bool selected = false;
      /// Recently executed in play mode, for the live debug highlight.
      float activity = 0.0f;
    };

    /// Structural edits are queued while the canvas is being drawn, because
    /// NodeView holds pointers into the node vector.
    struct PendingEdits
    {
      std::vector<BlueprintNodeId> nodesToDelete;
      std::vector<BlueprintLink> linksToAdd;
      std::vector<BlueprintLink> linksToRemove;
      bool duplicateSelection = false;
      bool empty() const
      {
        return nodesToDelete.empty() && linksToAdd.empty() && linksToRemove.empty() && !duplicateSelection;
      }
    };

    struct LinkDrag
    {
      bool active = false;
      BlueprintNodeId node = kInvalidBlueprintNode;
      std::string pin;
      bool fromOutput = false;
      BlueprintLinkKind kind = BlueprintLinkKind::Exec;
      ValueType type = ValueType::Exec;
    };

    // --- asset lifecycle ---
    void refresh_assets(const std::filesystem::path &workspaceRoot);
    void open_asset(EditorPluginContext &context, const std::string &relativePath);
    void close_asset();
    bool save_asset(EditorPluginContext &context);
    void compile_now(EditorPluginContext &context);
    void mark_dirty();

    // --- graph access ---
    BlueprintGraph *active_graph();
    BlueprintFunction *active_function();
    BlueprintSignatureContext signature_context() const;

    // --- panels ---
    void draw_toolbar(EditorPluginContext &context);
    void draw_sidebar(EditorPluginContext &context);
    void draw_details(EditorPluginContext &context);
    void draw_messages(EditorPluginContext &context);
    void draw_canvas(EditorPluginContext &context);
    void draw_new_asset_dialog(EditorPluginContext &context);

    // --- details-panel node configuration ---
    /// Type picker for a pin whose type is chosen per node rather than fixed
    /// by the registry, stored as a type name under `configKey`. Used by the
    /// `script.*` nodes, whose Value and Result pins carry whatever the
    /// author is passing across the C++ boundary.
    void draw_pin_type_combo(BlueprintNode &node, const char *label, const char *configKey);
    /// Editable parameter list for a Custom Event, stored as
    /// `config["params"]`. The event node turns them into data outputs and
    /// every Call Event targeting it into matching data inputs.
    void draw_event_parameters(BlueprintNode &node);

    // --- canvas internals ---
    std::vector<NodeView> build_views(EditorPluginContext &context);
    void layout_node(NodeView &view) const;
    void draw_grid(ImDrawList *drawList, const ImVec2 &min, const ImVec2 &max) const;
    void draw_links(ImDrawList *drawList, const std::vector<NodeView> &views);
    void draw_node(ImDrawList *drawList, NodeView &view);
    void draw_pending_link(ImDrawList *drawList, const std::vector<NodeView> &views);
    void draw_palette(EditorPluginContext &context);
    void draw_node_context_menu(EditorPluginContext &context);
    void apply_pending_edits();

    BlueprintNodeId spawn_node(
        const std::string &type,
        const ImVec2 &graphPosition,
        const nlohmann::json &config = nlohmann::json::object());

    bool is_selected(BlueprintNodeId node) const;
    void toggle_selection(BlueprintNodeId node);

    ImVec2 to_screen(const ImVec2 &graphPosition) const;
    ImVec2 to_graph(const ImVec2 &screenPosition) const;
    void focus_node(BlueprintNodeId node);

    // --- state ---
    bool visible_ = false;
    bool focusRequested_ = false;

    std::filesystem::path workspaceRoot_;
    std::vector<std::string> assets_;
    double assetsRefreshedAt_ = -1.0;

    std::string assetPath_;
    Blueprint blueprint_;
    bool hasAsset_ = false;
    bool dirty_ = false;
    std::string status_;
    bool statusIsError_ = false;

    CompiledBlueprint compiled_;
    bool compiledValid_ = false;

    /// -1 = event graph, otherwise an index into `blueprint_.functions`.
    int activeFunction_ = -1;
    int selectedVariable_ = -1;

    ImVec2 pan_{0.0f, 0.0f};
    float zoom_ = 1.0f;
    ImVec2 canvasOrigin_{0.0f, 0.0f};
    ImVec2 canvasSize_{0.0f, 0.0f};

    std::vector<BlueprintNodeId> selection_;
    LinkDrag linkDrag_;
    PendingEdits pending_;

    /// Accumulated this frame by whichever node is being dragged, applied to
    /// the whole selection once every node has been drawn.
    ImVec2 dragDelta_{0.0f, 0.0f};
    bool dragging_ = false;

    std::optional<BlueprintLink> selectedLink_;
    /// Panning is tracked explicitly rather than through the background
    /// button's active state, so a middle/right drag still pans when the
    /// cursor happens to be over a node.
    bool panning_ = false;
    bool boxSelecting_ = false;
    ImVec2 boxSelectStart_{0.0f, 0.0f};
    ImVec2 rightPressPosition_{0.0f, 0.0f};
    bool rightPressOnCanvas_ = false;

    /// Pin the mouse is over this frame, used as the link drop target.
    bool hasHoveredPin_ = false;
    BlueprintNodeId hoveredPinNode_ = kInvalidBlueprintNode;
    std::string hoveredPinName_;
    bool hoveredPinIsOutput_ = false;
    BlueprintLinkKind hoveredPinKind_ = BlueprintLinkKind::Exec;
    ValueType hoveredPinType_ = ValueType::Exec;

    bool openPaletteRequested_ = false;
    ImVec2 paletteGraphPosition_{0.0f, 0.0f};
    /// Set when the palette was opened by dropping a wire on empty canvas, so
    /// the spawned node can auto-connect back to where the drag started.
    LinkDrag palettePendingLink_;
    std::array<char, 128> paletteSearch_{};
    bool paletteFocusSearch_ = false;

    BlueprintNodeId contextMenuNode_ = kInvalidBlueprintNode;
    bool openNodeContextMenu_ = false;

    bool openNewAssetDialog_ = false;
    std::array<char, 128> newAssetName_{};
    std::string newAssetError_;

    std::optional<BlueprintNodeId> focusRequest_;
    bool frameRequest_ = false;
    std::array<char, 512> nodeCommentBuffer_{};
    BlueprintNodeId nodeCommentOwner_ = kInvalidBlueprintNode;
  };
}

#endif
