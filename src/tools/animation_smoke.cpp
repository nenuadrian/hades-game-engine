// Headless smoke test for the editor's two animation panels.
//
// The Animation panel (skeleton tree, dope sheet, curve editor, rig editor,
// clip browser) and the Animator panel (state-machine canvas) are ~8000 lines
// of Dear ImGui submission code. Everything they compute is unit tested;
// nothing they *draw* was, and ImGui misuse -- an unbalanced Begin/End,
// PushID/PopID, PushStyleVar/PopStyleVar or ImDrawList::PushClipRect, an ID
// collision, a clipper driven wrongly -- only shows up when a frame is
// actually submitted.
//
// So: stand up a synthetic workspace (a real skinned glTF, an authored clip
// with every interpolation mode and an event, an animator graph with layers,
// blend trees and conditional transitions), render both panels against ImGui's
// null backend for several hundred frames, and walk them through every drawing
// mode they have -- each tab, the curve editor on and off, a joint selected and
// none, playback running, a broken model, a graph loaded and none, a state and
// a transition selected.
//
// Every frame is checked for balance three ways. ImGui's own recoverable-error
// callback is routed into a failure list, with its asserts switched off so one
// bad frame does not abort before the rest are covered. Each panel's render is
// bracketed by a snapshot of ImGui's eleven context stacks. And once the frame
// is closed, every window's ImDrawList clip-rect stack -- which ImGui neither
// asserts on nor recovers -- is measured against a control window no panel
// draws into. Any mismatch names the frame and the phase, and the process
// exits non-zero.
//
// Drawing cleanly is not on its own evidence that anything was *reachable*, so
// the run also carries a set of canaries: the rig has to arrive, each tab has
// to become active, a joint pick has to be consumed, an auto-keyed gizmo edit
// has to reach the clip, and clicks have to select a state, a transition and a
// curve key handle. Each of those is a branch that silently went dead once
// already.
//
// Two areas are checked harder than "it drew", because both are only ever
// exercised by a user in a hurry and neither shows up in a screenshot:
//
//   The undo shortcuts. A real edit is made through each panel's own door --
//   a gizmo gesture with auto-key for the clip, a canvas drag for the graph --
//   and then Ctrl+Z and Ctrl+Shift+Z are typed at it. The assertion is that
//   the *model* moved back and then forward again, not that a stack got
//   shorter. Both are then typed again with a text field genuinely active, to
//   confirm the io.WantTextInput gate holds: without it, Ctrl+Z inside a name
//   field undoes the keystroke and the whole document.
//
//   The play-mode debug overlay. The canvas mirrors a live AnimatorInstance:
//   it borders the active state, runs a progress bar across it, colours the
//   arrow being crossfaded along -- including the any-state stub, which has no
//   source node and is drawn by a branch of its own -- and hands the parameter
//   rail the running instance's values instead of the authored ones. None of
//   that runs unless an entity is really playing the very graph the panel
//   holds, so the run stands one up, steps it, catches it mid-crossfade, fires
//   a trigger to light the any-state edge, and finally points the panel at a
//   different graph to confirm the overlay stands down rather than matching
//   another graph's nodes by name.
//
// Registered with ctest as `AnimationSmoke`.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_null.h"
#include "imgui_impl_null.cpp"

#include "editor/animation_edit_state.hpp"
#include "editor/animation_timeline.hpp"
#include "editor/editor.hpp"
#include "editor/plugins/animation_editor_plugin.hpp"
#include "editor/plugins/animator_graph_plugin.hpp"
#include "engine/animation/animation_clip.hpp"
#include "engine/animation/animation_clip_cache.hpp"
#include "engine/animation/animation_runtime.hpp"
#include "engine/animation/animation_types.hpp"
#include "engine/animation/animator_graph.hpp"
#include "engine/assets/model_asset.hpp"
#include "engine/assets/model_asset_cache.hpp"
#include "engine/blueprint/blueprint_runtime.hpp"
#include "engine/components/animator_component.hpp"
#include "engine/components/model_component.hpp"
#include "engine/components/name_component.hpp"
#include "engine/core/ecs/component_manager.hpp"
#include "engine/core/ecs/entity_factory.hpp"
#include "engine/core/ecs/entity_manager.hpp"
#include "engine/runtime/script_runtime.hpp"

namespace
{
  using namespace hades;

  // ---------------------------------------------------------------------------
  // Failure collection
  // ---------------------------------------------------------------------------

  struct Failure
  {
    int frame = 0;
    std::string phase;
    std::string message;
  };

  std::vector<Failure> g_failures;
  int g_frame = 0;
  const char *g_phase = "startup";

  void fail(const std::string &message)
  {
    // One line per distinct problem is plenty; a stack imbalance repeats on
    // every frame of the phase and would otherwise bury the summary.
    for (const Failure &existing : g_failures)
    {
      if (existing.phase == g_phase && existing.message == message)
      {
        return;
      }
    }
    g_failures.push_back({g_frame, g_phase, message});
  }

  /// ImGui routes every recoverable user error (stack imbalance, mismatched
  /// Begin/End, a popup left open) through this. Asserts are switched off so
  /// the run continues and reports every phase that misbehaves, not just the
  /// first.
  void on_imgui_error(ImGuiContext *, void *, const char *message)
  {
    fail(std::string("ImGui reported: ") + (message != nullptr ? message : "<null>"));
  }

  // ---------------------------------------------------------------------------
  // Stack balance
  // ---------------------------------------------------------------------------

  /// ImGui's own eleven stacks plus the one it never checks: the clip-rect
  /// stack of each window's ImDrawList. An extra ImDrawList::PushClipRect is
  /// silently swallowed -- ImGui neither asserts on it nor recovers from it --
  /// so it has to be counted here or it is invisible.
  ///
  /// Per window rather than summed: a window that is submitted for the first
  /// time this frame adds its own persistent entry, which a total would read
  /// as a leak. Only windows present in both samples are compared, and every
  /// window this test cares about lives for the whole of its phase, so a real
  /// leak is still caught on the very next frame.
  struct StackDepths
  {
    ImGuiErrorRecoveryState imgui;
    std::vector<std::pair<ImGuiWindow *, int>> clipRects;
  };

  StackDepths capture_stacks()
  {
    StackDepths depths;
    ImGui::ErrorRecoveryStoreState(&depths.imgui);

    ImGuiContext &g = *ImGui::GetCurrentContext();
    depths.clipRects.reserve(static_cast<std::size_t>(g.Windows.Size));
    for (ImGuiWindow *window : g.Windows)
    {
      if (window != nullptr && window->DrawList != nullptr)
      {
        depths.clipRects.emplace_back(window, window->DrawList->_ClipRectStack.Size);
      }
    }
    return depths;
  }

  void compare_clip_rects(const StackDepths &before, const StackDepths &after, const char *what)
  {
    for (const auto &earlier : before.clipRects)
    {
      for (const auto &later : after.clipRects)
      {
        if (later.first != earlier.first)
        {
          continue;
        }
        if (later.second != earlier.second)
        {
          char buffer[400];
          std::snprintf(
              buffer, sizeof(buffer),
              "%s left window '%s' with %d ImDrawList clip rect(s) pushed, expected %d "
              "(unbalanced ImDrawList::PushClipRect/PopClipRect -- ImGui does not check this one)",
              what, earlier.first->Name != nullptr ? earlier.first->Name : "<unnamed>",
              later.second, earlier.second);
          fail(buffer);
        }
        break;
      }
    }
  }

  /// The absolute clip-rect invariant, checked once the frame is closed.
  ///
  /// ImGui::Begin pushes a clip rect on the window's ImDrawList and End pops
  /// it, so after Render() nothing a panel pushed may still be there. ImGui
  /// itself never checks this, and neither a before/after comparison inside
  /// the frame nor a frame-to-frame one can see a violation: an unpopped
  /// ImDrawList::PushClipRect is perfectly *stable*, it just leaves every
  /// frame sitting one entry too deep. The only thing that catches it is an
  /// absolute depth -- taken here from a control sample, the implicit Debug
  /// window, which neither panel ever draws into, so an ImGui upgrade that
  /// changes the bookkeeping does not turn into a false alarm.
  void check_clip_rects_after_render()
  {
    ImGuiContext &g = *ImGui::GetCurrentContext();

    int expected = -1;
    for (ImGuiWindow *window : g.Windows)
    {
      if (window != nullptr && window->Name != nullptr &&
          std::strcmp(window->Name, "Debug##Default") == 0 && window->DrawList != nullptr)
      {
        expected = window->DrawList->_ClipRectStack.Size;
        break;
      }
    }
    if (expected < 0)
    {
      return;
    }

    for (ImGuiWindow *window : g.Windows)
    {
      if (window == nullptr || window->DrawList == nullptr || !window->WasActive)
      {
        continue;
      }
      const int depth = window->DrawList->_ClipRectStack.Size;
      if (depth != expected)
      {
        char buffer[400];
        std::snprintf(
            buffer, sizeof(buffer),
            "window '%s' ended the frame with %d ImDrawList clip rect(s) pushed, expected %d "
            "(an ImDrawList::PushClipRect was never popped -- ImGui does not check this one)",
            window->Name != nullptr ? window->Name : "<unnamed>", depth, expected);
        fail(buffer);
      }
    }
  }

  void compare_stacks(const StackDepths &before, const StackDepths &after, const char *what)
  {
    const struct
    {
      const char *name;
      short before;
      short after;
    } entries[] = {
        {"window stack (Begin/End)", before.imgui.SizeOfWindowStack, after.imgui.SizeOfWindowStack},
        {"ID stack (PushID/PopID)", before.imgui.SizeOfIDStack, after.imgui.SizeOfIDStack},
        {"tree stack (TreeNode/TreePop)", before.imgui.SizeOfTreeStack, after.imgui.SizeOfTreeStack},
        {"colour stack (PushStyleColor/PopStyleColor)", before.imgui.SizeOfColorStack, after.imgui.SizeOfColorStack},
        {"style-var stack (PushStyleVar/PopStyleVar)", before.imgui.SizeOfStyleVarStack, after.imgui.SizeOfStyleVarStack},
        {"font stack (PushFont/PopFont)", before.imgui.SizeOfFontStack, after.imgui.SizeOfFontStack},
        {"focus-scope stack", before.imgui.SizeOfFocusScopeStack, after.imgui.SizeOfFocusScopeStack},
        {"group stack (BeginGroup/EndGroup)", before.imgui.SizeOfGroupStack, after.imgui.SizeOfGroupStack},
        {"item-flags stack", before.imgui.SizeOfItemFlagsStack, after.imgui.SizeOfItemFlagsStack},
        {"popup stack (BeginPopup/EndPopup)", before.imgui.SizeOfBeginPopupStack, after.imgui.SizeOfBeginPopupStack},
        {"disabled stack (BeginDisabled/EndDisabled)", before.imgui.SizeOfDisabledStack, after.imgui.SizeOfDisabledStack},
    };

    for (const auto &entry : entries)
    {
      if (entry.before != entry.after)
      {
        char buffer[320];
        std::snprintf(
            buffer, sizeof(buffer), "%s left the %s at %d, expected %d",
            what, entry.name, static_cast<int>(entry.after), static_cast<int>(entry.before));
        fail(buffer);
      }
    }

    compare_clip_rects(before, after, what);
  }

  // ---------------------------------------------------------------------------
  // Fixture: a small skinned glTF on disk
  // ---------------------------------------------------------------------------

