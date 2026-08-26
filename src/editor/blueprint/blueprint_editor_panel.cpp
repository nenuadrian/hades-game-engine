#include "blueprint_editor_panel.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>

#include "imgui.h"
#include "imgui_internal.h"

#include "../../engine/blueprint/blueprint_asset.hpp"
#include "../../engine/blueprint/blueprint_runtime.hpp"
#include "../../engine/blueprint/blueprint_vm.hpp"
#include "../../engine/components/blueprint_component.hpp"
#include "../../engine/core/ecs/component_manager.hpp"
#include "../../engine/core/ecs/entity_manager.hpp"
#include "../IconsFontAwesome6.h"
#include "../editor.hpp"

namespace hades
{
  namespace
  {
    // -----------------------------------------------------------------------
    // Cross-panel open request
    // -----------------------------------------------------------------------

    std::string &pending_open_request()
    {
      static std::string request;
      return request;
    }

    // -----------------------------------------------------------------------
    // Palette
    // -----------------------------------------------------------------------

    constexpr float kNodeMinWidth = 132.0f;
    constexpr float kNodeRowHeight = 20.0f;
    constexpr float kNodePadding = 10.0f;
    constexpr float kPinRadius = 4.5f;
    constexpr float kPinHitRadius = 9.0f;
    constexpr float kGridStep = 24.0f;
    constexpr float kActivityWindow = 0.6f;

    ImU32 type_colour(ValueType type)
    {
      switch (type)
      {
      case ValueType::Exec:
        return IM_COL32(238, 238, 238, 255);
      case ValueType::Bool:
        return IM_COL32(196, 62, 62, 255);
      case ValueType::Int:
        return IM_COL32(38, 184, 150, 255);
      case ValueType::Float:
        return IM_COL32(150, 222, 74, 255);
      case ValueType::String:
        return IM_COL32(216, 74, 200, 255);
      case ValueType::Vector:
        return IM_COL32(240, 190, 62, 255);
      case ValueType::Entity:
        return IM_COL32(86, 152, 240, 255);
      case ValueType::Wildcard:
      default:
        return IM_COL32(160, 160, 168, 255);
      }
    }

    ImU32 category_colour(const std::string &category)
    {
      if (category == "Events")
      {
        return IM_COL32(142, 44, 44, 255);
      }
      if (category == "Flow Control")
      {
        return IM_COL32(72, 74, 86, 255);
      }
      if (category == "Variables")
      {
        return IM_COL32(46, 92, 142, 255);
      }
      if (category == "Functions")
      {
        return IM_COL32(48, 112, 96, 255);
      }
      if (category == "Entity" || category == "Transform")
      {
        return IM_COL32(66, 78, 132, 255);
      }
      if (category == "Physics")
      {
        return IM_COL32(126, 84, 40, 255);
      }
      if (category == "Audio")
      {
        return IM_COL32(112, 58, 122, 255);
      }
      if (category == "Animation")
      {
        return IM_COL32(120, 96, 40, 255);
      }
      if (category == "Scripts")
      {
        return IM_COL32(38, 104, 124, 255);
      }
      if (category == "Debug" || category == "World" || category == "Time")
      {
        return IM_COL32(64, 64, 70, 255);
      }

      // Math, Vector, Logic, Conversion, String, Constants — the pure nodes.
      return IM_COL32(40, 92, 92, 255);
    }

    bool matches_search(const BlueprintNodeType &type, const std::string &needle)
    {
      if (needle.empty())
      {
        return true;
      }

      const auto contains = [&needle](const std::string &haystack)
      {
        if (haystack.empty())
        {
          return false;
        }
        std::string lowered;
        lowered.reserve(haystack.size());
        for (char character : haystack)
        {
          lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
        return lowered.find(needle) != std::string::npos;
      };

      return contains(type.displayName) || contains(type.name) ||
             contains(type.keywords) || contains(type.category);
    }

    std::string lowered(const char *text)
    {
      std::string result;
      for (const char *cursor = text; *cursor != '\0'; ++cursor)
      {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*cursor))));
      }
      return result;
    }

    /// Distance from `point` to the cubic bezier the canvas draws for wires.
    float distance_to_wire(const ImVec2 &a, const ImVec2 &b, const ImVec2 &c, const ImVec2 &d, const ImVec2 &point)
    {
      constexpr int kSamples = 24;
      float best = FLT_MAX;
      ImVec2 previous = a;

      for (int i = 1; i <= kSamples; ++i)
      {
        const float t = static_cast<float>(i) / static_cast<float>(kSamples);
        const float u = 1.0f - t;
        const ImVec2 current(
            u * u * u * a.x + 3.0f * u * u * t * b.x + 3.0f * u * t * t * c.x + t * t * t * d.x,
            u * u * u * a.y + 3.0f * u * u * t * b.y + 3.0f * u * t * t * c.y + t * t * t * d.y);

        const ImVec2 segment(current.x - previous.x, current.y - previous.y);
        const ImVec2 toPoint(point.x - previous.x, point.y - previous.y);
        const float lengthSquared = segment.x * segment.x + segment.y * segment.y;
        float projection = 0.0f;
        if (lengthSquared > 0.0f)
        {
          projection = std::clamp((toPoint.x * segment.x + toPoint.y * segment.y) / lengthSquared, 0.0f, 1.0f);
        }

        const ImVec2 closest(previous.x + segment.x * projection, previous.y + segment.y * projection);
        const float dx = point.x - closest.x;
        const float dy = point.y - closest.y;
        best = std::min(best, std::sqrt(dx * dx + dy * dy));
        previous = current;
      }

      return best;
    }

    void draw_wire(
        ImDrawList *drawList,
        const ImVec2 &from,
        const ImVec2 &to,
        ImU32 colour,
        float thickness)
    {
      const float reach = std::clamp(std::fabs(to.x - from.x) * 0.5f, 24.0f, 140.0f);
      drawList->AddBezierCubic(
          from,
          ImVec2(from.x + reach, from.y),
          ImVec2(to.x - reach, to.y),
          to,
          colour,
          thickness);
    }

    void wire_control_points(const ImVec2 &from, const ImVec2 &to, ImVec2 &b, ImVec2 &c)
    {
      const float reach = std::clamp(std::fabs(to.x - from.x) * 0.5f, 24.0f, 140.0f);
      b = ImVec2(from.x + reach, from.y);
      c = ImVec2(to.x - reach, to.y);
    }

    void draw_exec_pin(ImDrawList *drawList, const ImVec2 &centre, ImU32 colour, bool filled, float scale)
    {
      const float half = 5.0f * scale;
      const ImVec2 a(centre.x - half * 0.7f, centre.y - half);
      const ImVec2 b(centre.x - half * 0.7f, centre.y + half);
      const ImVec2 c(centre.x + half, centre.y);

      if (filled)
      {
        drawList->AddTriangleFilled(a, b, c, colour);
      }
      else
      {
        drawList->AddTriangle(a, b, c, colour, 1.6f * scale);
      }
    }

    ImU32 with_alpha(ImU32 colour, float alpha)
    {
      const ImU32 rgb = colour & 0x00FFFFFFu;
      const ImU32 a = static_cast<ImU32>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
      return rgb | (a << IM_COL32_A_SHIFT);
    }
  }

  void request_blueprint_editor_open(std::string relativePath)
  {
    pending_open_request() = std::move(relativePath);
  }

  // ---------------------------------------------------------------------------
  // Graph access helpers
  // ---------------------------------------------------------------------------

  BlueprintGraph *BlueprintEditorPlugin::active_graph()
  {
    if (!hasAsset_)
    {
      return nullptr;
    }

    if (activeFunction_ < 0)
    {
      return &blueprint_.eventGraph;
    }

    if (static_cast<std::size_t>(activeFunction_) >= blueprint_.functions.size())
    {
      activeFunction_ = -1;
      return &blueprint_.eventGraph;
    }

    return &blueprint_.functions[static_cast<std::size_t>(activeFunction_)].graph;
  }

  BlueprintFunction *BlueprintEditorPlugin::active_function()
  {
    if (!hasAsset_ || activeFunction_ < 0 ||
        static_cast<std::size_t>(activeFunction_) >= blueprint_.functions.size())
    {
      return nullptr;
    }

    return &blueprint_.functions[static_cast<std::size_t>(activeFunction_)];
  }

  BlueprintSignatureContext BlueprintEditorPlugin::signature_context() const
  {
    BlueprintSignatureContext context;
    context.blueprint = &blueprint_;
    if (hasAsset_ && activeFunction_ >= 0 &&
        static_cast<std::size_t>(activeFunction_) < blueprint_.functions.size())
    {
      context.function = &blueprint_.functions[static_cast<std::size_t>(activeFunction_)];
    }
    return context;
  }

  ImVec2 BlueprintEditorPlugin::to_screen(const ImVec2 &graphPosition) const
  {
    return ImVec2(
        canvasOrigin_.x + pan_.x + graphPosition.x * zoom_,
        canvasOrigin_.y + pan_.y + graphPosition.y * zoom_);
  }

  ImVec2 BlueprintEditorPlugin::to_graph(const ImVec2 &screenPosition) const
  {
    return ImVec2(
        (screenPosition.x - canvasOrigin_.x - pan_.x) / zoom_,
        (screenPosition.y - canvasOrigin_.y - pan_.y) / zoom_);
  }

  bool BlueprintEditorPlugin::is_selected(BlueprintNodeId node) const
  {
    return std::find(selection_.begin(), selection_.end(), node) != selection_.end();
  }

  void BlueprintEditorPlugin::select_only(BlueprintNodeId node)
  {
    selection_.clear();
    if (node != kInvalidBlueprintNode)
    {
      selection_.push_back(node);
    }
    selectedVariable_ = -1;
  }

  void BlueprintEditorPlugin::toggle_selection(BlueprintNodeId node)
  {
    const auto it = std::find(selection_.begin(), selection_.end(), node);
    if (it == selection_.end())
    {
      selection_.push_back(node);
    }
    else
    {
      selection_.erase(it);
    }
  }

  void BlueprintEditorPlugin::mark_dirty()
  {
    dirty_ = true;
    compiledValid_ = false;
  }

  void BlueprintEditorPlugin::focus_node(BlueprintNodeId node)
  {
    focusRequest_ = node;
  }

  void BlueprintEditorPlugin::request_frame_all()
  {
    frameRequest_ = true;
  }

  // ---------------------------------------------------------------------------
  // Asset lifecycle
  // ---------------------------------------------------------------------------

  void BlueprintEditorPlugin::refresh_assets(const std::filesystem::path &workspaceRoot)
  {
    workspaceRoot_ = workspaceRoot;
    assets_ = list_blueprint_assets(workspaceRoot);
    assetsRefreshedAt_ = ImGui::GetTime();
  }

  void BlueprintEditorPlugin::open_asset(EditorPluginContext &context, const std::string &relativePath)
  {
    Blueprint loaded;
    std::string error;
    if (!load_blueprint(context.workspacePath / relativePath, loaded, &error))
    {
      status_ = error;
      statusIsError_ = true;
      context.editor.log_error("Blueprint: " + error);
      return;
    }

    blueprint_ = std::move(loaded);
    assetPath_ = relativePath;
    hasAsset_ = true;
    dirty_ = false;
    activeFunction_ = -1;
    selection_.clear();
    selectedVariable_ = -1;
    pan_ = ImVec2(0.0f, 0.0f);
    zoom_ = 1.0f;
    // Graphs routinely use negative coordinates, so parking the view at the
    // origin would leave most of the nodes off-screen on open.
    request_frame_all();
    status_ = "Opened " + relativePath;
    statusIsError_ = false;
    compile_now(context);
  }

  void BlueprintEditorPlugin::close_asset()
  {
    blueprint_ = Blueprint();
    assetPath_.clear();
    hasAsset_ = false;
    dirty_ = false;
    compiledValid_ = false;
    selection_.clear();
    activeFunction_ = -1;
  }

  bool BlueprintEditorPlugin::save_asset(EditorPluginContext &context)
  {
    if (!hasAsset_ || assetPath_.empty())
    {
      return false;
    }

    blueprint_.normalize();

    std::string error;
    if (!save_blueprint(context.workspacePath / assetPath_, blueprint_, &error))
    {
      status_ = error;
      statusIsError_ = true;
      context.editor.log_error("Blueprint: " + error);
      return false;
    }

    dirty_ = false;
    status_ = "Saved " + assetPath_;
    statusIsError_ = false;
    context.editor.log_info("Blueprint saved: " + assetPath_);
    return true;
  }

  void BlueprintEditorPlugin::compile_now(EditorPluginContext &context)
  {
    if (!hasAsset_)
    {
      return;
    }

    blueprint_.normalize();
    compiled_ = compile_blueprint(blueprint_);
    compiledValid_ = true;

    if (compiled_.succeeded)
    {
      const int warnings = compiled_.warning_count();
      status_ = warnings == 0
                    ? "Compiled cleanly"
                    : ("Compiled with " + std::to_string(warnings) + " warning(s)");
      statusIsError_ = false;
    }
    else
    {
      status_ = std::to_string(compiled_.error_count()) + " error(s)";
      statusIsError_ = true;
      context.editor.log_error("Blueprint '" + assetPath_ + "' failed to compile:\n" + compiled_.error_summary());
    }
  }

  BlueprintNodeId BlueprintEditorPlugin::spawn_node(
      const std::string &type,
      const ImVec2 &graphPosition,
      const nlohmann::json &config)
  {
    BlueprintGraph *graph = active_graph();
    if (graph == nullptr)
    {
      return kInvalidBlueprintNode;
    }

    BlueprintNode node;
    node.id = blueprint_.allocate_node_id();
    node.type = type;
    node.x = graphPosition.x;
    node.y = graphPosition.y;
    if (config.is_object() && !config.empty())
    {
      node.config = config;
    }

    graph->nodes.push_back(std::move(node));
    mark_dirty();
    return graph->nodes.back().id;
  }

  void BlueprintEditorPlugin::apply_pending_edits()
  {
    BlueprintGraph *graph = active_graph();
    if (graph == nullptr || pending_.empty())
    {
      pending_ = PendingEdits();
      return;
    }

    for (const auto &link : pending_.linksToRemove)
    {
      graph->remove_link(link);
    }

    for (const auto &link : pending_.linksToAdd)
    {
      // Data inputs take a single wire, and an exec output drives a single
      // chain; adding a wire displaces whatever was there.
      if (link.kind == BlueprintLinkKind::Data)
      {
        graph->remove_links_into(link.to, BlueprintLinkKind::Data);
      }
      else
      {
        graph->remove_links_out_of(link.from, BlueprintLinkKind::Exec);
      }

      if (std::find(graph->links.begin(), graph->links.end(), link) == graph->links.end())
      {
        graph->links.push_back(link);
      }
    }

    if (pending_.duplicateSelection && !selection_.empty())
    {
      std::vector<BlueprintNodeId> copies;
      std::map<BlueprintNodeId, BlueprintNodeId> remap;

      for (BlueprintNodeId id : selection_)
      {
        const BlueprintNode *source = graph->find_node(id);
        if (source == nullptr)
        {
          continue;
        }

        BlueprintNode copy = *source;
        copy.id = blueprint_.allocate_node_id();
        copy.x += 30.0f;
        copy.y += 30.0f;
        remap[id] = copy.id;
        copies.push_back(copy.id);
        graph->nodes.push_back(std::move(copy));
      }

      // Carry over wires whose endpoints were both duplicated.
      const std::size_t originalLinkCount = graph->links.size();
      for (std::size_t i = 0; i < originalLinkCount; ++i)
      {
        const BlueprintLink &link = graph->links[i];
        const auto from = remap.find(link.from.node);
        const auto to = remap.find(link.to.node);
        if (from == remap.end() || to == remap.end())
        {
          continue;
        }

        BlueprintLink copy = link;
        copy.from.node = from->second;
        copy.to.node = to->second;
        graph->links.push_back(copy);
      }

      selection_ = copies;
    }

    for (BlueprintNodeId id : pending_.nodesToDelete)
    {
      graph->remove_node(id);
      const auto it = std::find(selection_.begin(), selection_.end(), id);
      if (it != selection_.end())
      {
        selection_.erase(it);
      }
    }

    mark_dirty();
    pending_ = PendingEdits();
  }
}

