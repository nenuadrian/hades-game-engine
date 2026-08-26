// Headless smoke test for the editor's ImGui panels.
//
// Runs the Blueprint editor panel against Dear ImGui's null backend and drives
// a deterministic sweep of mouse input across it. Nothing is asserted about
// what is drawn -- the point is that ImGui's own IM_ASSERT checks (unbalanced
// Begin/End, PushID/PopID, PushFont/PopFont, ID collisions in some builds) run
// over the real draw path on every frame, which no unit test of the graph model
// can cover.
//
// Registered with ctest as `EditorSmoke`.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#include "imgui.h"
#include "imgui_impl_null.h"
#include "imgui_impl_null.cpp"

#include "editor/blueprint/blueprint_editor_panel.hpp"
#include "editor/editor.hpp"
#include "engine/blueprint/blueprint_asset.hpp"
#include "engine/blueprint/blueprint_runtime.hpp"
#include "engine/components/blueprint_component.hpp"
#include "engine/components/name_component.hpp"
#include "engine/core/ecs/component_manager.hpp"
#include "engine/core/ecs/entity_factory.hpp"
#include "engine/core/ecs/entity_manager.hpp"
#include "engine/runtime/script_runtime.hpp"

namespace
{
  using namespace hades;

  BlueprintNodeId add_node(
      Blueprint &blueprint,
      BlueprintGraph &graph,
      const char *type,
      float x,
      float y,
      nlohmann::json config = nlohmann::json::object())
  {
    BlueprintNode node;
    node.id = blueprint.allocate_node_id();
    node.type = type;
    node.x = x;
    node.y = y;
    node.config = std::move(config);
    graph.nodes.push_back(std::move(node));
    return graph.nodes.back().id;
  }

  void link_exec(BlueprintGraph &graph, BlueprintNodeId a, const char *ap, BlueprintNodeId b, const char *bp = "exec")
  {
    graph.links.push_back({BlueprintLinkKind::Exec, {a, ap}, {b, bp}});
  }

  void link_data(BlueprintGraph &graph, BlueprintNodeId a, const char *ap, BlueprintNodeId b, const char *bp)
  {
    graph.links.push_back({BlueprintLinkKind::Data, {a, ap}, {b, bp}});
  }

  /// A graph that touches every pin type, an inline literal of each editable
  /// kind, a wildcard node, a loop, a latent node, a user function, a custom
  /// event with a declared payload and the script bridge nodes -- so the layout
  /// and drawing code, and every details-panel editor, has to handle them.
  Blueprint build_coverage_blueprint()
  {
    Blueprint blueprint;
    blueprint.name = "Smoke";
    blueprint.description = "Every pin type in one graph.";

    const char *names[] = {"Flag", "Count", "Speed", "Label", "Home", "Other"};
    const ValueType types[] = {
        ValueType::Bool, ValueType::Int, ValueType::Float,
        ValueType::String, ValueType::Vector, ValueType::Entity};
    for (int i = 0; i < 6; ++i)
    {
      BlueprintVariable variable;
      variable.name = names[i];
      variable.type = types[i];
      variable.defaultValue = BlueprintValue::default_for(types[i]);
      variable.exposed = (i % 2) == 0;
      blueprint.variables.push_back(std::move(variable));
    }

    BlueprintFunction function;
    function.name = "Scale";
    BlueprintVariable input;
    input.name = "Value";
    input.type = ValueType::Float;
    function.inputs.push_back(input);
    BlueprintVariable output;
    output.name = "Result";
    output.type = ValueType::Float;
    function.outputs.push_back(output);
    blueprint.functions.push_back(std::move(function));

    {
      BlueprintGraph &graph = blueprint.functions.back().graph;
      const auto entry = add_node(blueprint, graph, "function.entry", 40.0f, 60.0f);
      const auto multiply = add_node(blueprint, graph, "math.multiply", 240.0f, 140.0f);
      const auto result = add_node(blueprint, graph, "function.result", 460.0f, 60.0f);
      link_exec(graph, entry, "exec", result);
      link_data(graph, entry, "Value", multiply, "a");
      link_data(graph, multiply, "result", result, "Result");
    }

    BlueprintGraph &graph = blueprint.eventGraph;

    const auto tick = add_node(blueprint, graph, "event.tick", 60.0f, 60.0f);
    const auto sequence = add_node(blueprint, graph, "flow.sequence", 260.0f, 60.0f, {{"outputs", 3}});
    const auto branch = add_node(blueprint, graph, "flow.branch", 460.0f, 40.0f);
    const auto print = add_node(blueprint, graph, "debug.print", 660.0f, 40.0f, {{"level", "warning"}});
    const auto loop = add_node(blueprint, graph, "flow.for_loop", 460.0f, 200.0f);
    const auto offset = add_node(blueprint, graph, "transform.add_offset", 700.0f, 200.0f);
    const auto delay = add_node(blueprint, graph, "flow.delay", 460.0f, 380.0f);
    const auto call = add_node(blueprint, graph, "function.call", 660.0f, 380.0f, {{"function", "Scale"}});

    const auto getFlag = add_node(blueprint, graph, "variable.get", 60.0f, 260.0f, {{"variable", "Flag"}});
    const auto setSpeed = add_node(blueprint, graph, "variable.set", 260.0f, 500.0f, {{"variable", "Speed"}});
    const auto select = add_node(blueprint, graph, "logic.select", 60.0f, 380.0f);
    const auto makeVector = add_node(blueprint, graph, "vector.make", 260.0f, 300.0f);
    const auto concat = add_node(blueprint, graph, "string.concat", 60.0f, 560.0f);
    const auto findEntity = add_node(blueprint, graph, "entity.find_by_name", 60.0f, 660.0f);

    link_exec(graph, tick, "exec", sequence);
    link_exec(graph, sequence, "then0", branch);
    link_exec(graph, sequence, "then1", loop);
    link_exec(graph, sequence, "then2", delay);
    link_exec(graph, branch, "true", print);
    link_exec(graph, loop, "loopBody", offset);
    link_exec(graph, delay, "completed", call);
    link_exec(graph, call, "then", setSpeed);

    link_data(graph, getFlag, "value", branch, "condition");
    link_data(graph, concat, "result", print, "text");
    link_data(graph, makeVector, "vector", offset, "delta");
    link_data(graph, select, "result", makeVector, "x");
    link_data(graph, findEntity, "entity", offset, "target");
    link_data(graph, call, "Result", setSpeed, "value");

    // The script bridge: a custom event with a declared payload, the Call Event
    // that mirrors its parameters as arguments, and one of each script node.
    // Each has a details-panel editor of its own (parameter list, type combos).
    const auto damaged = add_node(
        blueprint, graph, "event.custom", 900.0f, 60.0f,
        {{"name", "Damaged"},
         {"params", nlohmann::json::array({{{"name", "amount"}, {"type", "float"}},
                                           {{"name", "source"}, {"type", "entity"}}})}});
    const auto raise = add_node(blueprint, graph, "flow.call_event", 900.0f, 240.0f, {{"name", "Damaged"}});
    const auto sendMessage = add_node(blueprint, graph, "script.send", 1140.0f, 60.0f, {{"valueType", "string"}});
    const auto callScript = add_node(
        blueprint, graph, "script.call", 1140.0f, 240.0f,
        {{"valueType", "float"}, {"resultType", "vector"}});
    const auto broadcast = add_node(blueprint, graph, "script.broadcast", 1140.0f, 460.0f, {{"valueType", "int"}});

    link_exec(graph, damaged, "exec", sendMessage);
    link_exec(graph, sendMessage, "then", callScript);
    link_exec(graph, callScript, "then", broadcast);
    link_data(graph, damaged, "source", sendMessage, "target");
    link_data(graph, damaged, "amount", callScript, "value");

    return blueprint;
  }
}