  void write_text_file(const std::filesystem::path &path, const std::string &content)
  {
    std::error_code errorCode;
    std::filesystem::create_directories(path.parent_path(), errorCode);
    std::ofstream out(path, std::ios::binary);
    out << content;
  }

  template <typename T>
  void append_bytes(std::vector<std::uint8_t> &buffer, const T &value)
  {
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(&value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(T));
  }

  /// A six-joint biped stub with a two-triangle skinned mesh and one imported
  /// animation. The panels key off a real skeleton for nearly everything they
  /// draw -- the joint tree, the dope-sheet rows, the rig editor's parent
  /// combo, the "imported clips" list -- so a synthetic ModelAsset built in
  /// memory would not exercise the paths that matter. This goes through
  /// assimp, exactly as the editor's own import does.
  ///
  /// The hierarchy is deliberately more than a chain: Hips forks into a spine
  /// and two legs, so the tree has a branch to indent and the dope sheet has
  /// sibling tracks to sort.
  void write_skinned_gltf(const std::filesystem::path &directory)
  {
    std::vector<std::uint8_t> bin;

    // 6 vertices / 2 triangles, spread over four of the six joints.
    const float positions[6][3] = {
        {0.0f, 0.0f, 0.0f}, {0.4f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        {0.4f, 1.0f, 0.0f}, {0.0f, 2.0f, 0.0f}, {0.4f, 2.0f, 0.0f}};
    const std::size_t positionsOffset = bin.size();
    for (const auto &position : positions)
    {
      for (const float component : position)
      {
        append_bytes(bin, component);
      }
    }

    const std::size_t normalsOffset = bin.size();
    for (int i = 0; i < 6; ++i)
    {
      append_bytes(bin, 0.0f);
      append_bytes(bin, 0.0f);
      append_bytes(bin, 1.0f);
    }

    // Joint indices address the skin's joint array: 0=Root 1=Hips 2=Spine
    // 3=Head 4=LegL 5=LegR. Nothing is weighted to the legs, so they come
    // back as unskinned hierarchy joints -- the dimmed rows in the tree.
    const std::size_t jointsOffset = bin.size();
    const std::uint16_t joints[6][4] = {
        {1, 0, 0, 0}, {1, 0, 0, 0}, {2, 1, 0, 0},
        {2, 1, 0, 0}, {3, 2, 0, 0}, {3, 2, 0, 0}};
    for (const auto &influence : joints)
    {
      for (const std::uint16_t index : influence)
      {
        append_bytes(bin, index);
      }
    }

    const std::size_t weightsOffset = bin.size();
    const float weights[6][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f},
        {0.6f, 0.4f, 0.0f, 0.0f}, {0.6f, 0.4f, 0.0f, 0.0f},
        {0.7f, 0.3f, 0.0f, 0.0f}, {0.7f, 0.3f, 0.0f, 0.0f}};
    for (const auto &weight : weights)
    {
      for (const float value : weight)
      {
        append_bytes(bin, value);
      }
    }

    const std::size_t indicesOffset = bin.size();
    for (const std::uint16_t index : {0, 1, 2, 2, 1, 3})
    {
      append_bytes(bin, index);
    }

    const std::size_t timesOffset = bin.size();
    append_bytes(bin, 0.0f);
    append_bytes(bin, 0.5f);
    append_bytes(bin, 1.0f);

    const std::size_t rotationsOffset = bin.size();
    const float halfSqrt2 = 0.70710678f;
    const float rotations[3][4] = {
        {0.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 0.38268343f, 0.92387953f},
        {0.0f, 0.0f, halfSqrt2, halfSqrt2}};
    for (const auto &rotation : rotations)
    {
      for (const float component : rotation)
      {
        append_bytes(bin, component);
      }
    }

    const std::size_t translationsOffset = bin.size();
    const float translations[3][3] = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.15f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    for (const auto &translation : translations)
    {
      for (const float component : translation)
      {
        append_bytes(bin, component);
      }
    }

    // Inverse bind matrices, column-major: identity for Root, then a
    // translate(-y) for each joint's rest height.
    const std::size_t ibmOffset = bin.size();
    const float restHeights[6] = {0.0f, 0.0f, 1.0f, 2.0f, 0.0f, 0.0f};
    for (const float height : restHeights)
    {
      const float matrix[16] = {
          1.0f, 0.0f, 0.0f, 0.0f,
          0.0f, 1.0f, 0.0f, 0.0f,
          0.0f, 0.0f, 1.0f, 0.0f,
          0.0f, -height, 0.0f, 1.0f};
      for (const float value : matrix)
      {
        append_bytes(bin, value);
      }
    }

    {
      std::error_code errorCode;
      std::filesystem::create_directories(directory, errorCode);
      std::ofstream out(directory / "biped.bin", std::ios::binary);
      out.write(
          reinterpret_cast<const char *>(bin.data()), static_cast<std::streamsize>(bin.size()));
    }

    nlohmann::json gltf;
    gltf["asset"] = {{"version", "2.0"}};
    gltf["scene"] = 0;
    gltf["scenes"] = {{{"nodes", {0, 1}}}};
    gltf["nodes"] = {
        {{"name", "MeshNode"}, {"mesh", 0}, {"skin", 0}},                 // 0
        {{"name", "Root"}, {"children", {2}}},                            // 1
        {{"name", "Hips"}, {"children", {3, 5, 6}}, {"translation", {0.0, 1.0, 0.0}}}, // 2
        {{"name", "Spine"}, {"children", {4}}, {"translation", {0.0, 0.6, 0.0}}},      // 3
        {{"name", "Head"}, {"translation", {0.0, 0.4, 0.0}}},             // 4
        {{"name", "LegL"}, {"translation", {-0.2, -0.4, 0.0}}},           // 5
        {{"name", "LegR"}, {"translation", {0.2, -0.4, 0.0}}}};           // 6
    gltf["meshes"] = {
        {{"primitives",
          {{{"attributes",
             {{"POSITION", 0}, {"NORMAL", 1}, {"JOINTS_0", 2}, {"WEIGHTS_0", 3}}},
            {"indices", 4}}}}}};
    gltf["skins"] = {{{"inverseBindMatrices", 8}, {"joints", {1, 2, 3, 4, 5, 6}}}};
    gltf["animations"] = {
        {{"name", "idle_imported"},
         {"channels",
          {{{"sampler", 0}, {"target", {{"node", 3}, {"path", "rotation"}}}},
           {{"sampler", 1}, {"target", {{"node", 2}, {"path", "translation"}}}}}},
         {"samplers",
          {{{"input", 5}, {"output", 6}, {"interpolation", "LINEAR"}},
           {{"input", 5}, {"output", 7}, {"interpolation", "LINEAR"}}}}}};

    const auto bufferView = [](std::size_t offset, std::size_t length)
    {
      return nlohmann::json{{"buffer", 0}, {"byteOffset", offset}, {"byteLength", length}};
    };
    gltf["bufferViews"] = {
        bufferView(positionsOffset, 72),
        bufferView(normalsOffset, 72),
        bufferView(jointsOffset, 48),
        bufferView(weightsOffset, 96),
        bufferView(indicesOffset, 12),
        bufferView(timesOffset, 12),
        bufferView(rotationsOffset, 48),
        bufferView(translationsOffset, 36),
        bufferView(ibmOffset, 384)};

    gltf["accessors"] = {
        {{"bufferView", 0}, {"componentType", 5126}, {"count", 6}, {"type", "VEC3"},
         {"min", {0.0, 0.0, 0.0}}, {"max", {0.4, 2.0, 0.0}}},
        {{"bufferView", 1}, {"componentType", 5126}, {"count", 6}, {"type", "VEC3"}},
        {{"bufferView", 2}, {"componentType", 5123}, {"count", 6}, {"type", "VEC4"}},
        {{"bufferView", 3}, {"componentType", 5126}, {"count", 6}, {"type", "VEC4"}},
        {{"bufferView", 4}, {"componentType", 5123}, {"count", 6}, {"type", "SCALAR"}},
        {{"bufferView", 5}, {"componentType", 5126}, {"count", 3}, {"type", "SCALAR"},
         {"min", {0.0}}, {"max", {1.0}}},
        {{"bufferView", 6}, {"componentType", 5126}, {"count", 3}, {"type", "VEC4"}},
        {{"bufferView", 7}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
        {{"bufferView", 8}, {"componentType", 5126}, {"count", 6}, {"type", "MAT4"}}};

    gltf["buffers"] = {{{"uri", "biped.bin"}, {"byteLength", bin.size()}}};

    write_text_file(directory / "biped.gltf", gltf.dump(2));
  }

  // ---------------------------------------------------------------------------
  // Fixture: authored clips and an animator graph
  // ---------------------------------------------------------------------------

  math::Quat quat_z(float degrees)
  {
    const float radians = degrees * 3.14159265f / 180.0f;
    math::Quat out;
    out.x = 0.0f;
    out.y = 0.0f;
    out.z = std::sin(radians * 0.5f);
    out.w = std::cos(radians * 0.5f);
    return out;
  }

  /// Keys on four joints across all six interpolation modes, plus events.
  /// The dope sheet draws one diamond per key and the curve editor one
  /// polyline per component, with a different segment shape per mode, so
  /// every easing branch of both has something to render.
  AnimationClipAsset build_coverage_clip()
  {
    AnimationClipAsset clip;
    clip.name = "smoke_walk";
    clip.sourceModel = "Models/biped.gltf";
    clip.duration = 2.0f;
    clip.frameRate = 30.0f;
    clip.looping = true;

    clip.set_translation_key("Hips", 0.0f, {0.0f, 1.0f, 0.0f}, Interpolation::Linear);
    clip.set_translation_key(
        "Hips", 0.5f, {0.0f, 1.25f, 0.0f}, Interpolation::Bezier, EaseCurve{0.42f, 0.0f, 0.58f, 1.0f});
    clip.set_translation_key("Hips", 1.0f, {0.0f, 1.0f, 0.0f}, Interpolation::Step);
    clip.set_translation_key("Hips", 1.5f, {0.1f, 1.15f, 0.0f}, Interpolation::EaseInOut);
    clip.set_translation_key("Hips", 2.0f, {0.0f, 1.0f, 0.0f}, Interpolation::Linear);

    clip.set_rotation_key("Spine", 0.0f, quat_z(0.0f), Interpolation::EaseIn);
    clip.set_rotation_key("Spine", 0.75f, quat_z(12.0f), Interpolation::EaseOut);
    clip.set_rotation_key(
        "Spine", 1.5f, quat_z(-8.0f), Interpolation::Bezier, EaseCurve{0.1f, 0.9f, 0.2f, 1.0f});
    clip.set_rotation_key("Spine", 2.0f, quat_z(0.0f), Interpolation::Linear);

    clip.set_scale_key("Head", 0.0f, {1.0f, 1.0f, 1.0f}, Interpolation::Step);
    clip.set_scale_key("Head", 1.0f, {1.1f, 0.9f, 1.0f}, Interpolation::EaseInOut);
    clip.set_scale_key("Head", 2.0f, {1.0f, 1.0f, 1.0f}, Interpolation::Linear);

    // A joint keyed on every channel at once, which is what auto-key writes.
    clip.set_pose_key(
        "LegL", 0.25f, {-0.2f, -0.4f, 0.0f}, quat_z(20.0f), {1.0f, 1.0f, 1.0f},
        Interpolation::Linear);
    clip.set_pose_key(
        "LegL", 1.25f, {-0.2f, -0.4f, 0.1f}, quat_z(-20.0f), {1.0f, 1.0f, 1.0f},
        Interpolation::EaseOut);

    // A track bound to a joint the skeleton does not have, so the dope sheet
    // draws its "retargeting mismatch" row (joint == -1).
    clip.set_rotation_key("TailTip", 0.5f, quat_z(45.0f), Interpolation::Linear);

    clip.events.push_back({0.25f, "footstep", "left", 0.8f});
    clip.events.push_back({1.25f, "footstep", "right", 0.8f});
    clip.events.push_back({1.9f, "loop_end", "", 0.0f});

    clip.sort_keys();
    return clip;
  }

  AnimationClipAsset build_additive_clip()
  {
    AnimationClipAsset clip;
    clip.name = "smoke_lean";
    clip.sourceModel = "Models/biped.gltf";
    clip.duration = 1.0f;
    clip.frameRate = 24.0f;
    clip.looping = false;
    clip.additive = true;
    clip.additiveReferenceTime = 0.0f;
    clip.set_rotation_key("Spine", 0.0f, quat_z(0.0f), Interpolation::Linear);
    clip.set_rotation_key("Spine", 1.0f, quat_z(25.0f), Interpolation::EaseIn);
    clip.sort_keys();
    return clip;
  }

  AnimatorGraph build_coverage_graph()
  {
    AnimatorGraph graph;
    graph.name = "Locomotion";
    graph.description = "Every state kind, condition op and parameter type.";
    graph.sourceModel = "Models/biped.gltf";

    const auto parameter =
        [&graph](const char *name, AnimParamType type, float f, int i, bool b)
    {
      AnimParameter added;
      added.name = name;
      added.type = type;
      added.floatValue = f;
      added.intValue = i;
      added.boolValue = b;
      graph.parameters.push_back(std::move(added));
    };
    parameter("Speed", AnimParamType::Float, 0.4f, 0, false);
    parameter("Ammo", AnimParamType::Int, 0.0f, 7, false);
    parameter("Grounded", AnimParamType::Bool, 0.0f, 0, true);
    parameter("Jump", AnimParamType::Trigger, 0.0f, 0, false);

    AnimLayer base;
    base.name = "Base";
    base.weight = 1.0f;
    base.defaultState = 0;

    AnimState idle;
    idle.name = "Idle";
    idle.kind = AnimStateKind::Clip;
    idle.clip = "smoke_walk";
    idle.x = 20.0f;
    idle.y = 20.0f;
    base.states.push_back(idle);

    AnimState locomotion;
    locomotion.name = "Locomotion";
    locomotion.kind = AnimStateKind::BlendTree1D;
    locomotion.blendParameterX = "Speed";
    locomotion.entries = {
        {"smoke_walk", 0.0f, 0.0f, 1.0f},
        {"smoke_walk", 0.5f, 0.0f, 1.2f},
        {"smoke_lean", 1.0f, 0.0f, 1.5f}};
    locomotion.x = 240.0f;
    locomotion.y = 20.0f;
    base.states.push_back(locomotion);

    AnimState strafe;
    strafe.name = "Strafe";
    strafe.kind = AnimStateKind::BlendTree2D;
    strafe.blendParameterX = "Speed";
    strafe.blendParameterY = "Speed";
    strafe.entries = {
        {"smoke_walk", -1.0f, 0.0f, 1.0f},
        {"smoke_walk", 1.0f, 0.0f, 1.0f},
        {"smoke_lean", 0.0f, 1.0f, 1.0f}};
    strafe.x = 20.0f;
    strafe.y = 160.0f;
    base.states.push_back(strafe);

    AnimState jump;
    jump.name = "Jump";
    jump.kind = AnimStateKind::Clip;
    jump.clip = "smoke_lean";
    jump.looping = false;
    jump.speed = 1.4f;
    jump.x = 240.0f;
    jump.y = 160.0f;
    base.states.push_back(jump);

    // Idle -> Locomotion, plain guard.
    AnimTransition run;
    run.fromState = 0;
    run.toState = 1;
    run.duration = 0.18f;
    run.conditions = {{"Speed", AnimConditionOp::Greater, 0.1f}};
    base.transitions.push_back(run);

    // Locomotion -> Idle, with exit time and two conditions.
    AnimTransition stop;
    stop.fromState = 1;
    stop.toState = 0;
    stop.duration = 0.25f;
    stop.hasExitTime = true;
    stop.exitTime = 0.8f;
    stop.conditions = {
        {"Speed", AnimConditionOp::LessOrEqual, 0.1f},
        {"Grounded", AnimConditionOp::IsTrue, 0.0f}};
    base.transitions.push_back(stop);

    // Any-state transition on a trigger, and a return edge. The blend is long
    // enough to survive several frames: the canvas colours an any-state stub
    // through its own branch, and a 0.05 s crossfade would be over before the
    // play-mode phases could look at it.
    AnimTransition leap;
    leap.fromState = AnimTransition::kAnyState;
    leap.toState = 3;
    leap.duration = 0.25f;
    leap.priority = 5;
    leap.canInterrupt = true;
    leap.conditions = {{"Jump", AnimConditionOp::IsTrue, 0.0f}};
    base.transitions.push_back(leap);

    AnimTransition land;
    land.fromState = 3;
    land.toState = 0;
    land.duration = 0.2f;
    land.hasExitTime = true;
    land.exitTime = 0.9f;
    land.conditions = {{"Ammo", AnimConditionOp::NotEquals, 0.0f}};
    base.transitions.push_back(land);

    // A second edge between the same pair, so the canvas has to fan them out.
    AnimTransition strafeIn;
    strafeIn.fromState = 0;
    strafeIn.toState = 2;
    strafeIn.duration = 0.3f;
    strafeIn.conditions = {{"Speed", AnimConditionOp::GreaterOrEqual, 0.6f}};
    base.transitions.push_back(strafeIn);

    // A self transition. The canvas draws these as a loop arcing over the
    // node -- its own bezier, arrow head and hit test, shared with nothing
    // else -- and no other edge in the fixture reaches that branch. Its
    // condition can never hold (Ammo is 7), so it draws every frame without
    // ever perturbing the live animator the play-mode phases drive.
    AnimTransition rebalance;
    rebalance.fromState = 1;
    rebalance.toState = 1;
    rebalance.duration = 0.12f;
    rebalance.hasExitTime = true;
    rebalance.exitTime = 0.95f;
    rebalance.conditions = {{"Ammo", AnimConditionOp::Equals, 99.0f}};
    base.transitions.push_back(rebalance);

    graph.layers.push_back(std::move(base));

    // A masked additive layer, so the layer rail and the mask editor have
    // something other than defaults to draw.
    AnimLayer upper;
    upper.name = "UpperBody";
    upper.weight = 0.6f;
    upper.additive = true;
    upper.maskBones = {"Spine", "Head"};
    upper.maskIncludesDescendants = true;
    AnimState lean;
    lean.name = "Lean";
    lean.kind = AnimStateKind::Clip;
    lean.clip = "smoke_lean";
    lean.x = 40.0f;
    lean.y = 40.0f;
    upper.states.push_back(lean);
    graph.layers.push_back(std::move(upper));

    return graph;
  }

  /// A second graph that fails validation, so the problems banner renders.
  AnimatorGraph build_broken_graph()
  {
    AnimatorGraph graph;
    graph.name = "Broken";
    AnimLayer layer;
    layer.name = "Base";
    AnimState orphan;
    orphan.name = "Orphan";
    orphan.kind = AnimStateKind::BlendTree1D;
    orphan.blendParameterX = "NoSuchParameter";
    layer.states.push_back(orphan);
    AnimTransition dangling;
    dangling.fromState = 0;
    dangling.toState = 9;
    dangling.conditions = {{"AlsoMissing", AnimConditionOp::Less, 1.0f}};
    layer.transitions.push_back(dangling);
    graph.layers.push_back(std::move(layer));
    return graph;
  }

  // ---------------------------------------------------------------------------
  // ImGui plumbing the harness needs
  // ---------------------------------------------------------------------------

  ImGuiWindow *find_window_with_suffix(const char *suffix)
  {
    ImGuiContext &g = *ImGui::GetCurrentContext();
    const std::size_t suffixLength = std::strlen(suffix);
    for (ImGuiWindow *window : g.Windows)
    {
      if (window == nullptr || window->Name == nullptr)
      {
        continue;
      }
      const std::size_t nameLength = std::strlen(window->Name);
      if (nameLength >= suffixLength &&
          std::strcmp(window->Name + nameLength - suffixLength, suffix) == 0)
      {
        return window;
      }
    }
    return nullptr;
  }

  /// Switch the Animation panel's tab bar without a mouse. The tabs are plain
  /// ImGui::BeginTabItem calls, so nothing outside ImGui knows their
  /// rectangles; this is the same door ImGui's own test engine uses.
  bool queue_tab(int tabIndex)
  {
    ImGuiWindow *window = find_window_with_suffix("Animation");
    if (window == nullptr)
    {
      return false;
    }
    ImGuiTabBar *tabBar = ImGui::GetCurrentContext()->TabBars.GetByKey(window->GetID("##animation-tabs"));
    if (tabBar == nullptr || tabIndex < 0 || tabIndex >= tabBar->Tabs.Size)
    {
      return false;
    }
    ImGui::TabBarQueueFocus(tabBar, &tabBar->Tabs[tabIndex]);
    return true;
  }

  ImGuiWindow *find_window_containing(const char *needle)
  {
    ImGuiContext &g = *ImGui::GetCurrentContext();
    for (ImGuiWindow *window : g.Windows)
    {
      if (window != nullptr && window->Name != nullptr &&
          std::strstr(window->Name, needle) != nullptr)
      {
        return window;
      }
    }
    return nullptr;
  }

  /// Horizontal centre of the curve editor's plot, which is where a key at
  /// the exact middle of the visible time range lands.
  ///
  /// The curve editor is drawn inside the Animate tab's right-hand child at a
  /// cursor position nothing publishes, so the only way to aim a click at a
  /// key handle is to reconstruct the mapping: the plot starts one gutter in
  /// from the child's content edge (the gutter is `labelWidth`, clamped) and
  /// spans the rest, and `timeline_time_to_x` is linear in the view range.
  /// Park a key at the middle of that range and its handles sit on this line.
  bool curve_plot_centre_x(float gutter, float &outX)
  {
    ImGuiWindow *window = find_window_containing("##animate-right");
    if (window == nullptr)
    {
      return false;
    }
    const float left = window->WorkRect.Min.x;
    const float width = window->WorkRect.Max.x - left;
    if (width <= gutter + 64.0f)
    {
      return false;
    }
    outX = left + gutter + (width - gutter) * 0.5f;
    return true;
  }

  bool animation_panel_work_rect(float &outTop, float &outBottom)
  {
    ImGuiWindow *window = find_window_containing("##animate-right");
    if (window == nullptr)
    {
      return false;
    }
    outTop = window->WorkRect.Min.y;
    outBottom = window->WorkRect.Max.y;
    return true;
  }

  /// Ask Dear ImGui to activate a text field the harness cannot click.
  ///
  /// Both panels stand their undo shortcut down while `io.WantTextInput` is
  /// raised, and only a live InputText raises it. A null-backend run never
  /// learns any widget's rectangle, so there is no click to aim; this uses
  /// ImGui's own programmatic door instead, which queues the request and
  /// honours it on the next frame when the item is submitted again. The real
  /// activation path runs -- the flag is not forged.
  ///
  /// The id is seeded from the child window because both fields addressed
  /// this way are submitted with nothing extra on the id stack, and Begin
  /// resets IDStack to the window id, so reading it after the frame gives
  /// exactly the seed the widget used.
  bool activate_text_field(const char *windowNeedle, const char *label)
  {
    ImGuiWindow *window = find_window_containing(windowNeedle);
    if (window == nullptr)
    {
      return false;
    }
    ImGui::ActivateItemByID(window->GetID(label));
    return true;
  }

  /// One scheduled Ctrl(+Shift)+Z tap, in frames local to its phase.
  struct UndoTap
  {
    int frame = 0;
    bool shift = false;
  };

  /// Drive the undo/redo chord for one phase-local frame.
  ///
  /// `ImGui::IsKeyPressed` only fires on an up->down transition, so a tap is
  /// two frames -- down, then up -- and the modifier has to be held either
  /// side of it so the panel sees `io.KeyCtrl` on the frame the press lands.
  /// Both the named modifier and the physical key are sent: ImGui derives
  /// `io.KeyCtrl` from the former and `IsKeyPressed(ImGuiKey_LeftCtrl)` from
  /// the latter, and a real keyboard produces both.
  void drive_undo_chord(ImGuiIO &io, int local, const std::vector<UndoTap> &taps)
  {
    bool control = false;
    bool shift = false;
    bool z = false;
    for (const UndoTap &tap : taps)
    {
      if (local >= tap.frame - 1 && local <= tap.frame + 1)
      {
        control = true;
        shift = shift || tap.shift;
      }
      if (local == tap.frame)
      {
        z = true;
      }
    }
    io.AddKeyEvent(ImGuiMod_Ctrl, control);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, control);
    io.AddKeyEvent(ImGuiMod_Shift, shift);
    io.AddKeyEvent(ImGuiKey_LeftShift, shift);
    io.AddKeyEvent(ImGuiKey_Z, z);
  }