namespace hades
{
  // ---------------------------------------------------------------------------
  // Canvas layout and drawing
  // ---------------------------------------------------------------------------

  void BlueprintEditorPlugin::layout_node(NodeView &view) const
  {
    const float scale = zoom_;
    const float rowHeight = kNodeRowHeight * scale;
    const float padding = kNodePadding * scale;
    const float fontSize = ImGui::GetFontSize();

    view.headerHeight = fontSize + 10.0f * scale;

    const auto &signature = view.signature;
    const std::size_t inputRows = signature.execInputs.size() + signature.dataInputs.size();
    const std::size_t outputRows = signature.execOutputs.size() + signature.dataOutputs.size();
    const std::size_t rows = std::max(inputRows, outputRows);

    // Widths: the title, the widest label column on each side, plus room for
    // an inline literal editor next to unwired data inputs.
    float leftWidth = 0.0f;
    for (const auto &pinName : signature.execInputs)
    {
      leftWidth = std::max(leftWidth, ImGui::CalcTextSize(pinName.c_str()).x);
    }

    float literalWidth = 0.0f;
    for (const auto &pinSpec : signature.dataInputs)
    {
      leftWidth = std::max(leftWidth, ImGui::CalcTextSize(pinSpec.label().c_str()).x);

      const bool wired = view.node != nullptr && false;
      (void)wired;
      switch (pinSpec.type)
      {
      case ValueType::Vector:
      case ValueType::String:
        literalWidth = std::max(literalWidth, 118.0f * scale);
        break;
      case ValueType::Bool:
        literalWidth = std::max(literalWidth, 22.0f * scale);
        break;
      case ValueType::Float:
      case ValueType::Int:
        literalWidth = std::max(literalWidth, 60.0f * scale);
        break;
      default:
        break;
      }
    }

    float rightWidth = 0.0f;
    for (const auto &pinName : signature.execOutputs)
    {
      rightWidth = std::max(rightWidth, ImGui::CalcTextSize(pinName.c_str()).x);
    }
    for (const auto &pinSpec : signature.dataOutputs)
    {
      rightWidth = std::max(rightWidth, ImGui::CalcTextSize(pinSpec.label().c_str()).x);
    }

    const float titleWidth = ImGui::CalcTextSize(view.title.c_str()).x;
    const float width = std::max(
        {kNodeMinWidth * scale,
         titleWidth + padding * 3.0f,
         leftWidth + literalWidth + rightWidth + padding * 4.0f});

    const float bodyHeight = padding + static_cast<float>(rows) * rowHeight + padding * 0.5f;
    view.size = ImVec2(width, view.headerHeight + std::max(bodyHeight, rowHeight));

    // Pin placement, inputs down the left edge then outputs down the right.
    float y = view.min.y + view.headerHeight + padding + rowHeight * 0.5f;
    int index = 0;

    for (const auto &pinName : signature.execInputs)
    {
      PinView pin;
      pin.name = pinName;
      pin.label = pinName;
      pin.type = ValueType::Exec;
      pin.isExec = true;
      pin.isOutput = false;
      pin.index = index++;
      pin.center = ImVec2(view.min.x, y);
      view.pins.push_back(std::move(pin));
      y += rowHeight;
    }

    index = 0;
    for (const auto &pinSpec : signature.dataInputs)
    {
      PinView pin;
      pin.name = pinSpec.name;
      pin.label = pinSpec.label();
      pin.type = pinSpec.type;
      pin.isExec = false;
      pin.isOutput = false;
      pin.index = index++;
      pin.center = ImVec2(view.min.x, y);
      pin.editorWidth = literalWidth;
      pin.editorPosition = ImVec2(
          view.min.x + padding + leftWidth + padding * 0.5f,
          y - ImGui::GetFrameHeight() * 0.5f);
      view.pins.push_back(std::move(pin));
      y += rowHeight;
    }

    y = view.min.y + view.headerHeight + padding + rowHeight * 0.5f;
    index = 0;
    for (const auto &pinName : signature.execOutputs)
    {
      PinView pin;
      pin.name = pinName;
      pin.label = pinName;
      pin.type = ValueType::Exec;
      pin.isExec = true;
      pin.isOutput = true;
      pin.index = index++;
      pin.center = ImVec2(view.min.x + view.size.x, y);
      view.pins.push_back(std::move(pin));
      y += rowHeight;
    }

    index = 0;
    for (const auto &pinSpec : signature.dataOutputs)
    {
      PinView pin;
      pin.name = pinSpec.name;
      pin.label = pinSpec.label();
      pin.type = pinSpec.type;
      pin.isExec = false;
      pin.isOutput = true;
      pin.index = index++;
      pin.center = ImVec2(view.min.x + view.size.x, y);
      view.pins.push_back(std::move(pin));
      y += rowHeight;
    }
  }