int main()
{
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1600.0f, 1000.0f);
  io.DeltaTime = 1.0f / 60.0f;
  io.IniFilename = nullptr;

  ImGui_ImplNullPlatform_Init();
  ImGui_ImplNullRender_Init();

  // A throwaway workspace holding one rich Blueprint.
  std::error_code errorCode;
  const auto workspace =
      std::filesystem::temp_directory_path() / "hades-editor-smoke";
  std::filesystem::remove_all(workspace, errorCode);
  std::filesystem::create_directories(workspace / ".hades" / "worlds", errorCode);

  const std::string relativeAsset = "Blueprints/Smoke.hbp";
  std::string saveError;
  if (!hades::save_blueprint(workspace / relativeAsset, build_coverage_blueprint(), &saveError))
  {
    std::fprintf(stderr, "editor smoke: could not write fixture: %s\n", saveError.c_str());
    return 1;
  }

  hades::Editor editor;
  hades::EntityManager entityManager;
  hades::ComponentManager componentManager(&entityManager);
  hades::ScriptRuntime scriptRuntime;
  hades::BlueprintRuntime blueprintRuntime;

  const auto world = hades::EntityFactory::createWorld(entityManager, componentManager, "Smoke", true);
  const auto entity = hades::EntityFactory::createCube(entityManager, componentManager, world);
  componentManager.getComponent<hades::NameComponent>(entity).value = "Subject";
  {
    hades::BlueprintComponent component;
    hades::BlueprintAttachment attachment;
    attachment.assetPath = relativeAsset;
    component.attachments.push_back(std::move(attachment));
    componentManager.addComponent(entity, component);
  }

  hades::BlueprintEditorPlugin panel;
  hades::request_blueprint_editor_open(relativeAsset);

  // Phase 1 fuzzes: a fast Lissajous sweep with presses, drags, wheel and
  // right-clicks, purely to shake out crashes and ImGui stack imbalance.
  // Phase 2 proves the canvas is actually interactive: the cursor parks on a
  // coarse grid, dwelling several frames per point (ImGui only hands hover to
  // an AllowOverlap item on the second consecutive frame) and clicking. If node
  // hit-testing regresses, nothing ever gets selected and the run fails.
  constexpr int kFuzzFrames = 240;
  constexpr int kDwellFrames = 5;
  constexpr int kGridColumns = 12;
  constexpr int kGridRows = 8;
  constexpr int kProbeFrames = kGridColumns * kGridRows * kDwellFrames;
  constexpr int kFrames = kFuzzFrames + kProbeFrames;

  // Only a selection established during the probe phase counts, and only after
  // a probe click has been seen to clear the selection first. Otherwise a
  // selection made during phase 1 (which sweeps the sidebar's event list, and
  // selects through that) would mask a completely dead canvas.
  std::size_t maxSelected = 0;
  bool probeSawEmptySelection = false;

  for (int frame = 0; frame < kFrames; ++frame)
  {
    if (frame < kFuzzFrames)
    {
      const float t = static_cast<float>(frame) * 0.11f;
      const float x = 800.0f + 700.0f * std::sin(t * 1.7f);
      const float y = 500.0f + 440.0f * std::sin(t * 1.1f + 0.7f);
      io.AddMousePosEvent(x, y);

      const int phase = frame % 8;
      io.AddMouseButtonEvent(ImGuiMouseButton_Left, phase == 2 || phase == 3);
      io.AddMouseButtonEvent(ImGuiMouseButton_Right, phase == 6);
      io.AddMouseWheelEvent(0.0f, (frame % 40 == 0) ? 1.0f : ((frame % 40 == 20) ? -1.0f : 0.0f));
    }
    else
    {
      const int probe = (frame - kFuzzFrames) / kDwellFrames;
      const int dwell = (frame - kFuzzFrames) % kDwellFrames;
      const int column = probe % kGridColumns;
      const int row = probe / kGridColumns;

      // Aim inside the canvas only. Clicking the sidebar's event list would
      // also change the selection, which would make this a useless canary.
      const ImVec2 origin = panel.canvas_origin();
      const ImVec2 size = panel.canvas_size();
      const float x = origin.x + (size.x / (kGridColumns + 1)) * static_cast<float>(column + 1);
      const float y = origin.y + (size.y / (kGridRows + 1)) * static_cast<float>(row + 1);
      io.AddMousePosEvent(x, y);

      // Settle for two frames, then press and release on the spot.
      io.AddMouseButtonEvent(ImGuiMouseButton_Left, dwell == 2 || dwell == 3);
      io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
      io.AddMouseWheelEvent(0.0f, 0.0f);
    }

    ImGui_ImplNullPlatform_NewFrame();
    ImGui_ImplNullRender_NewFrame();
    ImGui::NewFrame();

    // Exercise both the idle and the "play mode" details path.
    editor.state.isPlaying = (frame > 160);

    hades::EditorPluginContext context{
        editor,
        io.DeltaTime,
        workspace,
        entityManager,
        componentManager,
        scriptRuntime,
        blueprintRuntime,
    };

    panel.set_visible(editor, true);
    panel.render(context);

    if (frame >= kFuzzFrames)
    {
      const std::size_t selected = panel.selected_node_count();
      if (selected == 0)
      {
        probeSawEmptySelection = true;
      }
      else if (probeSawEmptySelection)
      {
        maxSelected = std::max(maxSelected, selected);
      }
    }

    ImGui::Render();
  }

  // Phase 3: select every node in turn and render its details panel. The probe
  // grid only reaches whatever it happens to land on, and the per-node config
  // editors (custom event parameters, the script nodes' type combos) each push
  // their own ImGui ID stacks and widgets that nothing else exercises.
  std::size_t inspectedNodes = 0;
  {
    const auto nodeIds = panel.event_graph_node_ids();
    inspectedNodes = nodeIds.size();
    for (BlueprintNodeId id : nodeIds)
    {
      for (int repeat = 0; repeat < 2; ++repeat)
      {
        io.AddMousePosEvent(-1.0f, -1.0f);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);

        ImGui_ImplNullPlatform_NewFrame();
        ImGui_ImplNullRender_NewFrame();
        ImGui::NewFrame();

        panel.select_only(id);

        hades::EditorPluginContext context{
            editor,
            io.DeltaTime,
            workspace,
            entityManager,
            componentManager,
            scriptRuntime,
            blueprintRuntime,
        };

        panel.set_visible(editor, true);
        panel.render(context);
        ImGui::Render();
      }
    }

    if (nodeIds.empty())
    {
      std::fprintf(stderr, "editor smoke: the fixture Blueprint has no nodes to inspect\n");
      return 1;
    }
  }

  ImGui_ImplNullRender_Shutdown();
  ImGui_ImplNullPlatform_Shutdown();
  ImGui::DestroyContext();

  std::filesystem::remove_all(workspace, errorCode);

  if (maxSelected == 0)
  {
    std::fprintf(
        stderr,
        "editor smoke: %d clicks inside the graph canvas never selected a node — "
        "node hit-testing is broken (ImGui hands overlapping items to whichever "
        "was submitted first unless SetNextItemAllowOverlap is used)\n",
        kGridColumns * kGridRows);
    return 1;
  }

  std::printf(
      "editor smoke: %d frames rendered cleanly, canvas selected up to %zu node(s), "
      "details panel rendered for %zu node(s)\n",
      kFrames,
      maxSelected,
      inspectedNodes);
  return 0;
}