  /// Is `stateName` the destination of an edge in the fixture graph's base
  /// layer? `anyStateOnly` narrows that to the "Any State" stub, which the
  /// canvas colours through a branch of its own.
  ///
  /// The canvas decides which arrow to paint as "running" by comparing the
  /// live animator's state *name* against each transition's destination, so
  /// this is the same question it asks -- and the only way to tell "an
  /// overlay was available" apart from "an arrow was actually lit".
  bool graph_has_edge_into(const AnimatorGraph &graph, const std::string &stateName, bool anyStateOnly)
  {
    if (graph.layers.empty() || stateName.empty())
    {
      return false;
    }
    const AnimLayer &layer = graph.layers.front();
    for (const AnimTransition &transition : layer.transitions)
    {
      if (anyStateOnly && transition.fromState != AnimTransition::kAnyState)
      {
        continue;
      }
      if (transition.toState < 0 ||
          static_cast<std::size_t>(transition.toState) >= layer.states.size())
      {
        continue;
      }
      if (layer.states[static_cast<std::size_t>(transition.toState)].name == stateName)
      {
        return true;
      }
    }
    return false;
  }

  bool graph_has_state(const AnimatorGraph &graph, const std::string &stateName)
  {
    if (graph.layers.empty() || stateName.empty())
    {
      return false;
    }
    for (const AnimState &state : graph.layers.front().states)
    {
      if (state.name == stateName)
      {
        return true;
      }
    }
    return false;
  }