  std::vector<BlueprintEditorPlugin::NodeView> BlueprintEditorPlugin::build_views(
      EditorPluginContext &context)
  {
    std::vector<NodeView> views;
    BlueprintGraph *graph = active_graph();
    if (graph == nullptr)
    {
      return views;
    }

    // Live execution highlight: find the running instance for this asset so
    // recently executed nodes can pulse while play mode is on.
    const BlueprintInstance *liveInstance = nullptr;
    if (context.editor.state.isPlaying && activeFunction_ < 0)
    {
      for (const auto &view : context.blueprintRuntime.instances())
      {
        if (view.assetPath == assetPath_ && view.instance != nullptr)
        {
          liveInstance = view.instance;
          break;
        }
      }
    }

    const BlueprintSignatureContext signatureContext = signature_context();
    views.reserve(graph->nodes.size());

    for (auto &node : graph->nodes)
    {
      NodeView view;
      view.id = node.id;
      view.node = &node;
      view.type = BlueprintNodeRegistry::instance().find(node.type);
      view.selected = is_selected(node.id);

      if (!resolve_blueprint_node_signature(signatureContext, node, view.signature))
      {
        view.title = "Unknown: " + node.type;
      }
      else
      {
        view.title = view.signature.title.empty()
                         ? (view.type != nullptr ? view.type->displayName : node.type)
                         : view.signature.title;
      }

      view.min = to_screen(ImVec2(node.x, node.y));
      layout_node(view);

      // Mark which pins already carry a wire so the editor can hide the
      // inline literal and draw a filled socket.
      for (auto &pin : view.pins)
      {
        const BlueprintLinkKind kind = pin.isExec ? BlueprintLinkKind::Exec : BlueprintLinkKind::Data;
        pin.connected = graph->pin_has_link({node.id, pin.name}, kind, !pin.isOutput);
        pin.showEditor =
            !pin.isExec && !pin.isOutput && !pin.connected && zoom_ >= 0.55f &&
            (pin.type == ValueType::Float || pin.type == ValueType::Int ||
             pin.type == ValueType::Bool || pin.type == ValueType::String ||
             pin.type == ValueType::Vector);
      }

      if (liveInstance != nullptr && compiledValid_)
      {
        const int compiledIndex = [&]()
        {
          for (std::size_t i = 0; i < compiled_.eventGraph.nodes.size(); ++i)
          {
            if (compiled_.eventGraph.nodes[i].source.id == node.id)
            {
              return static_cast<int>(i);
            }
          }
          return -1;
        }();

        if (compiledIndex >= 0 &&
            static_cast<std::size_t>(compiledIndex) < liveInstance->nodeLastExecuted.size())
        {
          const float age =
              liveInstance->elapsedSeconds -
              liveInstance->nodeLastExecuted[static_cast<std::size_t>(compiledIndex)];
          if (age >= 0.0f && age <= kActivityWindow)
          {
            view.activity = 1.0f - (age / kActivityWindow);
          }
        }
      }

      views.push_back(std::move(view));
    }

    return views;
  }

  void BlueprintEditorPlugin::draw_grid(ImDrawList *drawList, const ImVec2 &min, const ImVec2 &max) const
  {
    const ImU32 background = IM_COL32(24, 25, 30, 255);
    drawList->AddRectFilled(min, max, background);

    const float step = kGridStep * zoom_;
    if (step < 6.0f)
    {
      return;
    }

    const ImU32 minor = IM_COL32(38, 40, 47, 255);
    const ImU32 major = IM_COL32(52, 55, 64, 255);

    const float offsetX = std::fmod(pan_.x, step * 4.0f);
    const float offsetY = std::fmod(pan_.y, step * 4.0f);

    for (float x = std::fmod(pan_.x, step); x < max.x - min.x; x += step)
    {
      const bool isMajor = std::fabs(std::fmod(x - offsetX, step * 4.0f)) < 0.5f;
      drawList->AddLine(
          ImVec2(min.x + x, min.y),
          ImVec2(min.x + x, max.y),
          isMajor ? major : minor);
    }

    for (float y = std::fmod(pan_.y, step); y < max.y - min.y; y += step)
    {
      const bool isMajor = std::fabs(std::fmod(y - offsetY, step * 4.0f)) < 0.5f;
      drawList->AddLine(
          ImVec2(min.x, min.y + y),
          ImVec2(max.x, min.y + y),
          isMajor ? major : minor);
    }
  }

  void BlueprintEditorPlugin::draw_links(ImDrawList *drawList, const std::vector<NodeView> &views)
  {
    BlueprintGraph *graph = active_graph();
    if (graph == nullptr)
    {
      return;
    }

    const auto find_pin = [&views](BlueprintNodeId node, const std::string &name, bool output, const PinView **out)
    {
      for (const auto &view : views)
      {
        if (view.id != node)
        {
          continue;
        }
        for (const auto &pin : view.pins)
        {
          if (pin.isOutput == output && pin.name == name)
          {
            *out = &pin;
            return true;
          }
        }
        return false;
      }
      return false;
    };

    const auto activity_of = [&views](BlueprintNodeId node)
    {
      for (const auto &view : views)
      {
        if (view.id == node)
        {
          return view.activity;
        }
      }
      return 0.0f;
    };

    for (const auto &link : graph->links)
    {
      const PinView *from = nullptr;
      const PinView *to = nullptr;
      if (!find_pin(link.from.node, link.from.pin, true, &from) ||
          !find_pin(link.to.node, link.to.pin, false, &to))
      {
        continue;
      }

      const bool isExec = link.kind == BlueprintLinkKind::Exec;
      ImU32 colour = isExec ? type_colour(ValueType::Exec) : type_colour(from->type);
      float thickness = (isExec ? 2.4f : 1.9f) * zoom_;

      // A wire between two nodes that both ran recently glows, which makes the
      // path execution took visible at a glance.
      const float pulse = std::min(activity_of(link.from.node), activity_of(link.to.node));
      if (pulse > 0.0f)
      {
        drawList->AddBezierCubic(
            from->center,
            ImVec2(from->center.x + 60.0f * zoom_, from->center.y),
            ImVec2(to->center.x - 60.0f * zoom_, to->center.y),
            to->center,
            with_alpha(IM_COL32(255, 190, 80, 255), pulse * 0.75f),
            thickness * 3.0f);
        colour = IM_COL32(255, 214, 130, 255);
        thickness *= 1.35f;
      }

      draw_wire(drawList, from->center, to->center, colour, thickness);
    }
  }

  void BlueprintEditorPlugin::draw_pending_link(ImDrawList *drawList, const std::vector<NodeView> &views)
  {
    if (!linkDrag_.active)
    {
      return;
    }

    const PinView *source = nullptr;
    for (const auto &view : views)
    {
      if (view.id != linkDrag_.node)
      {
        continue;
      }
      for (const auto &pin : view.pins)
      {
        if (pin.isOutput == linkDrag_.fromOutput && pin.name == linkDrag_.pin)
        {
          source = &pin;
          break;
        }
      }
      break;
    }

    if (source == nullptr)
    {
      return;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ImU32 colour = type_colour(linkDrag_.type);
    if (linkDrag_.fromOutput)
    {
      draw_wire(drawList, source->center, mouse, colour, 2.2f * zoom_);
    }
    else
    {
      draw_wire(drawList, mouse, source->center, colour, 2.2f * zoom_);
    }
  }

  void BlueprintEditorPlugin::draw_node(ImDrawList *drawList, NodeView &view)
  {
    const float scale = zoom_;
    const float rounding = 5.0f * scale;
    const ImVec2 max(view.min.x + view.size.x, view.min.y + view.size.y);
    const ImVec2 headerMax(max.x, view.min.y + view.headerHeight);

    const std::string category = view.type != nullptr ? view.type->category : std::string("Unknown");
    const ImU32 headerColour = view.type != nullptr ? category_colour(category) : IM_COL32(150, 40, 40, 255);

    ImGui::PushID(static_cast<int>(view.id));

    // Body button first, but flagged AllowOverlap so the pins and inline
    // editors submitted after it can still claim hover. Without this ImGui
    // hands the whole node rect to the body and every pin becomes dead.
    ImGui::SetNextItemAllowOverlap();
    ImGui::SetCursorScreenPos(view.min);
    ImGui::InvisibleButton("##body", view.size);
    const bool bodyHovered = ImGui::IsItemHovered();
    const bool bodyRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsItemActivated())
    {
      if (io.KeyShift)
      {
        toggle_selection(view.id);
      }
      else if (!is_selected(view.id))
      {
        select_only(view.id);
      }
      view.selected = is_selected(view.id);
    }

    if (!panning_ && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
      dragDelta_ = ImVec2(dragDelta_.x + io.MouseDelta.x / scale, dragDelta_.y + io.MouseDelta.y / scale);
      dragging_ = true;
    }

    // Chrome.
    drawList->AddRectFilled(view.min, max, IM_COL32(34, 36, 43, 242), rounding);
    drawList->AddRectFilled(view.min, headerMax, headerColour, rounding, ImDrawFlags_RoundCornersTop);

    ImU32 borderColour = IM_COL32(14, 15, 18, 255);
    float borderThickness = 1.2f * scale;
    if (view.activity > 0.0f)
    {
      borderColour = with_alpha(IM_COL32(255, 200, 90, 255), 0.35f + view.activity * 0.65f);
      borderThickness = 2.4f * scale;
    }
    if (view.selected)
    {
      borderColour = IM_COL32(255, 176, 46, 255);
      borderThickness = 2.4f * scale;
    }
    else if (bodyHovered)
    {
      borderColour = IM_COL32(150, 156, 170, 255);
    }
    drawList->AddRect(view.min, max, borderColour, rounding, 0, borderThickness);

    const float fontSize = ImGui::GetFontSize();
    drawList->AddText(
        ImVec2(view.min.x + kNodePadding * scale, view.min.y + (view.headerHeight - fontSize) * 0.5f),
        IM_COL32(244, 246, 250, 255),
        view.title.c_str());

    if (view.node != nullptr && !view.node->comment.empty())
    {
      drawList->AddText(
          ImVec2(view.min.x, view.min.y - fontSize - 3.0f * scale),
          IM_COL32(190, 196, 210, 220),
          view.node->comment.c_str());
    }

    // Pins.
    BlueprintGraph *graph = active_graph();
    bool pinConsumedRightClick = false;
    for (auto &pin : view.pins)
    {
      const ImU32 colour = type_colour(pin.type);
      const float hit = kPinHitRadius * scale;

      ImGui::PushID(pin.isOutput ? 1 : 0);
      ImGui::PushID(pin.name.c_str());
      ImGui::SetCursorScreenPos(ImVec2(pin.center.x - hit, pin.center.y - hit));
      ImGui::InvisibleButton("##pin", ImVec2(hit * 2.0f, hit * 2.0f));

      const bool pinHovered = ImGui::IsItemHovered();
      if (pinHovered)
      {
        hasHoveredPin_ = true;
        hoveredPinNode_ = view.id;
        hoveredPinName_ = pin.name;
        hoveredPinIsOutput_ = pin.isOutput;
        hoveredPinKind_ = pin.isExec ? BlueprintLinkKind::Exec : BlueprintLinkKind::Data;
        hoveredPinType_ = pin.type;

        ImGui::SetTooltip("%s (%s)", pin.label.c_str(), value_type_name(pin.type));
      }

      if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
      {
        linkDrag_.active = true;
        linkDrag_.node = view.id;
        linkDrag_.pin = pin.name;
        linkDrag_.fromOutput = pin.isOutput;
        linkDrag_.kind = pin.isExec ? BlueprintLinkKind::Exec : BlueprintLinkKind::Data;
        linkDrag_.type = pin.type;
      }

      if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && graph != nullptr)
      {
        pinConsumedRightClick = true;
        const BlueprintLinkKind kind = pin.isExec ? BlueprintLinkKind::Exec : BlueprintLinkKind::Data;
        for (const auto &link : graph->links)
        {
          if (link.kind != kind)
          {
            continue;
          }
          if (pin.isOutput ? (link.from == BlueprintPinRef{view.id, pin.name})
                           : (link.to == BlueprintPinRef{view.id, pin.name}))
          {
            pending_.linksToRemove.push_back(link);
          }
        }
      }

      ImGui::PopID();
      ImGui::PopID();

      const float pinScale = pinHovered ? 1.35f : 1.0f;
      if (pin.isExec)
      {
        draw_exec_pin(drawList, pin.center, colour, pin.connected || pinHovered, scale * pinScale);
      }
      else
      {
        if (pin.connected)
        {
          drawList->AddCircleFilled(pin.center, kPinRadius * scale * pinScale, colour, 12);
        }
        else
        {
          drawList->AddCircle(pin.center, kPinRadius * scale * pinScale, colour, 12, 1.6f * scale);
        }
      }

      const float labelY = pin.center.y - fontSize * 0.5f;
      if (pin.isOutput)
      {
        const float labelWidth = ImGui::CalcTextSize(pin.label.c_str()).x;
        drawList->AddText(
            ImVec2(pin.center.x - kNodePadding * scale - labelWidth, labelY),
            IM_COL32(212, 216, 226, 255),
            pin.label.c_str());
      }
      else
      {
        drawList->AddText(
            ImVec2(pin.center.x + kNodePadding * scale, labelY),
            IM_COL32(212, 216, 226, 255),
            pin.label.c_str());
      }
    }

    // Inline literal editors last so they capture the mouse over everything.
    if (view.node != nullptr)
    {
      for (auto &pin : view.pins)
      {
        if (!pin.showEditor)
        {
          continue;
        }

        BlueprintValue current = BlueprintValue::default_for(pin.type);
        const auto stored = view.node->pinDefaults.find(pin.name);
        if (stored != view.node->pinDefaults.end())
        {
          current = stored->second.coerced_to(pin.type);
        }
        else if (view.type != nullptr)
        {
          for (const auto &spec : view.signature.dataInputs)
          {
            if (spec.name == pin.name)
            {
              current = blueprint_pin_literal(*view.node, spec);
              break;
            }
          }
        }

        ImGui::PushID(pin.name.c_str());
        ImGui::SetCursorScreenPos(pin.editorPosition);
        ImGui::SetNextItemWidth(pin.editorWidth);

        bool changed = false;
        switch (pin.type)
        {
        case ValueType::Bool:
        {
          bool value = current.as_bool();
          if (ImGui::Checkbox("##literal", &value))
          {
            current = BlueprintValue::from_bool(value);
            changed = true;
          }
          break;
        }
        case ValueType::Int:
        {
          int value = current.as_int();
          if (ImGui::DragInt("##literal", &value, 0.2f))
          {
            current = BlueprintValue::from_int(value);
            changed = true;
          }
          break;
        }
        case ValueType::Float:
        {
          float value = current.as_float();
          if (ImGui::DragFloat("##literal", &value, 0.05f, 0.0f, 0.0f, "%.3f"))
          {
            current = BlueprintValue::from_float(value);
            changed = true;
          }
          break;
        }
        case ValueType::Vector:
        {
          math::Vec3 value = current.as_vector();
          float components[3] = {value.x, value.y, value.z};
          if (ImGui::DragFloat3("##literal", components, 0.05f, 0.0f, 0.0f, "%.2f"))
          {
            current = BlueprintValue::from_vector(math::Vec3(components[0], components[1], components[2]));
            changed = true;
          }
          break;
        }
        case ValueType::String:
        {
          std::array<char, 256> buffer{};
          const std::string text = current.as_string();
          std::snprintf(buffer.data(), buffer.size(), "%s", text.c_str());
          if (ImGui::InputText("##literal", buffer.data(), buffer.size()))
          {
            current = BlueprintValue::from_string(buffer.data());
            changed = true;
          }
          break;
        }
        default:
          break;
        }

        if (changed)
        {
          view.node->pinDefaults[pin.name] = current;
          mark_dirty();
        }

        ImGui::PopID();
      }
    }

    if (bodyRightClicked && !pinConsumedRightClick)
    {
      // Right-clicking outside the current selection retargets it, matching
      // how the scene tree and every other list in the editor behave.
      if (!is_selected(view.id))
      {
        select_only(view.id);
        view.selected = true;
      }

      contextMenuNode_ = view.id;
      openNodeContextMenu_ = true;
    }

    ImGui::PopID();
  }
}

namespace hades
{
  // ---------------------------------------------------------------------------
  // Canvas frame
  // ---------------------------------------------------------------------------