  bool nearly_equal(float a, float b)
  {
    return std::fabs(a - b) < 0.01f;
  }
}

int main()
{
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(2400.0f, 1400.0f);
  io.DeltaTime = 1.0f / 60.0f;
  io.IniFilename = nullptr;
  // Report recoverable errors instead of aborting on the first one: a run
  // that names every misbehaving phase is worth far more than a core dump on
  // frame 41.
  io.ConfigErrorRecovery = true;
  io.ConfigErrorRecoveryEnableAssert = false;
  io.ConfigErrorRecoveryEnableDebugLog = false;
  io.ConfigErrorRecoveryEnableTooltip = false;
  ImGui::GetCurrentContext()->ErrorCallback = &on_imgui_error;

  ImGui_ImplNullPlatform_Init();
  ImGui_ImplNullRender_Init();

  // ---- Workspace ----------------------------------------------------------

  std::error_code errorCode;
  const auto workspace = std::filesystem::temp_directory_path() / "hades-animation-smoke";
  std::filesystem::remove_all(workspace, errorCode);
  std::filesystem::create_directories(workspace / ".hades" / "worlds", errorCode);
  std::filesystem::create_directories(workspace / ".hades" / "animations", errorCode);
  std::filesystem::create_directories(workspace / ".hades" / "animators", errorCode);

  const std::string modelPath = "Models/biped.gltf";
  const std::string brokenModelPath = "Models/broken.gltf";
  write_skinned_gltf(workspace / "Models");
  // Valid extension, garbage contents: the "model failed to load" branch.
  write_text_file(workspace / brokenModelPath, "{ this is not a glTF document");

  ModelAssetCache::instance().setAssetRoot(workspace);
  AnimationClipCache::instance().setAssetRoot(workspace);

  {
    std::string error;
    if (!AnimationClipCache::instance().saveClip("smoke_walk", build_coverage_clip(), &error))
    {
      std::fprintf(stderr, "animation smoke: could not write clip fixture: %s\n", error.c_str());
      return 1;
    }
    if (!AnimationClipCache::instance().saveClip("smoke_lean", build_additive_clip(), &error))
    {
      std::fprintf(stderr, "animation smoke: could not write clip fixture: %s\n", error.c_str());
      return 1;
    }
    if (!AnimationClipCache::instance().saveGraph("Locomotion", build_coverage_graph(), &error))
    {
      std::fprintf(stderr, "animation smoke: could not write graph fixture: %s\n", error.c_str());
      return 1;
    }
    if (!AnimationClipCache::instance().saveGraph("Broken", build_broken_graph(), &error))
    {
      std::fprintf(stderr, "animation smoke: could not write graph fixture: %s\n", error.c_str());
      return 1;
    }
  }

  // The model has to import before the panels are worth driving: without a
  // skeleton the Animate tab is a single warning line.
  {
    const ModelAsset *asset = ModelAssetCache::instance().get(modelPath);
    if (asset == nullptr)
    {
      std::fprintf(
          stderr, "animation smoke: fixture model failed to import: %s\n",
          ModelAssetCache::instance().errorFor(modelPath).c_str());
      return 1;
    }
    if (asset->nodes.size() < 6)
    {
      std::fprintf(
          stderr, "animation smoke: fixture model imported only %zu nodes, expected a 7-node rig\n",
          asset->nodes.size());
      return 1;
    }
  }

  // ---- Scene --------------------------------------------------------------

  hades::Editor editor;
  hades::EntityManager entityManager;
  hades::ComponentManager componentManager(&entityManager);
  hades::ScriptRuntime scriptRuntime;
  hades::BlueprintRuntime blueprintRuntime;

  const auto world =
      hades::EntityFactory::createWorld(entityManager, componentManager, "Smoke", true);
  editor.state.loadedWorld = world;
  editor.state.activeWorld = world;

  const auto character = hades::EntityFactory::createModel(entityManager, componentManager, world);
  componentManager.getComponent<hades::ModelComponent>(character).assetPath = modelPath;
  if (componentManager.hasComponent<hades::NameComponent>(character))
  {
    componentManager.getComponent<hades::NameComponent>(character).value = "Biped";
  }
  {
    hades::AnimatorComponent animator;
    animator.graphPath = "Locomotion";
    animator.defaultClip = "smoke_walk";
    componentManager.addComponent(character, animator);
  }

  const auto brokenEntity = hades::EntityFactory::createModel(entityManager, componentManager, world);
  componentManager.getComponent<hades::ModelComponent>(brokenEntity).assetPath = brokenModelPath;
  if (componentManager.hasComponent<hades::NameComponent>(brokenEntity))
  {
    componentManager.getComponent<hades::NameComponent>(brokenEntity).value = "BrokenModel";
  }

  // A live animator instance running the fixture graph, so the Animator
  // panel's play-mode debug overlay (active state highlight, normalised time)
  // has something real to read.
  {
    hades::AnimatorInstance &instance = hades::AnimationRuntime::instance().instanceFor(character);
    instance.set_graph_reference("Locomotion");
    instance.set_playing(true);
    instance.set_float("Speed", 0.7f);
    instance.set_bool("Grounded", true);
  }

  // ---- Plugins ------------------------------------------------------------

  // The editor's own registry is what the menu drives, so confirm both panels
  // really registered under the ids the rest of the editor addresses them by;
  // a typo there is invisible until a user picks the menu item.
  editor.show_plugin("animation-editor");
  editor.show_plugin("animator-graph");
  if (!editor.is_plugin_visible("animation-editor"))
  {
    std::fprintf(stderr, "animation smoke: no editor plugin registered as 'animation-editor'\n");
    return 1;
  }
  if (!editor.is_plugin_visible("animator-graph"))
  {
    std::fprintf(stderr, "animation smoke: no editor plugin registered as 'animator-graph'\n");
    return 1;
  }

  // Rendered from local instances, the way editor_smoke.cpp drives the
  // Blueprint panel: the registry's copies are owned by Editor::render, which
  // needs a real window and renderer.
  hades::AnimationEditorPlugin animationPanel;
  hades::AnimatorGraphPlugin animatorPanel;
  animationPanel.set_visible(editor, true);
  animatorPanel.set_visible(editor, true);

  // ---- Phase plan ---------------------------------------------------------

  enum class Phase
  {
    EmptyWorkspace,
    NoTarget,
    BrokenModel,
    AnimateIdle,
    AnimateJointPicked,
    AnimateCurves,
    CurveHandleDrag,
    AnimatePlaying,
    AnimatePosing,
    AnimateCollapsed,
    AnimateUndoRedo,
    AnimateUndoGated,
    RigTab,
    RigViewport,
    ClipsTab,
    AnimatorLoaded,
    AnimatorStateSelected,
    AnimatorTransitionSelected,
    AnimatorDragUndo,
    AnimatorUndoGated,
    AnimatorBrokenGraph,
    AnimatorPreview,
    PlayModeEnter,
    PlayModeSettled,
    PlayModeAnyState,
    Fuzz,
  };

  struct PhaseStep
  {
    Phase phase;
    const char *name;
    int frames;
  };

  const PhaseStep plan[] = {
      {Phase::EmptyWorkspace, "empty workspace", 8},
      {Phase::NoTarget, "no target selected", 16},
      {Phase::BrokenModel, "target model fails to load", 16},
      {Phase::AnimateIdle, "Animate tab, no joint selected", 24},
      {Phase::AnimateJointPicked, "Animate tab, joint selected from the viewport", 24},
      {Phase::AnimateCurves, "Animate tab, curve editor with keys selected", 32},
      // Four frames per probe: park, press, drag, release. The raster is one
      // column wide -- see curve_plot_centre_x -- so only the vertical
      // position of the key handles has to be searched.
      {Phase::CurveHandleDrag, "Animate tab, dragging a curve key handle", 4 * 3 * 160},
      {Phase::AnimatePlaying, "Animate tab, playback running", 32},
      {Phase::AnimatePosing, "Animate tab, viewport gizmo posing with auto-key", 24},
      {Phase::AnimateCollapsed, "Animate tab, collapsed bone rows and no frame snapping", 20},
      {Phase::AnimateUndoRedo, "Animate tab, Ctrl+Z then Ctrl+Shift+Z over a real edit", 30},
      {Phase::AnimateUndoGated, "Animate tab, Ctrl+Z ignored while a text field is active", 26},
      {Phase::RigTab, "Rig tab", 32},
      {Phase::RigViewport, "Rig tab, joint picked and dragged through the viewport", 30},
      {Phase::ClipsTab, "Clips tab", 32},
      {Phase::AnimatorLoaded, "Animator, graph loaded", 24},
      {Phase::AnimatorStateSelected, "Animator, state selected on the canvas", 32},
      {Phase::AnimatorTransitionSelected, "Animator, transition selected on the canvas", 78},
      {Phase::AnimatorDragUndo, "Animator, node dragged then Ctrl+Z / Ctrl+Shift+Z", 40},
      {Phase::AnimatorUndoGated, "Animator, Ctrl+Z ignored while a text field is active", 26},
      {Phase::AnimatorBrokenGraph, "Animator, graph that fails validation", 20},
      {Phase::AnimatorPreview, "Animator, edit-mode preview driving an entity", 40},
      {Phase::PlayModeEnter, "play mode, animator crossfading into its first state", 20},
      {Phase::PlayModeSettled, "play mode, animator settled on one state", 14},
      {Phase::PlayModeAnyState, "play mode, any-state transition and the live state panel", 60},
      {Phase::Fuzz, "mouse fuzz over both panels", 320},
  };

  // Beats within the scripted phases, so the frame a chord lands on and the
  // frame its effect is checked on are stated once rather than sprinkled
  // through the loop as bare numbers.
  constexpr int kAnimateEditFrame = 2;    // gizmo gesture: 2, 3, finished on 4
  constexpr int kAnimateEditDone = 4;
  constexpr int kAnimateUndoTap = 10;
  constexpr int kAnimateUndoCheck = 14;
  constexpr int kAnimateRedoTap = 18;
  constexpr int kAnimateRedoCheck = 22;

  // Rig viewport: pick on 2, drag 4..7 (finished on 7), check on 9, Ctrl+Z on
  // 14, checked on 18.
  constexpr int kRigPickFrame = 2;
  constexpr int kRigDragStart = 4;
  constexpr int kRigDragDone = 7;
  constexpr int kRigDragCheck = 9;
  constexpr int kRigUndoTap = 14;
  constexpr int kRigUndoCheck = 18;
  /// Far from any rest translation in the fixture rig, so "the joint moved"
  /// cannot be true by coincidence.
  constexpr float kRigDragTargetX = 3.75f;

  constexpr int kAnimatorDragStart = 4;   // press on 5, drag to 10, release on 11
  constexpr int kAnimatorDragEnd = 10;
  constexpr int kAnimatorDragCheck = 13;
  constexpr int kAnimatorUndoTap = 16;
  constexpr int kAnimatorUndoCheck = 20;
  constexpr int kAnimatorRedoTap = 26;
  constexpr int kAnimatorRedoCheck = 30;

  // The gated phases end with a control tap: the same chord, with the text
  // field handed back first. Without it "nothing happened" would also be the
  // answer for a panel that simply never had focus, and the whole phase would
  // pass vacuously.
  constexpr int kGatedReleaseOffset = 8;  // frames before the end: Escape
  constexpr int kGatedControlOffset = 5;  // frames before the end: Ctrl+Z
  constexpr int kGatedCheckOffset = 2;

  constexpr int kAnyStateSelectFrom = 2;  // press on 5, release on 7
  constexpr int kAnyStateOtherGraph = 38; // swap the panel onto a graph the
  constexpr int kAnyStateBackAgain = 48;  // running entity is not playing

  const ImVec2 animationPanelPos(10.0f, 10.0f);
  const ImVec2 animatorPanelPos(1170.0f, 10.0f);

  // Canaries: a frame that draws cleanly is not evidence that anything was
  // reachable, so each of these has to be observed at least once or the run
  // fails. They are what turns "it rendered" into "it rendered the thing".
  bool sawSkeleton = false;
  bool sawClipLoaded = false;
  bool sawTimelineRows = false;
  bool sawJointSelected = false;
  bool sawCurveSelection = false;
  bool sawCurveHandleDrag = false;
  bool sawAutoKey = false;
  bool sawRigTab = false;
  bool sawRigOverlay = false;
  bool sawRigPick = false;
  bool sawRigDrag = false;
  bool sawRigUndo = false;
  bool sawAnimatorPreview = false;
  std::size_t rigUndoDepthBefore = 0;
  float rigTranslationBeforeDrag = 0.0f;
  bool sawClipsTab = false;
  bool sawGraphLoaded = false;
  bool sawStateNodes = false;
  bool sawAnimatorDebug = false;
  int canvasSelectedState = -1;
  int canvasSelectedTransition = -1;
  std::size_t keysBeforePosing = 0;

  // Animation panel undo/redo: the clip has to move back and then forward
  // again, not merely the undo stack.
  bool sawAnimationEdit = false;
  bool sawAnimationUndo = false;
  bool sawAnimationRedo = false;
  bool sawAnimationUndoGated = false;
  std::size_t animateKeysBeforeEdit = 0;
  std::size_t animateKeysAfterEdit = 0;
  std::size_t animateUndoDepthBefore = 0;
  std::size_t gatedUndoDepth = 0;
  std::size_t controlUndoDepth = 0;
  bool gatedChordFired = false;
  int gatedTapFrame = -1;
  bool sawAnimationUndoUngated = false;
  bool sawAnimatorUndoUngated = false;

  // Animator panel: a node dragged through the canvas, then walked back and
  // forward with the same two chords.
  bool sawAnimatorDrag = false;
  bool sawAnimatorUndo = false;
  bool sawAnimatorRedo = false;
  bool sawAnimatorUndoGated = false;
  float dragBaselineX = 0.0f;
  float dragBaselineY = 0.0f;
  float draggedX = 0.0f;
  float draggedY = 0.0f;

  // Play-mode debug overlay: available, matched to a node, mid-crossfade,
  // lighting an ordinary arrow and an any-state stub, feeding the parameter
  // rail live values, and correctly standing down for a graph the running
  // entity is not playing.
  bool sawDebugActiveState = false;
  bool sawDebugProgressBar = false;
  bool sawDebugTransitionRunning = false;
  bool sawDebugArrowLit = false;
  bool sawDebugAnyStateLit = false;
  bool sawDebugSettled = false;
  bool sawLiveParameterRail = false;
  bool sawStateDetailsDuringPlay = false;
  bool sawDebugStoodDownForOtherGraph = false;
  const AnimatorGraph fixtureGraph = build_coverage_graph();

  int frame = 0;
  for (const PhaseStep &step : plan)
  {
    g_phase = step.name;

    for (int local = 0; local < step.frames; ++local, ++frame)
    {
      g_frame = frame;

      // ---- Input -------------------------------------------------------
      const float t = static_cast<float>(frame) * 0.13f;
      if (step.phase == Phase::Fuzz)
      {
        // A Lissajous sweep across both windows with presses, drags, wheel
        // and right-clicks: crash and stack-imbalance shaking, nothing more.
        const float x = 1200.0f + 1150.0f * std::sin(t * 1.7f);
        const float y = 380.0f + 360.0f * std::sin(t * 1.1f + 0.7f);
        io.AddMousePosEvent(x, y);
        const int beat = frame % 8;
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, beat == 2 || beat == 3);
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, beat == 6);
        io.AddMouseButtonEvent(ImGuiMouseButton_Middle, beat == 7);
        io.AddMouseWheelEvent(
            0.0f, (frame % 40 == 0) ? 1.0f : ((frame % 40 == 20) ? -1.0f : 0.0f));
        io.AddKeyEvent(ImGuiMod_Ctrl, (frame % 24) < 4);
      }
      else if (step.phase == Phase::AnimatorStateSelected ||
               step.phase == Phase::AnimatorTransitionSelected ||
               step.phase == Phase::AnimatorDragUndo ||
               step.phase == Phase::PlayModeAnyState ||
               step.phase == Phase::CurveHandleDrag)
      {
        // Driven below, once the previous frame's geometry is known.
      }
      else
      {
        // Hover-only: every IsItemHovered branch, tooltip and hover highlight
        // runs, but nothing is mutated behind the phase's back.
        const float x = 600.0f + 560.0f * std::sin(t * 0.9f);
        const float y = 360.0f + 330.0f * std::sin(t * 0.6f + 1.3f);
        io.AddMousePosEvent(x, y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
        io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
        io.AddMouseWheelEvent(0.0f, 0.0f);
      }

      switch (step.phase)
      {
      case Phase::AnimateUndoRedo:
        // One undo, then one redo, against the gizmo edit made below. Both
        // are gated on the panel's own window tree holding focus, which the
        // set_visible call further down arranges.
        drive_undo_chord(
            io, local, {{kAnimateUndoTap, false}, {kAnimateRedoTap, true}});
        break;

      case Phase::AnimatorDragUndo:
        drive_undo_chord(
            io, local, {{kAnimatorUndoTap, false}, {kAnimatorRedoTap, true}});
        break;

      case Phase::RigViewport:
        // The Rig tab keeps its own history: undoing here must roll back the
        // rig, and must not touch the clip.
        drive_undo_chord(io, local, {{kRigUndoTap, false}});
        break;

      case Phase::AnimateUndoGated:
      case Phase::AnimatorUndoGated:
      {
        // The chord only goes out once a text field is genuinely up:
        // ActivateItemByID is honoured on the frame after it is queued and
        // io.WantTextInput lands a frame after that, so the schedule cannot
        // be written in advance. Sending it early would prove nothing --
        // the shortcut would be ignored for want of focus, not for want of
        // the gate.
        if (local == 0)
        {
          gatedTapFrame = -1;
          gatedChordFired = false;
        }
        if (io.WantTextInput && gatedTapFrame < 0 && local >= 3)
        {
          gatedTapFrame = local + 1;
        }
        const UndoTap control{step.frames - kGatedControlOffset, false};
        if (gatedTapFrame >= 0)
        {
          drive_undo_chord(io, local, {{gatedTapFrame, false}, control});
        }
        else
        {
          drive_undo_chord(io, local, {control});
        }
        // Hand the field back before the control tap, both so the next phase
        // does not inherit a live InputText and so the control tap is the
        // same chord under the same focus with only the gate removed.
        io.AddKeyEvent(ImGuiKey_Escape, local == step.frames - kGatedReleaseOffset);
        break;
      }

      case Phase::Fuzz:
        break;

      default:
        drive_undo_chord(io, local, {});
        break;
      }

      // ---- Frame -------------------------------------------------------
      ImGui_ImplNullPlatform_NewFrame();
      ImGui_ImplNullRender_NewFrame();
      ImGui::NewFrame();

      const StackDepths baseline = capture_stacks();

      // ---- Phase state -------------------------------------------------
      const bool workspaceLive = step.phase != Phase::EmptyWorkspace;
      const bool playPhase = step.phase == Phase::PlayModeEnter ||
                             step.phase == Phase::PlayModeSettled ||
                             step.phase == Phase::PlayModeAnyState;
      editor.state.isPlaying = playPhase;

      // Built before the phase switch rather than just before the render, so
      // a phase can open a clip or a graph on the very frame it starts. The
      // Animator's debug overlay only lights up when the panel already holds
      // the graph the entity is running, and loading it a frame late would
      // leave the first frame of every play phase drawing the wrong branch.
      hades::EditorPluginContext context{
          editor,
          io.DeltaTime,
          workspaceLive ? workspace : std::filesystem::path{},
          entityManager,
          componentManager,
          scriptRuntime,
          blueprintRuntime,
      };

      switch (step.phase)
      {
      case Phase::EmptyWorkspace:
      case Phase::NoTarget:
        editor.state.selectedEntity.reset();
        break;
      case Phase::BrokenModel:
        editor.state.selectedEntity = brokenEntity;
        break;
      default:
        editor.state.selectedEntity = character;
        break;
      }

      // The clip has to be opened through the panel; the Clips tab's list is
      // the only door in the real UI and it needs a mouse.
      if (workspaceLive && !animationPanel.clip_is_loaded() &&
          step.phase != Phase::NoTarget && step.phase != Phase::BrokenModel)
      {
        animationPanel.open_clip(context, "smoke_walk");
      }

      AnimationTimelineState &timeline = animationPanel.timeline_state();
      switch (step.phase)
      {
      case Phase::AnimateIdle:
        timeline.showCurves = false;
        timeline.time = 0.4f;
        animationPanel.set_playing(false);
        break;

      case Phase::AnimateJointPicked:
        // The viewport publishes joint picks through the shared edit state;
        // this is the same one-shot the 3D view writes when a bone is clicked.
        animation_edit_state().pickedJoint = 2 + (local % 4);
        timeline.showCurves = false;
        break;

      case Phase::AnimateCurves:
      {
        timeline.showCurves = true;
        // The curve editor plots the channels the key selection touches, so
        // without a selection it draws its "select a channel row" placeholder
        // and none of the polyline, handle or easing-shape code runs.
        timeline.selection.clear();
        const std::vector<TimelineRow> &rows = animationPanel.timeline_rows();
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
          if (rows[i].kind == TimelineRow::Kind::Channel && rows[i].joint >= 0)
          {
            timeline.selection.push_back({static_cast<int>(i), 0.0f});
            timeline.selection.push_back({static_cast<int>(i), 1.0f});
          }
        }
        if (!timeline.selection.empty())
        {
          sawCurveSelection = true;
        }
        break;
      }

      case Phase::CurveHandleDrag:
      {
        // Same selection as above, but with the view range and gutter pinned
        // so the handles of every key at t=1.0 land on one known column --
        // see curve_plot_centre_x. Re-applied every frame because the raster
        // below drags across the panel's own checkboxes on its way past.
        animationPanel.set_playing(false);
        timeline.showCurves = true;
        timeline.viewStart = 0.0f;
        timeline.viewEnd = 2.0f;
        // The dope sheet clamps labelWidth to at least 60 before the curve
        // editor reads it as its gutter, so pick a value safely inside that
        // range or the plot does not start where the model below says.
        timeline.labelWidth = 120.0f;
        timeline.collapsedBones.clear();
        timeline.selection.clear();
        const std::vector<TimelineRow> &curveRows = animationPanel.timeline_rows();
        for (std::size_t i = 0; i < curveRows.size(); ++i)
        {
          if (curveRows[i].kind == TimelineRow::Kind::Channel && curveRows[i].joint >= 0)
          {
            timeline.selection.push_back({static_cast<int>(i), 0.0f});
          }
        }
        break;
      }

      case Phase::AnimatePlaying:
        animationPanel.set_playing(true);
        timeline.showCurves = (local % 2) == 0;
        break;

      case Phase::AnimatePosing:
      {
        animationPanel.set_playing(false);
        if (local == 0)
        {
          keysBeforePosing = animationPanel.clip_key_count();
        }
        // A gizmo drag, as the viewport reports it: a run of moves and then
        // one finished frame, which is what collapses a drag into one undo.
        AnimationEditState &editState = animation_edit_state();
        editState.pickedJoint = 2;
        editState.jointEdited = true;
        editState.jointEditFinished = ((local % 6) == 5);
        editState.editedTranslation = {0.0f, 1.0f + 0.01f * static_cast<float>(local), 0.0f};
        editState.editedRotation = quat_z(static_cast<float>(local));
        editState.editedScale = {1.0f, 1.0f, 1.0f};
        timeline.time = 0.2f + 0.05f * static_cast<float>(local % 8);
        break;
      }

      case Phase::AnimateCollapsed:
        // Collapsed bones fold their channel rows away, which is a different
        // row-building and hit-testing path from the expanded dope sheet.
        timeline.collapsedBones = {"Hips", "Spine"};
        timeline.snapToFrames = (local % 2) == 0;
        timeline.showCurves = (local % 3) == 0;
        timeline.time = 0.03f * static_cast<float>(local);
        break;

      case Phase::AnimateUndoRedo:
      {
        animationPanel.set_playing(false);
        timeline.showCurves = false;
        timeline.collapsedBones.clear();
        // The play head parks somewhere no track already has a key, so the
        // auto-key below lands as a *new* key and the clip's key count is a
        // witness on its own: an undo that only rewinds the label would
        // still leave the count high.
        timeline.time = 1.7f;
        if (local == 0)
        {
          animateKeysBeforeEdit = animationPanel.clip_key_count();
          animateUndoDepthBefore = animationPanel.undo_depth();
        }
        if (local >= kAnimateEditFrame && local <= kAnimateEditDone)
        {
          // One gizmo gesture, exactly as the viewport reports it: a run of
          // moves and a single finished frame, which is what collapses into
          // one undo entry.
          AnimationEditState &editState = animation_edit_state();
          editState.pickedJoint = 2;
          editState.jointEdited = true;
          editState.jointEditFinished = (local == kAnimateEditDone);
          editState.editedTranslation = {0.0f, 1.2f, 0.02f * static_cast<float>(local)};
          editState.editedRotation = quat_z(9.0f);
          editState.editedScale = {1.0f, 1.0f, 1.0f};
        }
        break;
      }

      case Phase::AnimateUndoGated:
        animationPanel.set_playing(false);
        timeline.showCurves = false;
        timeline.time = 1.7f;
        break;

      case Phase::RigViewport:
      {
        // The Rig tab's overlay is the only way a rig joint can be picked or
        // placed, and it is entirely a conversation through AnimationEditState
        // -- exactly what the 3D view writes when a bone is clicked or a
        // gizmo axis is dragged.
        AnimationEditState &editState = animation_edit_state();
        if (local == kRigPickFrame && animationPanel.rig_joint_count() > 1)
        {
          const int slot = animationPanel.rig_overlay_slot(1);
          if (slot >= 0)
          {
            editState.pickedJoint = slot;
          }
        }
        if (local >= kRigDragStart && local <= kRigDragDone)
        {
          // One gesture: a run of moves and a single finished frame, which is
          // what has to collapse into one undo entry.
          editState.jointEdited = true;
          editState.jointEditFinished = (local == kRigDragDone);
          editState.editedTranslation = {kRigDragTargetX, 0.5f, 0.0f};
          editState.editedRotation = math::Quat{};
          editState.editedScale = {1.0f, 1.0f, 1.0f};
        }
        break;
      }

      case Phase::AnimatorDragUndo:
        if (local == 0)
        {
          // Reload so the history starts empty: "the drag opened exactly one
          // entry" is only a statement about the drag if nothing else is on
          // the stack.
          animatorPanel.open_graph(context, "Locomotion");
        }
        break;

      case Phase::AnimatorPreview:
        if (local == 0)
        {
          animatorPanel.open_graph(context, "Locomotion");
          animatorPanel.set_preview_enabled(true);
        }
        break;

      case Phase::PlayModeEnter:
      case Phase::PlayModeSettled:
      case Phase::PlayModeAnyState:
      {
        // Drive the real animator so the canvas has a live state to mirror:
        // the highlight, the normalised-time bar and the running-transition
        // colour are all read from AnimatorInstance, and an instance that is
        // never ticked has no current state at all.
        hades::AnimatorInstance *instance = hades::AnimationRuntime::instance().find(character);
        if (instance != nullptr)
        {
          if (step.phase == Phase::PlayModeEnter && local == 0)
          {
            // Back to square one so the crossfade out of the default state
            // happens inside this phase rather than somewhere in the run's
            // history: mid-transition is the branch under test.
            instance->reset();
            instance->set_playing(true);
            instance->set_float("Speed", 0.7f);
            instance->set_bool("Grounded", true);
            instance->set_int("Ammo", 7);
            instance->reset_trigger("Jump");
            // Load the graph on the same frame, before the panel draws: the
            // overlay stands down for a graph the entity is not running, so
            // a frame's delay here would silently skip the first crossfade.
            animatorPanel.open_graph(context, "Locomotion");
          }
          if (step.phase == Phase::PlayModeAnyState && local == 0)
          {
            // The any-state edge is the only one with no source node, and
            // the canvas draws and colours it through a branch of its own.
            instance->set_trigger("Jump");
          }
          if (const ModelAsset *asset = ModelAssetCache::instance().get(modelPath))
          {
            instance->update(io.DeltaTime, *asset, AnimationClipCache::instance());
          }
        }

        if (step.phase == Phase::PlayModeAnyState && local == kAnyStateOtherGraph)
        {
          // Point the panel at a graph the running entity is not playing.
          // The overlay has to stand down: matching by name across two
          // unrelated graphs is exactly how the highlight lands on the wrong
          // node and the rail pokes the wrong instance's parameters.
          animatorPanel.open_graph(context, "Broken");
        }
        else if (step.phase == Phase::PlayModeAnyState && local == kAnyStateBackAgain)
        {
          animatorPanel.open_graph(context, "Locomotion");
        }
        break;
      }

      default:
        break;
      }

      // ---- Render ------------------------------------------------------
      if (local == 0 &&
          (step.phase == Phase::AnimateUndoRedo || step.phase == Phase::AnimateUndoGated ||
           step.phase == Phase::RigViewport))
      {
        // Focus the Animation panel: its undo shortcut only fires when its
        // own window tree holds focus.
        animationPanel.set_visible(editor, true);
      }
      if (local == 0 &&
          (step.phase == Phase::AnimatorDragUndo || step.phase == Phase::AnimatorUndoGated))
      {
        // Same for the Animator, which is rendered second and would otherwise
        // sit behind the Animation panel with no keyboard focus at all.
        animatorPanel.set_visible(editor, true);
      }

      // The panel opens at 1100x700, which is not tall enough to show the
      // Animate tab whole: the curve editor and the collapsing sections end up
      // scrolled out of the right-hand child, where nothing can hover them.
      // Give it the full display height once the window exists, so every
      // section is actually submitted and reachable.
      if (ImGuiWindow *panelWindow = find_window_with_suffix("Animation"))
      {
        ImGui::SetWindowSize(panelWindow, ImVec2(1100.0f, 1340.0f), ImGuiCond_Always);
      }

      ImGui::SetNextWindowPos(animationPanelPos, ImGuiCond_Always);
      const StackDepths beforeAnimation = capture_stacks();
      animationPanel.render(context);
      compare_stacks(beforeAnimation, capture_stacks(), "the Animation panel");

      ImGui::SetNextWindowPos(animatorPanelPos, ImGuiCond_Always);
      const StackDepths beforeAnimator = capture_stacks();
      animatorPanel.render(context);
      compare_stacks(beforeAnimator, capture_stacks(), "the Animator panel");

      compare_stacks(baseline, capture_stacks(), "the frame");

      // ---- Post-frame observation and next-frame steering --------------
      if (animationPanel.skeleton_joint_count() >= 6)
      {
        sawSkeleton = true;
      }
      if (animationPanel.clip_is_loaded())
      {
        sawClipLoaded = true;
      }
      if (animationPanel.timeline_rows().size() > 4)
      {
        sawTimelineRows = true;
      }
      if (animationPanel.selected_joint() >= 0)
      {
        sawJointSelected = true;
      }
      if (step.phase == Phase::AnimatePosing &&
          animationPanel.clip_key_count() > keysBeforePosing)
      {
        sawAutoKey = true;
      }
      if (animatorPanel.graph_is_loaded())
      {
        sawGraphLoaded = true;
      }
      if (animatorPanel.state_node_count() >= 4)
      {
        sawStateNodes = true;
      }
      if (animatorPanel.debug_overlay_active())
      {
        sawAnimatorDebug = true;
      }

      // ---- Rig tab: the viewport overlay, a pick, a drag and an undo -----
      if (step.phase == Phase::RigViewport)
      {
        // Imported nodes AND the rig's own joints: an overlay that published
        // only one of the two would still be non-empty, so the count is what
        // separates them.
        if (animationPanel.rig_overlay_joint_count() > animationPanel.rig_joint_count())
        {
          sawRigOverlay = true;
        }
        if (local == kRigPickFrame + 1 && animationPanel.selected_rig_joint() == 1)
        {
          sawRigPick = true;
          rigUndoDepthBefore = animationPanel.rig_undo_depth();
          rigTranslationBeforeDrag = animationPanel.rig_joint_translation(1).x;
        }
        if (local == kRigDragCheck && sawRigPick &&
            std::fabs(animationPanel.rig_joint_translation(1).x - kRigDragTargetX) < 1e-4f &&
            animationPanel.rig_undo_depth() == rigUndoDepthBefore + 1)
        {
          sawRigDrag = true;
        }
        if (local == kRigUndoCheck && sawRigDrag &&
            std::fabs(animationPanel.rig_joint_translation(1).x - rigTranslationBeforeDrag) < 1e-4f)
        {
          sawRigUndo = true;
        }
      }

      // ---- Animator: the edit-mode preview -------------------------------
      if (step.phase == Phase::AnimatorPreview && animatorPanel.preview_active() &&
          !animatorPanel.preview_state_name().empty() &&
          hades::AnimationRuntime::instance().has_preview(character))
      {
        sawAnimatorPreview = true;
      }

      // ---- Animation panel: undo, redo, and the gate ---------------------
      if (step.phase == Phase::AnimateUndoRedo)
      {
        const std::size_t keys = animationPanel.clip_key_count();
        if (local == kAnimateEditDone + 1)
        {
          animateKeysAfterEdit = keys;
          // Exactly one new history entry: a gesture that opened several
          // would leave Ctrl+Z rolling back only part of it, and the key
          // counts below would then agree for the wrong reason.
          if (animateKeysAfterEdit > animateKeysBeforeEdit &&
              animationPanel.undo_depth() == animateUndoDepthBefore + 1)
          {
            sawAnimationEdit = true;
          }
        }
        // Checked a few frames after each chord so the assertion is about
        // the clip having actually moved, not about the frame the key press
        // happened to land on.
        if (local == kAnimateUndoCheck && sawAnimationEdit &&
            keys == animateKeysBeforeEdit)
        {
          sawAnimationUndo = true;
        }
        if (local == kAnimateRedoCheck && sawAnimationUndo &&
            keys == animateKeysAfterEdit)
        {
          sawAnimationRedo = true;
        }
      }

      if (step.phase == Phase::AnimateUndoGated)
      {
        // Queue the activation until the field comes up: the child window
        // has to exist, and the request is only honoured on the frame the
        // item is submitted again.
        if (!io.WantTextInput && gatedTapFrame < 0)
        {
          activate_text_field("##animate-left", "##joint-filter");
        }
        if (gatedTapFrame >= 0 && local == gatedTapFrame - 1)
        {
          gatedUndoDepth = animationPanel.undo_depth();
          gatedChordFired = true;
        }
        // Two frames after the press: long enough for an undo to have landed
        // and been observed, which is what makes "nothing happened" evidence.
        if (gatedChordFired && local == gatedTapFrame + 2 &&
            animationPanel.undo_depth() == gatedUndoDepth)
        {
          sawAnimationUndoGated = true;
        }
        if (local == step.frames - kGatedControlOffset - 1)
        {
          controlUndoDepth = animationPanel.undo_depth();
        }
        if (local == step.frames - kGatedCheckOffset && controlUndoDepth > 0 &&
            animationPanel.undo_depth() < controlUndoDepth)
        {
          sawAnimationUndoUngated = true;
        }
      }

      // ---- Animator panel: drag, undo, redo, and the gate -----------------
      if (step.phase == Phase::AnimatorDragUndo)
      {
        float centreX = 0.0f;
        float centreY = 0.0f;
        if (animatorPanel.state_node_centre(0, centreX, centreY))
        {
          // Park on the node, press, walk it a few pixels a frame -- the
          // node follows the cursor, so re-reading its centre every frame
          // keeps the press inside it -- then let go.
          const bool dragging = (local > kAnimatorDragStart && local <= kAnimatorDragEnd);
          io.AddMousePosEvent(
              dragging ? centreX + 7.0f : centreX, dragging ? centreY + 5.0f : centreY);
          io.AddMouseButtonEvent(
              ImGuiMouseButton_Left, local >= kAnimatorDragStart && local <= kAnimatorDragEnd);
        }

        float x = 0.0f;
        float y = 0.0f;
        if (animatorPanel.state_position(0, x, y))
        {
          if (local == kAnimatorDragStart - 1)
          {
            dragBaselineX = x;
            dragBaselineY = y;
          }
          if (local == kAnimatorDragCheck)
          {
            draggedX = x;
            draggedY = y;
            // The drag has to have moved the *model*, and to have opened
            // exactly one history entry: a canvas that repainted the node
            // without recording anything would undo to nothing.
            if (!nearly_equal(draggedX, dragBaselineX) && animatorPanel.undo_depth() == 1)
            {
              sawAnimatorDrag = true;
            }
          }
          if (local == kAnimatorUndoCheck && sawAnimatorDrag &&
              nearly_equal(x, dragBaselineX) && nearly_equal(y, dragBaselineY) &&
              animatorPanel.redo_depth() == 1)
          {
            sawAnimatorUndo = true;
          }
          if (local == kAnimatorRedoCheck && sawAnimatorUndo &&
              nearly_equal(x, draggedX) && nearly_equal(y, draggedY) &&
              animatorPanel.redo_depth() == 0)
          {
            sawAnimatorRedo = true;
          }
        }
      }

      if (step.phase == Phase::AnimatorUndoGated)
      {
        if (!io.WantTextInput && gatedTapFrame < 0)
        {
          activate_text_field("##animator_rail", "##new_parameter");
        }
        if (gatedTapFrame >= 0 && local == gatedTapFrame - 1)
        {
          gatedUndoDepth = animatorPanel.undo_depth();
          gatedChordFired = true;
        }
        if (gatedChordFired && local == gatedTapFrame + 2 && gatedUndoDepth > 0 &&
            animatorPanel.undo_depth() == gatedUndoDepth)
        {
          sawAnimatorUndoGated = true;
        }
        if (local == step.frames - kGatedControlOffset - 1)
        {
          controlUndoDepth = animatorPanel.undo_depth();
        }
        if (local == step.frames - kGatedCheckOffset && controlUndoDepth > 0 &&
            animatorPanel.undo_depth() < controlUndoDepth)
        {
          sawAnimatorUndoUngated = true;
        }
      }

      // ---- Animator panel: the play-mode debug overlay --------------------
      if (playPhase)
      {
        const std::string &liveState = animatorPanel.debug_state_name();
        const bool overlay = animatorPanel.debug_overlay_active();

        if (overlay && graph_has_state(fixtureGraph, liveState))
        {
          sawDebugActiveState = true;
          if (animatorPanel.debug_normalized_time() > 0.0f)
          {
            // The progress bar is skipped entirely at fraction 0.
            sawDebugProgressBar = true;
          }
        }
        if (overlay && animatorPanel.debug_transitioning())
        {
          sawDebugTransitionRunning = true;
          if (graph_has_edge_into(fixtureGraph, liveState, false))
          {
            sawDebugArrowLit = true;
          }
          if (graph_has_edge_into(fixtureGraph, liveState, true))
          {
            sawDebugAnyStateLit = true;
          }
        }
        if (overlay && !animatorPanel.debug_transitioning() &&
            animatorPanel.debug_normalized_time() > 0.0f)
        {
          sawDebugSettled = true;
        }
        if (animatorPanel.live_parameter_rows() > 0)
        {
          sawLiveParameterRail = true;
        }
        if (overlay && animatorPanel.selected_state() >= 0)
        {
          sawStateDetailsDuringPlay = true;
        }
        if (step.phase == Phase::PlayModeAnyState &&
            local >= kAnyStateOtherGraph && local < kAnyStateBackAgain && !overlay)
        {
          sawDebugStoodDownForOtherGraph = true;
        }
      }

      if (step.phase == Phase::PlayModeAnyState)
      {
        // Select the Jump node through the canvas while the game runs: the
        // details pane only grows its live "current state / normalised time"
        // block, and its "Play this state" button, when something is
        // selected during play.
        float x = 0.0f;
        float y = 0.0f;
        if (local <= kAnyStateSelectFrom + 6 && animatorPanel.state_node_centre(3, x, y))
        {
          io.AddMousePosEvent(x, y);
          io.AddMouseButtonEvent(
              ImGuiMouseButton_Left,
              local >= kAnyStateSelectFrom + 3 && local <= kAnyStateSelectFrom + 4);
        }
        else if (local > kAnyStateSelectFrom + 6)
        {
          io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        }
      }

      // Tab switching: queued on the tab bar so the next frame draws it.
      if ((step.phase == Phase::RigTab || step.phase == Phase::RigViewport) && local == 0)
      {
        queue_tab(1);
      }
      else if (step.phase == Phase::ClipsTab && local == 0)
      {
        queue_tab(2);
      }
      else if (step.phase == Phase::AnimateIdle && local == 0)
      {
        queue_tab(0);
      }
      if (step.phase == Phase::RigTab && animationPanel.active_tab() == 1)
      {
        sawRigTab = true;
      }
      if (step.phase == Phase::ClipsTab && animationPanel.active_tab() == 2)
      {
        sawClipsTab = true;
      }

      // Animator: load the graph, then aim real clicks at the node
      // rectangles this frame produced. Selecting through the canvas rather
      // than through a setter is the point -- it is the only thing that
      // proves the canvas hit-tests at all.
      if (step.phase == Phase::AnimatorLoaded && local == 0)
      {
        animatorPanel.open_graph(context, "Locomotion");
      }
      else if (step.phase == Phase::AnimatorBrokenGraph && local == 0)
      {
        animatorPanel.open_graph(context, "Broken");
      }

      if (step.phase == Phase::AnimatorStateSelected)
      {
        // Dwell on the node, then press and release on the spot.
        const int beat = local % 8;
        float x = 0.0f;
        float y = 0.0f;
        if (animatorPanel.state_node_centre((local / 8) % 4, x, y))
        {
          io.AddMousePosEvent(x, y);
          io.AddMouseButtonEvent(ImGuiMouseButton_Left, beat == 3 || beat == 4);
        }
        canvasSelectedState = std::max(canvasSelectedState, animatorPanel.selected_state());
      }

      if (step.phase == Phase::CurveHandleDrag)
      {
        // Park, press, drag down, release -- one probe every four frames,
        // walking down a single column. A key handle is a 10x10 button, so
        // 8px steps cannot step over one; the small horizontal jitter absorbs
        // rounding in the time-to-pixel mapping.
        constexpr int kRows = 160;
        const int beat = local % 4;
        const int probe = local / 4;
        float centreX = 0.0f;
        float top = 0.0f;
        float bottom = 0.0f;
        if (curve_plot_centre_x(120.0f, centreX) && animation_panel_work_rect(top, bottom))
        {
          const float x = centreX + static_cast<float>((probe / kRows) % 3 - 1) * 3.0f;
          const float y =
              std::min(top + 8.0f * static_cast<float>(probe % kRows), bottom - 2.0f);
          const bool dragging = (beat >= 2);
          io.AddMousePosEvent(x, dragging ? y + 14.0f : y);
          io.AddMouseButtonEvent(ImGuiMouseButton_Left, beat == 1 || beat == 2);
        }
        if (animationPanel.last_undo_label() == "edit key value")
        {
          sawCurveHandleDrag = true;
        }
      }

      if (step.phase == Phase::AnimatorTransitionSelected)
      {
        // Idle -> Locomotion runs between node 0 and node 1, but the arrow is
        // a bezier pushed off the centre line by kTransitionOffset and bent
        // again by its control points, so the straight midpoint misses it.
        // Sweep across the gap instead: the hit radius is a few pixels, and
        // this stays honest if the curve is reshaped later.
        constexpr int kDwell = 6;
        constexpr int kProbes = 13;
        const int probe = (local / kDwell) % kProbes;
        const int beat = local % kDwell;
        float ax = 0.0f;
        float ay = 0.0f;
        float bx = 0.0f;
        float by = 0.0f;
        if (animatorPanel.state_node_centre(0, ax, ay) &&
            animatorPanel.state_node_centre(1, bx, by))
        {
          const float dy = -12.0f + 4.0f * static_cast<float>(probe);
          io.AddMousePosEvent((ax + bx) * 0.5f, (ay + by) * 0.5f + dy);
          io.AddMouseButtonEvent(ImGuiMouseButton_Left, beat == 4);
        }
        canvasSelectedTransition =
            std::max(canvasSelectedTransition, animatorPanel.selected_transition());
      }

      ImGui::Render();
      check_clip_rects_after_render();

      // The curve-handle raster is a search, not a coverage sweep: once a
      // handle has been grabbed there is nothing left to learn from walking
      // the rest of the column.
      if (step.phase == Phase::CurveHandleDrag && sawCurveHandleDrag)
      {
        ++frame;
        break;
      }
    }
  }

  const int totalFrames = frame;

  const std::size_t finalKeyCount = animationPanel.clip_key_count();
  const std::size_t finalJointCount = animationPanel.skeleton_joint_count();
  const std::size_t finalRowCount = animationPanel.timeline_rows().size();

  animationPanel.set_visible(editor, false);
  animatorPanel.set_visible(editor, false);

  ImGui_ImplNullRender_Shutdown();
  ImGui_ImplNullPlatform_Shutdown();
  ImGui::DestroyContext();

  hades::AnimationRuntime::instance().clear();
  ModelAssetCache::instance().clear();
  AnimationClipCache::instance().clear();
  // The fixture is the only rigged model in the tree, so it is also the only
  // thing to open the editor on when a panel has to be looked at rather than
  // asserted about. Keeping it is opt-in so CI still leaves nothing behind.
  if (std::getenv("HADES_SMOKE_KEEP_WORKSPACE") == nullptr)
  {
    std::filesystem::remove_all(workspace, errorCode);
  }
  else
  {
    std::printf("animation smoke: kept workspace at %s\n", workspace.string().c_str());
  }

  // ---- Verdict ------------------------------------------------------------

  g_phase = "coverage check";
  g_frame = totalFrames;
  if (!sawSkeleton)
  {
    fail("the fixture rig never reached the panel — the skeleton stayed empty, so the "
         "joint tree, dope sheet and bone overlay never drew");
  }
  if (!sawClipLoaded)
  {
    fail("the authored clip never loaded, so the dope sheet drew empty");
  }
  if (!sawTimelineRows)
  {
    fail("the dope sheet built no rows for a clip with four keyed joints");
  }
  if (!sawJointSelected)
  {
    fail("no joint was ever selected — the viewport's joint pick was not consumed");
  }
  if (!sawCurveSelection)
  {
    fail("no channel rows were available to plot, so the curve editor only ever drew "
         "its placeholder");
  }
  if (!sawCurveHandleDrag)
  {
    fail("no drag anywhere over the curve editor ever grabbed a key handle — the "
         "handles are unreachable, so curve values cannot be edited at all");
  }
  if (!sawAutoKey)
  {
    fail("a gizmo pose edit with auto-key on wrote no key to the clip");
  }
  if (!sawRigTab)
  {
    fail("the Rig tab never became active, so its draw path never ran");
  }
  if (!sawRigOverlay)
  {
    fail("the Rig tab published no viewport overlay — bones cannot be seen, picked or "
         "dragged, so the rig is authored blind");
  }
  if (!sawRigPick)
  {
    fail("a viewport joint pick on the Rig tab selected no rig joint");
  }
  if (!sawRigDrag)
  {
    fail("a gizmo drag on the Rig tab did not write the joint's rest translation, or "
         "opened more than one undo entry for a single gesture");
  }
  if (!sawRigUndo)
  {
    fail("Ctrl+Z on the Rig tab did not roll the dragged joint back — the rig has no "
         "history of its own, or the shortcut reached the clip stack instead");
  }
  if (!sawAnimatorPreview)
  {
    fail("the Animator's edit-mode preview never drove an entity — the graph is still "
         "inert until play mode");
  }
  if (!sawClipsTab)
  {
    fail("the Clips tab never became active, so its draw path never ran");
  }
  if (!sawGraphLoaded)
  {
    fail("the animator graph never loaded, so the canvas drew its empty state only");
  }
  if (!sawStateNodes)
  {
    fail("the canvas laid out no state nodes for a four-state graph");
  }
  if (!sawAnimatorDebug)
  {
    fail("the Animator never mirrored the live animator during play mode, so the "
         "active-state highlight and transition debug drawing never ran");
  }
  if (!sawDebugActiveState)
  {
    fail("the live animator's state never matched a node on the canvas, so the "
         "active-state border and its progress bar never drew — the overlay was "
         "available but pointed at nothing");
  }
  if (!sawDebugProgressBar)
  {
    fail("the active node's normalised-time bar never had a non-zero fraction, so "
         "the progress-bar branch never drew");
  }
  if (!sawDebugTransitionRunning)
  {
    fail("the live animator was never observed mid-crossfade, so the running "
         "transition branch of the canvas never ran");
  }
  if (!sawDebugArrowLit)
  {
    fail("no transition arrow was ever coloured as running: the canvas matched no "
         "edge against the state the animator was crossfading into");
  }
  if (!sawDebugAnyStateLit)
  {
    fail("the any-state stub was never drawn as running — it has no source node "
         "and is the one edge the canvas draws through a separate branch");
  }
  if (!sawDebugSettled)
  {
    fail("the animator never settled on a state with the overlay up, so the "
         "not-transitioning half of the debug drawing never ran");
  }
  if (!sawLiveParameterRail)
  {
    fail("the parameter rail never read a value from the running instance, so its "
         "live-value path is dead and the rail was showing authored defaults while "
         "the game ran");
  }
  if (!sawStateDetailsDuringPlay)
  {
    fail("no state was selected while play mode ran, so the details pane's live "
         "'current state / normalised time' block and its Play-this-state button "
         "never drew");
  }
  if (!sawDebugStoodDownForOtherGraph)
  {
    fail("the overlay stayed up after the panel was pointed at a graph the running "
         "entity is not playing — the canvas would be highlighting another graph's "
         "nodes by name and the rail poking the wrong instance");
  }
  if (!sawAnimationEdit)
  {
    fail("the gizmo gesture in the undo phase keyed nothing, so there was no edit "
         "for Ctrl+Z to take back");
  }
  if (!sawAnimationUndo)
  {
    fail("Ctrl+Z over the Animation panel did not take the clip back to its "
         "pre-edit key count — the panel's undo shortcut is dead");
  }
  if (!sawAnimationRedo)
  {
    fail("Ctrl+Shift+Z over the Animation panel did not put the undone key back — "
         "the panel's redo shortcut is dead");
  }
  if (!sawAnimationUndoGated)
  {
    fail("Ctrl+Z over the Animation panel still undid while a text field was "
         "active — typing Ctrl+Z into a name field would roll the whole clip back "
         "as well as the keystroke");
  }
  if (!sawAnimationUndoUngated)
  {
    fail("the control tap at the end of the Animation panel's gated phase undid "
         "nothing either, so that phase proved nothing: the shortcut was dead for "
         "want of focus, not held back by the text-input gate");
  }
  if (!sawAnimatorDrag)
  {
    fail("dragging a state node on the canvas moved nothing, or recorded no undo "
         "entry, so the Animator had no edit to undo");
  }
  if (!sawAnimatorUndo)
  {
    fail("Ctrl+Z over the Animator did not put the dragged node back where it "
         "started — the panel's undo shortcut is dead");
  }
  if (!sawAnimatorRedo)
  {
    fail("Ctrl+Shift+Z over the Animator did not re-apply the undone move — the "
         "panel's redo shortcut is dead");
  }
  if (!sawAnimatorUndoGated)
  {
    fail("Ctrl+Z over the Animator still undid while a text field was active — "
         "typing Ctrl+Z into a parameter name would roll the whole graph back as "
         "well as the keystroke");
  }
  if (!sawAnimatorUndoUngated)
  {
    fail("the control tap at the end of the Animator's gated phase undid nothing "
         "either, so that phase proved nothing: the shortcut was dead for want of "
         "focus, not held back by the text-input gate");
  }
  if (canvasSelectedState < 0)
  {
    fail("clicks on the state nodes never selected one — canvas hit-testing is broken");
  }
  if (canvasSelectedTransition < 0)
  {
    fail("clicks on a transition arrow never selected one — transition hit-testing is broken");
  }

  if (!g_failures.empty())
  {
    std::fprintf(stderr, "\nanimation smoke: FAILED with %zu problem(s)\n", g_failures.size());
    for (const Failure &failure : g_failures)
    {
      std::fprintf(
          stderr, "  frame %d, %s: %s\n", failure.frame, failure.phase.c_str(),
          failure.message.c_str());
    }
    return 1;
  }

  std::printf(
      "animation smoke: %d frames rendered cleanly over %zu panel states, "
      "%zu-joint rig, %zu dope-sheet rows, %zu clip keys, state %d and transition %d "
      "selected through the canvas, a node dragged and walked back and forward "
      "through Ctrl+Z/Ctrl+Shift+Z on both panels, both shortcuts correctly ignored "
      "over a live text field, a rig joint picked and dragged into place through the "
      "viewport overlay and walked back with Ctrl+Z, the Animator's edit-mode preview "
      "observed driving an entity, and the play-mode overlay observed active, "
      "mid-crossfade, on an any-state stub, feeding the parameter rail, and stood "
      "down for a foreign graph\n",
      totalFrames,
      sizeof(plan) / sizeof(plan[0]),
      finalJointCount,
      finalRowCount,
      finalKeyCount,
      canvasSelectedState,
      canvasSelectedTransition);
  return 0;
}