  void BlueprintEditorPlugin::draw_canvas(EditorPluginContext &context)
  {
    ImGui::BeginChild(
        "##blueprint_canvas",
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);

    canvasOrigin_ = ImGui::GetCursorScreenPos();
    canvasSize_ = ImGui::GetContentRegionAvail();
    canvasSize_.x = std::max(canvasSize_.x, 64.0f);
    canvasSize_.y = std::max(canvasSize_.y, 64.0f);
    const ImVec2 canvasMax(canvasOrigin_.x + canvasSize_.x, canvasOrigin_.y + canvasSize_.y);

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImGuiIO &io = ImGui::GetIO();

    draw_grid(drawList, canvasOrigin_, canvasMax);

    // True whenever the cursor is anywhere over the canvas, including on top
    // of a node. Zooming and panning key off this rather than off the
    // background button, which deliberately loses hover to the nodes.
    const bool canvasRegionHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    // Background hit target. AllowOverlap hands hover to the nodes submitted
    // after it; without the flag ImGui gives this button the entire canvas and
    // every node, pin and inline editor becomes unclickable.
    ImGui::SetNextItemAllowOverlap();
    ImGui::SetCursorScreenPos(canvasOrigin_);
    ImGui::InvisibleButton(
        "##canvas",
        canvasSize_,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
            ImGuiButtonFlags_MouseButtonMiddle);
    const bool canvasHovered = ImGui::IsItemHovered();
    const bool canvasActive = ImGui::IsItemActive();

    // Panning is latched on press instead of following the background button's
    // active state, so a drag that starts over a node still pans. Space or Alt
    // plus the left button is offered because trackpads have no middle button
    // and a right-drag is awkward on them.
    const bool panGestureHeld =
        (ImGui::IsKeyDown(ImGuiKey_Space) && !io.WantTextInput) || io.KeyAlt;

    if (!panning_ && canvasRegionHovered &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
         (panGestureHeld && ImGui::IsMouseClicked(ImGuiMouseButton_Left))))
    {
      panning_ = true;
    }
    if (panning_ &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Middle) &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
      panning_ = false;
    }
    if (panning_)
    {
      pan_.x += io.MouseDelta.x;
      pan_.y += io.MouseDelta.y;
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    else if (panGestureHeld && canvasRegionHovered)
    {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    if (canvasRegionHovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f))
    {
      // Plain scrolling pans, which is what a trackpad's two-finger gesture
      // should do; zooming is the modified gesture, as in most canvas editors.
      const bool zoomGesture = io.KeyCtrl || io.KeySuper;
      if (zoomGesture && io.MouseWheel != 0.0f)
      {
        // Zoom about the cursor so the graph does not slide away under it.
        const ImVec2 anchor = to_graph(io.MousePos);
        zoom_ = std::clamp(zoom_ * (1.0f + io.MouseWheel * 0.12f), 0.3f, 2.5f);
        const ImVec2 after = to_screen(anchor);
        pan_.x += io.MousePos.x - after.x;
        pan_.y += io.MousePos.y - after.y;
      }
      else
      {
        constexpr float kScrollPanSpeed = 48.0f;
        pan_.x += io.MouseWheelH * kScrollPanSpeed;
        pan_.y += io.MouseWheel * kScrollPanSpeed;
      }
    }

    if (canvasRegionHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
      rightPressOnCanvas_ = true;
      rightPressPosition_ = io.MousePos;
    }

    const float baseFontSize = ImGui::GetFontSize();
    ImGui::PushFont(nullptr, baseFontSize * zoom_);

    hasHoveredPin_ = false;
    dragDelta_ = ImVec2(0.0f, 0.0f);
    dragging_ = false;

    std::vector<NodeView> views = build_views(context);

    if (frameRequest_)
    {
      frameRequest_ = false;

      if (views.empty())
      {
        pan_ = ImVec2(60.0f, 60.0f);
        zoom_ = 1.0f;
      }
      else
      {
        // Node extents are only known after layout, and layout is in screen
        // space, so convert back through the current zoom.
        float minX = FLT_MAX;
        float minY = FLT_MAX;
        float maxX = -FLT_MAX;
        float maxY = -FLT_MAX;

        for (const auto &view : views)
        {
          if (view.node == nullptr)
          {
            continue;
          }
          minX = std::min(minX, view.node->x);
          minY = std::min(minY, view.node->y);
          maxX = std::max(maxX, view.node->x + view.size.x / zoom_);
          maxY = std::max(maxY, view.node->y + view.size.y / zoom_);
        }

        if (minX <= maxX)
        {
          constexpr float kMargin = 70.0f;
          const float spanX = std::max((maxX - minX) + kMargin * 2.0f, 1.0f);
          const float spanY = std::max((maxY - minY) + kMargin * 2.0f, 1.0f);

          // Never magnify past 1:1 — a two-node graph blown up to fill the
          // canvas looks broken rather than helpful.
          zoom_ = std::clamp(std::min(canvasSize_.x / spanX, canvasSize_.y / spanY), 0.3f, 1.0f);

          const float centreX = (minX + maxX) * 0.5f;
          const float centreY = (minY + maxY) * 0.5f;
          pan_ = ImVec2(
              canvasSize_.x * 0.5f - centreX * zoom_,
              canvasSize_.y * 0.5f - centreY * zoom_);
        }
      }

      views = build_views(context);
    }

    if (focusRequest_.has_value())
    {
      for (const auto &view : views)
      {
        if (view.id != *focusRequest_ || view.node == nullptr)
        {
          continue;
        }

        pan_ = ImVec2(
            canvasSize_.x * 0.5f - view.node->x * zoom_ - view.size.x * 0.5f,
            canvasSize_.y * 0.5f - view.node->y * zoom_ - view.size.y * 0.5f);
        select_only(view.id);
        break;
      }
      focusRequest_.reset();
      views = build_views(context);
    }

    // Wire picking only counts when the cursor is not already over a node.
    bool mouseOverNode = false;
    for (const auto &view : views)
    {
      if (io.MousePos.x >= view.min.x && io.MousePos.x <= view.min.x + view.size.x &&
          io.MousePos.y >= view.min.y && io.MousePos.y <= view.min.y + view.size.y)
      {
        mouseOverNode = true;
        break;
      }
    }

    draw_links(drawList, views);

    if (canvasHovered && !mouseOverNode && !linkDrag_.active)
    {
      BlueprintGraph *graph = active_graph();
      if (graph != nullptr && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
        selectedLink_.reset();

        const auto pin_centre = [&views](BlueprintNodeId node, const std::string &name, bool output, ImVec2 &out)
        {
          for (const auto &view : views)
          {
            if (view.id != node)
            {
              continue;
            }
            for (const auto &pin : view.pins)
            {
              if (pin.isOutput == output && pin.name == name)
              {
                out = pin.center;
                return true;
              }
            }
          }
          return false;
        };

        for (const auto &link : graph->links)
        {
          ImVec2 from;
          ImVec2 to;
          if (!pin_centre(link.from.node, link.from.pin, true, from) ||
              !pin_centre(link.to.node, link.to.pin, false, to))
          {
            continue;
          }

          ImVec2 b;
          ImVec2 c;
          wire_control_points(from, to, b, c);
          if (distance_to_wire(from, b, c, to, io.MousePos) <= 7.0f)
          {
            selectedLink_ = link;
            selection_.clear();
            break;
          }
        }
      }
    }

    if (selectedLink_.has_value())
    {
      // Redraw the picked wire brighter so the Delete key has a visible target.
      const auto pin_centre = [&views](BlueprintNodeId node, const std::string &name, bool output, ImVec2 &out)
      {
        for (const auto &view : views)
        {
          if (view.id != node)
          {
            continue;
          }
          for (const auto &pin : view.pins)
          {
            if (pin.isOutput == output && pin.name == name)
            {
              out = pin.center;
              return true;
            }
          }
        }
        return false;
      };

      ImVec2 from;
      ImVec2 to;
      if (pin_centre(selectedLink_->from.node, selectedLink_->from.pin, true, from) &&
          pin_centre(selectedLink_->to.node, selectedLink_->to.pin, false, to))
      {
        draw_wire(drawList, from, to, IM_COL32(255, 176, 46, 255), 3.4f * zoom_);
      }
      else
      {
        selectedLink_.reset();
      }
    }

    for (auto &view : views)
    {
      draw_node(drawList, view);
    }

    draw_pending_link(drawList, views);

    // Apply the frame's drag to every selected node at once.
    if (dragging_ && (dragDelta_.x != 0.0f || dragDelta_.y != 0.0f))
    {
      if (BlueprintGraph *graph = active_graph())
      {
        for (BlueprintNodeId id : selection_)
        {
          if (BlueprintNode *node = graph->find_node(id))
          {
            node->x += dragDelta_.x;
            node->y += dragDelta_.y;
          }
        }
        mark_dirty();
      }
    }

    // Box selection over empty canvas.
    if (canvasActive && !panning_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
      if (!boxSelecting_)
      {
        boxSelecting_ = true;
        boxSelectStart_ = ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left];
      }

      const ImVec2 boxMin(
          std::min(boxSelectStart_.x, io.MousePos.x), std::min(boxSelectStart_.y, io.MousePos.y));
      const ImVec2 boxMax(
          std::max(boxSelectStart_.x, io.MousePos.x), std::max(boxSelectStart_.y, io.MousePos.y));

      drawList->AddRectFilled(boxMin, boxMax, IM_COL32(255, 176, 46, 36));
      drawList->AddRect(boxMin, boxMax, IM_COL32(255, 176, 46, 190));

      selection_.clear();
      for (const auto &view : views)
      {
        const ImVec2 nodeMax(view.min.x + view.size.x, view.min.y + view.size.y);
        if (nodeMax.x >= boxMin.x && view.min.x <= boxMax.x &&
            nodeMax.y >= boxMin.y && view.min.y <= boxMax.y)
        {
          selection_.push_back(view.id);
        }
      }
    }
    else if (boxSelecting_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
      boxSelecting_ = false;
    }

    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !mouseOverNode && !linkDrag_.active)
    {
      selection_.clear();
      selectedVariable_ = -1;
    }

    // Finish a link drag.
    if (linkDrag_.active && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
      bool connected = false;

      if (hasHoveredPin_ &&
          hoveredPinNode_ != linkDrag_.node &&
          hoveredPinIsOutput_ != linkDrag_.fromOutput &&
          hoveredPinKind_ == linkDrag_.kind)
      {
        const ValueType fromType = linkDrag_.fromOutput ? linkDrag_.type : hoveredPinType_;
        const ValueType toType = linkDrag_.fromOutput ? hoveredPinType_ : linkDrag_.type;

        if (linkDrag_.kind == BlueprintLinkKind::Exec || value_type_convertible(fromType, toType))
        {
          BlueprintLink link;
          link.kind = linkDrag_.kind;
          if (linkDrag_.fromOutput)
          {
            link.from = {linkDrag_.node, linkDrag_.pin};
            link.to = {hoveredPinNode_, hoveredPinName_};
          }
          else
          {
            link.from = {hoveredPinNode_, hoveredPinName_};
            link.to = {linkDrag_.node, linkDrag_.pin};
          }

          pending_.linksToAdd.push_back(link);
          connected = true;
        }
        else
        {
          status_ = std::string("Cannot connect ") + value_type_name(fromType) + " to " + value_type_name(toType);
          statusIsError_ = true;
        }
      }

      if (!connected && canvasHovered)
      {
        // Dropping a wire on empty canvas offers the palette, the same way
        // Unreal does, and wires the spawned node up automatically.
        palettePendingLink_ = linkDrag_;
        paletteGraphPosition_ = to_graph(io.MousePos);
        openPaletteRequested_ = true;
      }

      linkDrag_ = LinkDrag();
    }

    if (rightPressOnCanvas_ && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
      const float dx = io.MousePos.x - rightPressPosition_.x;
      const float dy = io.MousePos.y - rightPressPosition_.y;
      if ((dx * dx + dy * dy) < 25.0f && canvasRegionHovered && !mouseOverNode && !openNodeContextMenu_)
      {
        palettePendingLink_ = LinkDrag();
        paletteGraphPosition_ = to_graph(io.MousePos);
        openPaletteRequested_ = true;
      }
      rightPressOnCanvas_ = false;
    }

    ImGui::PopFont();

    // Keyboard shortcuts, only while this panel owns focus.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
      if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))
      {
        if (selectedLink_.has_value())
        {
          pending_.linksToRemove.push_back(*selectedLink_);
          selectedLink_.reset();
        }
        for (BlueprintNodeId id : selection_)
        {
          pending_.nodesToDelete.push_back(id);
        }
      }

      if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.WantTextInput)
      {
        request_frame_all();
      }

      const bool commandDown = io.KeyCtrl || io.KeySuper;
      if (commandDown && ImGui::IsKeyPressed(ImGuiKey_D) && !selection_.empty())
      {
        pending_.duplicateSelection = true;
      }
      if (commandDown && ImGui::IsKeyPressed(ImGuiKey_S) && dirty_)
      {
        save_asset(context);
      }
    }

    draw_node_context_menu(context);
    draw_palette(context);

    ImGui::EndChild();
  }

  // ---------------------------------------------------------------------------
  // Node context menu
  // ---------------------------------------------------------------------------

  void BlueprintEditorPlugin::draw_node_context_menu(EditorPluginContext &context)
  {
    (void)context;

    if (openNodeContextMenu_)
    {
      ImGui::OpenPopup("##blueprint_node_menu");
      openNodeContextMenu_ = false;
    }

    if (!ImGui::BeginPopup("##blueprint_node_menu"))
    {
      return;
    }

    BlueprintGraph *graph = active_graph();
    BlueprintNode *node = graph != nullptr ? graph->find_node(contextMenuNode_) : nullptr;
    if (graph == nullptr || node == nullptr)
    {
      ImGui::EndPopup();
      return;
    }

    BlueprintNodeSignature signature;
    resolve_blueprint_node_signature(signature_context(), *node, signature);
    const BlueprintNodeType *type = BlueprintNodeRegistry::instance().find(node->type);
    const std::string title =
        !signature.title.empty()
            ? signature.title
            : (type != nullptr ? type->displayName : node->type);

    ImGui::TextDisabled("%s", title.c_str());
    ImGui::Separator();

    const std::size_t count = std::max<std::size_t>(selection_.size(), 1);
    const std::string deleteLabel =
        count > 1 ? (std::string(ICON_FA_TRASH "  Delete ") + std::to_string(count) + " Nodes")
                  : std::string(ICON_FA_TRASH "  Delete");

    if (ImGui::MenuItem(deleteLabel.c_str(), "Del"))
    {
      if (selection_.empty())
      {
        pending_.nodesToDelete.push_back(contextMenuNode_);
      }
      else
      {
        for (BlueprintNodeId id : selection_)
        {
          pending_.nodesToDelete.push_back(id);
        }
      }
    }

    if (ImGui::MenuItem(ICON_FA_PLUS "  Duplicate", "Ctrl+D"))
    {
      if (selection_.empty())
      {
        select_only(contextMenuNode_);
      }
      pending_.duplicateSelection = true;
    }

    ImGui::Separator();

    const bool hasLinks = std::any_of(
        graph->links.begin(),
        graph->links.end(),
        [this](const BlueprintLink &link)
        { return link.from.node == contextMenuNode_ || link.to.node == contextMenuNode_; });

    if (ImGui::MenuItem("Break All Links", nullptr, false, hasLinks))
    {
      for (const auto &link : graph->links)
      {
        if (link.from.node == contextMenuNode_ || link.to.node == contextMenuNode_)
        {
          pending_.linksToRemove.push_back(link);
        }
      }
    }

    ImGui::EndPopup();
  }

  // ---------------------------------------------------------------------------
  // Node palette
  // ---------------------------------------------------------------------------

  void BlueprintEditorPlugin::draw_palette(EditorPluginContext &context)
  {
    (void)context;

    if (openPaletteRequested_)
    {
      ImGui::OpenPopup("##blueprint_palette");
      paletteSearch_.fill('\0');
      paletteFocusSearch_ = true;
      openPaletteRequested_ = false;
    }

    if (!ImGui::BeginPopup("##blueprint_palette"))
    {
      return;
    }

    ImGui::TextUnformatted("Add Node");
    ImGui::Separator();

    if (paletteFocusSearch_)
    {
      ImGui::SetKeyboardFocusHere();
      paletteFocusSearch_ = false;
    }
    ImGui::SetNextItemWidth(280.0f);
    ImGui::InputTextWithHint("##search", "Search nodes...", paletteSearch_.data(), paletteSearch_.size());

    const std::string needle = lowered(paletteSearch_.data());
    const bool searching = !needle.empty();

    /// Spawn, then wire the new node back to wherever the drag started.
    const auto spawn_and_connect = [this](const std::string &typeName, const nlohmann::json &config)
    {
      const BlueprintNodeId created = spawn_node(typeName, paletteGraphPosition_, config);
      if (created == kInvalidBlueprintNode)
      {
        return;
      }

      select_only(created);

      if (!palettePendingLink_.active)
      {
        return;
      }

      BlueprintGraph *graph = active_graph();
      const BlueprintNode *node = graph != nullptr ? graph->find_node(created) : nullptr;
      if (node == nullptr)
      {
        return;
      }

      BlueprintNodeSignature signature;
      if (!resolve_blueprint_node_signature(signature_context(), *node, signature))
      {
        return;
      }

      BlueprintLink link;
      link.kind = palettePendingLink_.kind;

      if (palettePendingLink_.fromOutput)
      {
        link.from = {palettePendingLink_.node, palettePendingLink_.pin};
        if (link.kind == BlueprintLinkKind::Exec)
        {
          if (signature.execInputs.empty())
          {
            return;
          }
          link.to = {created, signature.execInputs.front()};
        }
        else
        {
          for (const auto &pinSpec : signature.dataInputs)
          {
            if (value_type_convertible(palettePendingLink_.type, pinSpec.type))
            {
              link.to = {created, pinSpec.name};
              break;
            }
          }
          if (link.to.pin.empty())
          {
            return;
          }
        }
      }
      else
      {
        link.to = {palettePendingLink_.node, palettePendingLink_.pin};
        if (link.kind == BlueprintLinkKind::Exec)
        {
          if (signature.execOutputs.empty())
          {
            return;
          }
          link.from = {created, signature.execOutputs.front()};
        }
        else
        {
          for (const auto &pinSpec : signature.dataOutputs)
          {
            if (value_type_convertible(pinSpec.type, palettePendingLink_.type))
            {
              link.from = {created, pinSpec.name};
              break;
            }
          }
          if (link.from.pin.empty())
          {
            return;
          }
        }
      }

      pending_.linksToAdd.push_back(link);
      palettePendingLink_ = LinkDrag();
    };

    ImGui::BeginChild("##palette_list", ImVec2(300.0f, 380.0f), ImGuiChildFlags_Borders);

    // Variable accessors first: they are what people reach for most.
    if (!blueprint_.variables.empty())
    {
      const bool open = searching || ImGui::CollapsingHeader("Variables", ImGuiTreeNodeFlags_DefaultOpen);
      if (open)
      {
        for (const auto &variable : blueprint_.variables)
        {
          const std::string lowerName = lowered(variable.name.c_str());
          if (searching && lowerName.find(needle) == std::string::npos)
          {
            continue;
          }

          const std::string getLabel = "Get " + variable.name;
          if (ImGui::Selectable(getLabel.c_str()))
          {
            spawn_and_connect("variable.get", nlohmann::json{{"variable", variable.name}});
            ImGui::CloseCurrentPopup();
          }

          const std::string setLabel = "Set " + variable.name;
          if (ImGui::Selectable(setLabel.c_str()))
          {
            spawn_and_connect("variable.set", nlohmann::json{{"variable", variable.name}});
            ImGui::CloseCurrentPopup();
          }
        }
      }
    }

    if (!blueprint_.functions.empty())
    {
      const bool open = searching || ImGui::CollapsingHeader("Call Function", ImGuiTreeNodeFlags_DefaultOpen);
      if (open)
      {
        for (const auto &function : blueprint_.functions)
        {
          const std::string lowerName = lowered(function.name.c_str());
          if (searching && lowerName.find(needle) == std::string::npos)
          {
            continue;
          }

          if (ImGui::Selectable(function.name.c_str()))
          {
            spawn_and_connect("function.call", nlohmann::json{{"function", function.name}});
            ImGui::CloseCurrentPopup();
          }
        }
      }
    }

    std::string currentCategory;
    bool categoryOpen = false;
    for (const auto *type : BlueprintNodeRegistry::instance().all())
    {
      if (type->hidden || !matches_search(*type, needle))
      {
        continue;
      }

      if (type->category != currentCategory)
      {
        currentCategory = type->category;
        categoryOpen = searching ||
                       ImGui::CollapsingHeader(currentCategory.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        if (searching)
        {
          ImGui::SeparatorText(currentCategory.c_str());
        }
      }

      if (!categoryOpen)
      {
        continue;
      }

      if (ImGui::Selectable(type->displayName.c_str()))
      {
        spawn_and_connect(type->name, nlohmann::json::object());
        ImGui::CloseCurrentPopup();
      }

      if (ImGui::IsItemHovered() && !type->tooltip.empty())
      {
        ImGui::SetTooltip("%s", type->tooltip.c_str());
      }
    }

    ImGui::EndChild();
    ImGui::EndPopup();
  }
}

namespace hades
{
  // ---------------------------------------------------------------------------
  // Toolbar, sidebar, details, messages
  // ---------------------------------------------------------------------------

  void BlueprintEditorPlugin::draw_toolbar(EditorPluginContext &context)
  {
    const std::string preview = hasAsset_ ? assetPath_ : std::string("<no Blueprint open>");

    ImGui::SetNextItemWidth(280.0f);
    if (ImGui::BeginCombo("##asset", preview.c_str()))
    {
      for (const auto &asset : assets_)
      {
        const bool selected = hasAsset_ && asset == assetPath_;
        if (ImGui::Selectable(asset.c_str(), selected) && !selected)
        {
          open_asset(context, asset);
        }
      }

      if (assets_.empty())
      {
        ImGui::TextDisabled("No .hbp files in this workspace");
      }
      ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS "  New"))
    {
      openNewAssetDialog_ = true;
      newAssetName_.fill('\0');
      newAssetError_.clear();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasAsset_ || !dirty_);
    if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save"))
    {
      save_asset(context);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasAsset_);
    if (ImGui::Button(ICON_FA_HAMMER "  Compile"))
    {
      compile_now(context);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasAsset_);
    ImGui::SetNextItemWidth(180.0f);
    const std::string graphPreview =
        activeFunction_ < 0
            ? std::string("Event Graph")
            : blueprint_.functions[static_cast<std::size_t>(activeFunction_)].name;
    if (ImGui::BeginCombo("##graph", graphPreview.c_str()))
    {
      if (ImGui::Selectable("Event Graph", activeFunction_ < 0))
      {
        activeFunction_ = -1;
        selection_.clear();
        selectedLink_.reset();
      }
      for (std::size_t i = 0; i < blueprint_.functions.size(); ++i)
      {
        const bool selected = activeFunction_ == static_cast<int>(i);
        if (ImGui::Selectable(blueprint_.functions[i].name.c_str(), selected))
        {
          activeFunction_ = static_cast<int>(i);
          selection_.clear();
          selectedLink_.reset();
        }
      }
      ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasAsset_);
    if (ImGui::Button(ICON_FA_EXPAND "  Fit"))
    {
      request_frame_all();
    }
    ImGui::SetItemTooltip("Frame the whole graph (F)");
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::SmallButton("-"))
    {
      zoom_ = std::clamp(zoom_ * 0.85f, 0.3f, 2.5f);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d%%", static_cast<int>(zoom_ * 100.0f));
    ImGui::SameLine();
    if (ImGui::SmallButton("+"))
    {
      zoom_ = std::clamp(zoom_ * 1.15f, 0.3f, 2.5f);
    }
    ImGui::SetItemTooltip("Scroll to pan, %s+scroll to zoom, Space or Alt drag to pan",
#ifdef __APPLE__
                          "Cmd"
#else
                          "Ctrl"
#endif
    );

    if (!status_.empty())
    {
      ImGui::SameLine();
      ImGui::TextColored(
          statusIsError_ ? ImVec4(0.94f, 0.42f, 0.36f, 1.0f) : ImVec4(0.62f, 0.78f, 0.55f, 1.0f),
          "%s%s",
          dirty_ ? "* " : "",
          status_.c_str());
    }
    else if (dirty_)
    {
      ImGui::SameLine();
      ImGui::TextDisabled("* unsaved");
    }
  }

  void BlueprintEditorPlugin::draw_sidebar(EditorPluginContext &context)
  {
    (void)context;

    ImGui::SeparatorText("Graphs");
    if (ImGui::Selectable("Event Graph", activeFunction_ < 0))
    {
      activeFunction_ = -1;
      selection_.clear();
    }

    ImGui::SeparatorText("Functions");
    if (ImGui::SmallButton(ICON_FA_PLUS "##add_function"))
    {
      BlueprintFunction function;
      function.name = "NewFunction";
      int suffix = 1;
      while (blueprint_.find_function(function.name) != nullptr)
      {
        function.name = "NewFunction" + std::to_string(++suffix);
      }

      // A function graph is unusable without its entry node, so place one.
      BlueprintNode entry;
      entry.id = blueprint_.allocate_node_id();
      entry.type = "function.entry";
      entry.x = -180.0f;
      entry.y = 0.0f;
      function.graph.nodes.push_back(std::move(entry));

      blueprint_.functions.push_back(std::move(function));
      activeFunction_ = static_cast<int>(blueprint_.functions.size()) - 1;
      selection_.clear();
      mark_dirty();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("add");

    for (std::size_t i = 0; i < blueprint_.functions.size(); ++i)
    {
      ImGui::PushID(static_cast<int>(i) + 5000);
      if (ImGui::Selectable(blueprint_.functions[i].name.c_str(), activeFunction_ == static_cast<int>(i)))
      {
        activeFunction_ = static_cast<int>(i);
        selection_.clear();
        selectedVariable_ = -1;
      }
      if (ImGui::BeginPopupContextItem("##function_menu"))
      {
        if (ImGui::MenuItem("Delete Function"))
        {
          blueprint_.functions.erase(blueprint_.functions.begin() + static_cast<long>(i));
          if (activeFunction_ >= static_cast<int>(blueprint_.functions.size()))
          {
            activeFunction_ = -1;
          }
          mark_dirty();
          ImGui::EndPopup();
          ImGui::PopID();
          break;
        }
        ImGui::EndPopup();
      }
      ImGui::PopID();
    }

    ImGui::SeparatorText("Variables");
    if (ImGui::SmallButton(ICON_FA_PLUS "##add_variable"))
    {
      BlueprintVariable variable;
      variable.name = "NewVar";
      int suffix = 1;
      while (blueprint_.find_variable(variable.name) != nullptr)
      {
        variable.name = "NewVar" + std::to_string(++suffix);
      }
      variable.type = ValueType::Float;
      variable.defaultValue = BlueprintValue::from_float(0.0f);
      blueprint_.variables.push_back(std::move(variable));
      selectedVariable_ = static_cast<int>(blueprint_.variables.size()) - 1;
      selection_.clear();
      mark_dirty();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("add");

    for (std::size_t i = 0; i < blueprint_.variables.size(); ++i)
    {
      const auto &variable = blueprint_.variables[i];
      ImGui::PushID(static_cast<int>(i) + 6000);

      const ImU32 colour = type_colour(variable.type);
      ImDrawList *drawList = ImGui::GetWindowDrawList();
      const ImVec2 cursor = ImGui::GetCursorScreenPos();
      drawList->AddCircleFilled(
          ImVec2(cursor.x + 6.0f, cursor.y + ImGui::GetTextLineHeight() * 0.5f), 4.5f, colour, 12);
      ImGui::Dummy(ImVec2(16.0f, 0.0f));
      ImGui::SameLine();

      if (ImGui::Selectable(variable.name.c_str(), selectedVariable_ == static_cast<int>(i)))
      {
        selectedVariable_ = static_cast<int>(i);
        selection_.clear();
      }

      if (ImGui::BeginPopupContextItem("##variable_menu"))
      {
        if (ImGui::MenuItem("Delete Variable"))
        {
          blueprint_.variables.erase(blueprint_.variables.begin() + static_cast<long>(i));
          selectedVariable_ = -1;
          mark_dirty();
          ImGui::EndPopup();
          ImGui::PopID();
          break;
        }
        ImGui::EndPopup();
      }
      ImGui::PopID();
    }

    ImGui::SeparatorText("Events");
    if (BlueprintGraph *graph = active_graph())
    {
      const BlueprintSignatureContext signatureContext = signature_context();
      for (const auto &node : graph->nodes)
      {
        const BlueprintNodeType *type = BlueprintNodeRegistry::instance().find(node.type);
        if (type == nullptr || type->kind != BlueprintNodeKind::Event)
        {
          continue;
        }

        BlueprintNodeSignature signature;
        resolve_blueprint_node_signature(signatureContext, node, signature);
        const std::string label =
            signature.title.empty() ? type->displayName : signature.title;

        ImGui::PushID(static_cast<int>(node.id) + 7000);
        if (ImGui::Selectable(label.c_str(), is_selected(node.id)))
        {
          focus_node(node.id);
        }

        if (ImGui::BeginPopupContextItem("##event_menu"))
        {
          if (ImGui::MenuItem("Focus"))
          {
            focus_node(node.id);
          }
          if (ImGui::MenuItem(ICON_FA_TRASH "  Delete"))
          {
            pending_.nodesToDelete.push_back(node.id);
          }
          ImGui::EndPopup();
        }
        ImGui::PopID();
      }
    }
  }

  namespace
  {
    // The types a per-node pin or an event parameter may take. Exec and
    // Wildcard are excluded: neither may survive into a compiled graph.
    constexpr int kPinTypeCount = 6;
    const char *const kPinTypeNames[kPinTypeCount] = {
        "bool", "int", "float", "string", "vector", "entity"};
    constexpr ValueType kPinTypeValues[kPinTypeCount] = {
        ValueType::Bool, ValueType::Int, ValueType::Float,
        ValueType::String, ValueType::Vector, ValueType::Entity};

    int pin_type_index(const std::string &name)
    {
      for (int i = 0; i < kPinTypeCount; ++i)
      {
        if (name == kPinTypeNames[i])
        {
          return i;
        }
      }
      return 2; // float
    }
  }

  std::vector<BlueprintNodeId> BlueprintEditorPlugin::event_graph_node_ids() const
  {
    std::vector<BlueprintNodeId> ids;
    ids.reserve(blueprint_.eventGraph.nodes.size());
    for (const auto &node : blueprint_.eventGraph.nodes)
    {
      ids.push_back(node.id);
    }
    return ids;
  }

  void BlueprintEditorPlugin::draw_pin_type_combo(
      BlueprintNode &node,
      const char *label,
      const char *configKey)
  {
    int current = pin_type_index(node.config.value(configKey, std::string("float")));
    if (ImGui::Combo(label, &current, kPinTypeNames, kPinTypeCount))
    {
      node.config[configKey] = std::string(kPinTypeNames[current]);
      mark_dirty();
    }
  }

  void BlueprintEditorPlugin::draw_event_parameters(BlueprintNode &node)
  {
    ImGui::SeparatorText("Parameters");
    ImGui::PushID("event_params");

    if (!node.config.contains("params") || !node.config["params"].is_array())
    {
      node.config["params"] = nlohmann::json::array();
    }

    auto &params = node.config["params"];

    for (std::size_t i = 0; i < params.size(); ++i)
    {
      ImGui::PushID(static_cast<int>(i));

      std::array<char, 96> buffer{};
      std::snprintf(buffer.data(), buffer.size(), "%s", params[i].value("name", std::string()).c_str());
      ImGui::SetNextItemWidth(110.0f);
      if (ImGui::InputText("##name", buffer.data(), buffer.size()))
      {
        params[i]["name"] = std::string(buffer.data());
        mark_dirty();
      }

      ImGui::SameLine();
      int current = pin_type_index(params[i].value("type", std::string("float")));
      ImGui::SetNextItemWidth(80.0f);
      if (ImGui::Combo("##type", &current, kPinTypeNames, kPinTypeCount))
      {
        params[i]["type"] = std::string(kPinTypeNames[current]);
        mark_dirty();
      }

      ImGui::SameLine();
      if (ImGui::SmallButton(ICON_FA_TRASH))
      {
        params.erase(params.begin() + static_cast<long>(i));
        mark_dirty();
        ImGui::PopID();
        break;
      }

      ImGui::PopID();
    }

    if (ImGui::SmallButton(ICON_FA_PLUS "  Add"))
    {
      nlohmann::json parameter;
      parameter["name"] = "Param" + std::to_string(params.size() + 1);
      parameter["type"] = "float";
      params.push_back(std::move(parameter));
      mark_dirty();
    }

    ImGui::TextDisabled("Scripts fill these with Blueprints::sendEvent.");
    ImGui::PopID();
  }

  void BlueprintEditorPlugin::draw_details(EditorPluginContext &context)
  {
    ImGui::SeparatorText("Details");

    if (!hasAsset_)
    {
      ImGui::TextWrapped("Open or create a Blueprint to start editing.");
      return;
    }

    // --- variable selected ---
    if (selectedVariable_ >= 0 && static_cast<std::size_t>(selectedVariable_) < blueprint_.variables.size())
    {
      auto &variable = blueprint_.variables[static_cast<std::size_t>(selectedVariable_)];

      std::array<char, 128> nameBuffer{};
      std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", variable.name.c_str());
      if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size()))
      {
        const std::string previous = variable.name;
        variable.name = nameBuffer.data();

        // Keep every accessor node pointing at the renamed variable.
        for (auto *graph : blueprint_.all_graphs())
        {
          for (auto &node : graph->nodes)
          {
            if ((node.type == "variable.get" || node.type == "variable.set") &&
                node.config.value("variable", std::string()) == previous)
            {
              node.config["variable"] = variable.name;
            }
          }
        }
        mark_dirty();
      }

      const char *typeNames[] = {"bool", "int", "float", "string", "vector", "entity"};
      const ValueType typeValues[] = {
          ValueType::Bool, ValueType::Int, ValueType::Float,
          ValueType::String, ValueType::Vector, ValueType::Entity};

      int current = 2;
      for (int i = 0; i < 6; ++i)
      {
        if (typeValues[i] == variable.type)
        {
          current = i;
        }
      }

      if (ImGui::Combo("Type", &current, typeNames, 6))
      {
        variable.type = typeValues[current];
        variable.defaultValue = variable.defaultValue.coerced_to(variable.type);
        mark_dirty();
      }

      switch (variable.type)
      {
      case ValueType::Bool:
      {
        bool value = variable.defaultValue.as_bool();
        if (ImGui::Checkbox("Default", &value))
        {
          variable.defaultValue = BlueprintValue::from_bool(value);
          mark_dirty();
        }
        break;
      }
      case ValueType::Int:
      {
        int value = variable.defaultValue.as_int();
        if (ImGui::DragInt("Default", &value))
        {
          variable.defaultValue = BlueprintValue::from_int(value);
          mark_dirty();
        }
        break;
      }
      case ValueType::Float:
      {
        float value = variable.defaultValue.as_float();
        if (ImGui::DragFloat("Default", &value, 0.05f))
        {
          variable.defaultValue = BlueprintValue::from_float(value);
          mark_dirty();
        }
        break;
      }
      case ValueType::Vector:
      {
        math::Vec3 value = variable.defaultValue.as_vector();
        float components[3] = {value.x, value.y, value.z};
        if (ImGui::DragFloat3("Default", components, 0.05f))
        {
          variable.defaultValue =
              BlueprintValue::from_vector(math::Vec3(components[0], components[1], components[2]));
          mark_dirty();
        }
        break;
      }
      case ValueType::String:
      {
        std::array<char, 256> buffer{};
        std::snprintf(buffer.data(), buffer.size(), "%s", variable.defaultValue.as_string().c_str());
        if (ImGui::InputText("Default", buffer.data(), buffer.size()))
        {
          variable.defaultValue = BlueprintValue::from_string(buffer.data());
          mark_dirty();
        }
        break;
      }
      default:
        ImGui::TextDisabled("Entity variables start unset.");
        break;
      }

      if (ImGui::Checkbox("Expose on instances", &variable.exposed))
      {
        mark_dirty();
      }
      ImGui::SetItemTooltip("Exposed variables can be overridden per entity from the inspector.");
      return;
    }

    // --- node selected ---
    if (selection_.size() == 1)
    {
      BlueprintGraph *graph = active_graph();
      BlueprintNode *node = graph != nullptr ? graph->find_node(selection_.front()) : nullptr;
      if (node != nullptr)
      {
        const BlueprintNodeType *type = BlueprintNodeRegistry::instance().find(node->type);
        ImGui::TextUnformatted(type != nullptr ? type->displayName.c_str() : node->type.c_str());
        ImGui::TextDisabled("%s", node->type.c_str());
        if (type != nullptr && !type->tooltip.empty())
        {
          ImGui::TextWrapped("%s", type->tooltip.c_str());
        }
        ImGui::Separator();

        if (nodeCommentOwner_ != node->id)
        {
          nodeCommentOwner_ = node->id;
          nodeCommentBuffer_.fill('\0');
          std::snprintf(nodeCommentBuffer_.data(), nodeCommentBuffer_.size(), "%s", node->comment.c_str());
        }
        if (ImGui::InputText("Comment", nodeCommentBuffer_.data(), nodeCommentBuffer_.size()))
        {
          node->comment = nodeCommentBuffer_.data();
          mark_dirty();
        }

        // Type-specific configuration.
        if (node->type == "variable.get" || node->type == "variable.set")
        {
          const std::string current = node->config.value("variable", std::string());
          if (ImGui::BeginCombo("Variable", current.empty() ? "<none>" : current.c_str()))
          {
            for (const auto &variable : blueprint_.variables)
            {
              if (ImGui::Selectable(variable.name.c_str(), variable.name == current))
              {
                node->config["variable"] = variable.name;
                mark_dirty();
              }
            }
            ImGui::EndCombo();
          }
        }
        else if (node->type == "function.call")
        {
          const std::string current = node->config.value("function", std::string());
          if (ImGui::BeginCombo("Function", current.empty() ? "<none>" : current.c_str()))
          {
            for (const auto &function : blueprint_.functions)
            {
              if (ImGui::Selectable(function.name.c_str(), function.name == current))
              {
                node->config["function"] = function.name;
                mark_dirty();
              }
            }
            ImGui::EndCombo();
          }
        }
        else if (node->type == "event.custom" || node->type == "flow.call_event")
        {
          std::array<char, 128> buffer{};
          std::snprintf(buffer.data(), buffer.size(), "%s", node->config.value("name", std::string()).c_str());
          if (ImGui::InputText("Event Name", buffer.data(), buffer.size()))
          {
            node->config["name"] = std::string(buffer.data());
            mark_dirty();
          }

          if (node->type == "event.custom")
          {
            draw_event_parameters(*node);
          }
          else
          {
            ImGui::TextDisabled("Arguments follow the Custom Event's parameters.");
          }
        }
        else if (node->type == "script.send" || node->type == "script.broadcast")
        {
          draw_pin_type_combo(*node, "Value Type", "valueType");
        }
        else if (node->type == "script.call")
        {
          draw_pin_type_combo(*node, "Value Type", "valueType");
          draw_pin_type_combo(*node, "Result Type", "resultType");
        }
        else if (node->type == "flow.sequence")
        {
          int outputs = node->config.value("outputs", 2);
          if (ImGui::SliderInt("Outputs", &outputs, 1, 8))
          {
            node->config["outputs"] = outputs;
            mark_dirty();
          }
        }
        else if (node->type == "debug.print")
        {
          const char *levels[] = {"info", "warning", "error"};
          const std::string current = node->config.value("level", std::string("info"));
          int index = current == "warning" ? 1 : (current == "error" ? 2 : 0);
          if (ImGui::Combo("Level", &index, levels, 3))
          {
            node->config["level"] = std::string(levels[index]);
            mark_dirty();
          }
        }

        ImGui::Separator();
        if (ImGui::Button(ICON_FA_TRASH "  Delete Node"))
        {
          pending_.nodesToDelete.push_back(node->id);
        }
        return;
      }
    }

    if (selection_.size() > 1)
    {
      ImGui::Text("%d nodes selected", static_cast<int>(selection_.size()));
      if (ImGui::Button(ICON_FA_TRASH "  Delete Selected"))
      {
        for (BlueprintNodeId id : selection_)
        {
          pending_.nodesToDelete.push_back(id);
        }
      }
      return;
    }

    // --- function signature ---
    if (BlueprintFunction *function = active_function())
    {
      std::array<char, 128> nameBuffer{};
      std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", function->name.c_str());
      if (ImGui::InputText("Function", nameBuffer.data(), nameBuffer.size()))
      {
        const std::string previous = function->name;
        function->name = nameBuffer.data();
        for (auto *graph : blueprint_.all_graphs())
        {
          for (auto &node : graph->nodes)
          {
            if (node.type == "function.call" &&
                node.config.value("function", std::string()) == previous)
            {
              node.config["function"] = function->name;
            }
          }
        }
        mark_dirty();
      }

      if (ImGui::Checkbox("Allow recursion", &function->allowRecursion))
      {
        mark_dirty();
      }

      const auto draw_parameters = [&](const char *label, std::vector<BlueprintVariable> &parameters)
      {
        ImGui::SeparatorText(label);
        ImGui::PushID(label);

        for (std::size_t i = 0; i < parameters.size(); ++i)
        {
          ImGui::PushID(static_cast<int>(i));
          std::array<char, 96> buffer{};
          std::snprintf(buffer.data(), buffer.size(), "%s", parameters[i].name.c_str());
          ImGui::SetNextItemWidth(110.0f);
          if (ImGui::InputText("##name", buffer.data(), buffer.size()))
          {
            parameters[i].name = buffer.data();
            mark_dirty();
          }

          ImGui::SameLine();
          const char *typeNames[] = {"bool", "int", "float", "string", "vector", "entity"};
          const ValueType typeValues[] = {
              ValueType::Bool, ValueType::Int, ValueType::Float,
              ValueType::String, ValueType::Vector, ValueType::Entity};
          int current = 2;
          for (int t = 0; t < 6; ++t)
          {
            if (typeValues[t] == parameters[i].type)
            {
              current = t;
            }
          }
          ImGui::SetNextItemWidth(80.0f);
          if (ImGui::Combo("##type", &current, typeNames, 6))
          {
            parameters[i].type = typeValues[current];
            parameters[i].defaultValue = parameters[i].defaultValue.coerced_to(parameters[i].type);
            mark_dirty();
          }

          ImGui::SameLine();
          if (ImGui::SmallButton(ICON_FA_TRASH))
          {
            parameters.erase(parameters.begin() + static_cast<long>(i));
            mark_dirty();
            ImGui::PopID();
            break;
          }
          ImGui::PopID();
        }

        if (ImGui::SmallButton(ICON_FA_PLUS "  Add"))
        {
          BlueprintVariable parameter;
          parameter.name = "Param" + std::to_string(parameters.size() + 1);
          parameter.type = ValueType::Float;
          parameter.defaultValue = BlueprintValue::from_float(0.0f);
          parameters.push_back(std::move(parameter));
          mark_dirty();
        }

        ImGui::PopID();
      };

      draw_parameters("Inputs", function->inputs);
      draw_parameters("Outputs", function->outputs);

      if (ImGui::Button("Add Return Node"))
      {
        spawn_node("function.result", ImVec2(200.0f, 0.0f));
      }
      return;
    }

    // --- asset properties ---
    std::array<char, 128> nameBuffer{};
    std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", blueprint_.name.c_str());
    if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size()))
    {
      blueprint_.name = nameBuffer.data();
      mark_dirty();
    }

    std::array<char, 256> descriptionBuffer{};
    std::snprintf(descriptionBuffer.data(), descriptionBuffer.size(), "%s", blueprint_.description.c_str());
    if (ImGui::InputText("Description", descriptionBuffer.data(), descriptionBuffer.size()))
    {
      blueprint_.description = descriptionBuffer.data();
      mark_dirty();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Nodes: %d", static_cast<int>(blueprint_.eventGraph.nodes.size()));
    ImGui::TextDisabled("Variables: %d", static_cast<int>(blueprint_.variables.size()));
    ImGui::TextDisabled("Functions: %d", static_cast<int>(blueprint_.functions.size()));

    if (context.editor.state.isPlaying)
    {
      ImGui::Separator();
      ImGui::SeparatorText("Live Instances");
      for (const auto &view : context.blueprintRuntime.instances())
      {
        if (view.assetPath != assetPath_ || view.instance == nullptr)
        {
          continue;
        }

        ImGui::Text("%s", view.entityName.c_str());
        for (std::size_t i = 0; i < view.instance->variables.size() &&
                                i < compiled_.blueprint.variables.size();
             ++i)
        {
          ImGui::BulletText(
              "%s = %s",
              compiled_.blueprint.variables[i].name.c_str(),
              view.instance->variables[i].to_display_string().c_str());
        }
        if (!view.instance->latentActions.empty())
        {
          ImGui::TextDisabled("  %d pending delay(s)", static_cast<int>(view.instance->latentActions.size()));
        }
      }
    }
  }

  void BlueprintEditorPlugin::draw_messages(EditorPluginContext &context)
  {
    ImGui::BeginChild("##blueprint_messages", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);

    if (!compiledValid_)
    {
      ImGui::TextDisabled("Not compiled yet — press Compile to validate this graph.");
      ImGui::EndChild();
      return;
    }

    if (compiled_.messages.empty())
    {
      ImGui::TextColored(ImVec4(0.62f, 0.78f, 0.55f, 1.0f), ICON_FA_CIRCLE_INFO "  No problems found.");
      ImGui::EndChild();
      return;
    }

    for (std::size_t i = 0; i < compiled_.messages.size(); ++i)
    {
      const auto &message = compiled_.messages[i];
      ImGui::PushID(static_cast<int>(i));

      const bool isError = message.is_error();
      ImGui::TextColored(
          isError ? ImVec4(0.94f, 0.42f, 0.36f, 1.0f) : ImVec4(0.94f, 0.79f, 0.36f, 1.0f),
          "%s",
          isError ? ICON_FA_CIRCLE_EXCLAMATION : ICON_FA_TRIANGLE_EXCLAMATION);
      ImGui::SameLine();

      std::string label = message.text;
      if (!message.graph.empty())
      {
        label = message.graph + ": " + label;
      }

      if (ImGui::Selectable(label.c_str()) && message.node != kInvalidBlueprintNode)
      {
        // Jump to the graph the message belongs to, then centre on the node.
        if (message.graph.empty())
        {
          activeFunction_ = -1;
        }
        else
        {
          for (std::size_t f = 0; f < blueprint_.functions.size(); ++f)
          {
            if (blueprint_.functions[f].name == message.graph)
            {
              activeFunction_ = static_cast<int>(f);
              break;
            }
          }
        }
        focus_node(message.node);
      }

      ImGui::PopID();
    }

    (void)context;
    ImGui::EndChild();
  }

  void BlueprintEditorPlugin::draw_new_asset_dialog(EditorPluginContext &context)
  {
    if (openNewAssetDialog_)
    {
      ImGui::OpenPopup("New Blueprint##blueprint_new");
      openNewAssetDialog_ = false;
    }

    if (!ImGui::BeginPopupModal("New Blueprint##blueprint_new", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      return;
    }

    ImGui::TextUnformatted("Created under Blueprints/ in the current workspace.");
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputTextWithHint("##name", "PlayerController", newAssetName_.data(), newAssetName_.size());

    if (!newAssetError_.empty())
    {
      ImGui::TextColored(ImVec4(0.94f, 0.42f, 0.36f, 1.0f), "%s", newAssetError_.c_str());
    }

    if (ImGui::Button("Create", ImVec2(120.0f, 0.0f)))
    {
      const std::string name(newAssetName_.data());
      if (name.empty())
      {
        newAssetError_ = "Give the Blueprint a name.";
      }
      else if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
      {
        newAssetError_ = "Names cannot contain path separators.";
      }
      else
      {
        const std::string relative = std::string("Blueprints/") + name + kBlueprintFileExtension;
        const auto absolute = context.workspacePath / relative;

        if (std::filesystem::exists(absolute))
        {
          newAssetError_ = "A Blueprint with that name already exists.";
        }
        else
        {
          const Blueprint created = make_starter_blueprint(name);
          std::string error;
          if (!save_blueprint(absolute, created, &error))
          {
            newAssetError_ = error;
          }
          else
          {
            refresh_assets(context.workspacePath);
            open_asset(context, relative);
            context.editor.log_info("Created Blueprint " + relative);
            ImGui::CloseCurrentPopup();
          }
        }
      }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
    {
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  // ---------------------------------------------------------------------------
  // Frame entry point
  // ---------------------------------------------------------------------------

  void BlueprintEditorPlugin::render(EditorPluginContext &context)
  {
    if (!pending_open_request().empty())
    {
      const std::string requested = pending_open_request();
      pending_open_request().clear();
      visible_ = true;
      focusRequested_ = true;
      refresh_assets(context.workspacePath);
      open_asset(context, requested);
    }

    if (!visible_)
    {
      return;
    }

    if (focusRequested_)
    {
      ImGui::SetNextWindowFocus();
      focusRequested_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(1180.0f, 720.0f), ImGuiCond_FirstUseEver);

    bool open = visible_;
    if (!ImGui::Begin(ICON_FA_DIAGRAM_PROJECT "  Blueprint Editor", &open))
    {
      visible_ = open;
      ImGui::End();
      return;
    }
    visible_ = open;

    if (workspaceRoot_ != context.workspacePath || (ImGui::GetTime() - assetsRefreshedAt_) > 2.0)
    {
      refresh_assets(context.workspacePath);
    }

    draw_toolbar(context);
    draw_new_asset_dialog(context);
    ImGui::Separator();

    constexpr float kSidebarWidth = 210.0f;
    constexpr float kDetailsWidth = 300.0f;
    constexpr float kMessagesHeight = 150.0f;

    ImGui::BeginChild("##blueprint_sidebar", ImVec2(kSidebarWidth, 0.0f), ImGuiChildFlags_Borders);
    if (hasAsset_)
    {
      draw_sidebar(context);
    }
    else
    {
      ImGui::TextWrapped("No Blueprint open.");
      ImGui::Spacing();
      ImGui::TextWrapped("Use New to create one, or pick an existing asset from the toolbar.");
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##blueprint_centre", ImVec2(-(kDetailsWidth + 8.0f), 0.0f));
    ImGui::BeginChild("##blueprint_canvas_holder", ImVec2(0.0f, -kMessagesHeight));
    if (hasAsset_)
    {
      draw_canvas(context);
    }
    else
    {
      ImGui::Dummy(ImGui::GetContentRegionAvail());
    }
    ImGui::EndChild();
    draw_messages(context);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##blueprint_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    draw_details(context);
    ImGui::EndChild();

    // Applied once every panel has had a chance to queue work, so a delete
    // requested from the sidebar, the canvas or the details panel all take
    // effect on the same frame.
    apply_pending_edits();

    ImGui::End();
  }

  HADES_REGISTER_EDITOR_PLUGIN(BlueprintEditorPlugin, 55)
}
