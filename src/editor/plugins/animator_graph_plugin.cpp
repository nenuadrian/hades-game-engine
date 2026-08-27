#include "imgui.h"

#include "animator_graph_plugin.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "../../engine/animation/animation_clip_cache.hpp"
#include "../../engine/animation/animation_runtime.hpp"
#include "../../engine/animation/animator_instance.hpp"
#include "../../engine/assets/model_asset.hpp"
#include "../../engine/assets/model_asset_cache.hpp"
#include "../../engine/components/animator_component.hpp"
#include "../../engine/components/model_component.hpp"
#include "../../engine/components/name_component.hpp"
#include "../../engine/core/ecs/entity_manager.hpp"
#include "../../engine/core/ecs/world_utils.hpp"
#include "../../engine/core/ecs/component_manager.hpp"
#include "../IconsFontAwesome6.h"
#include "animation_editor_plugin.hpp"
#include "../editor.hpp"

namespace hades
{
  namespace
  {
    // ---- Canvas metrics ----------------------------------------------------

    constexpr float kNodeWidth = 172.0f;
    constexpr float kNodeHeight = 58.0f;
    constexpr float kGridStep = 32.0f;
    constexpr float kMinZoom = 0.35f;
    constexpr float kMaxZoom = 2.5f;
    constexpr float kTransitionOffset = 9.0f;
    constexpr float kTransitionHitRadius = 7.0f;
    constexpr int kTransitionSamples = 24;
    constexpr std::size_t kMaxUndo = 64;
    constexpr float kListPollInterval = 1.0f;

    /// Reference the edit-mode preview stages its working graph under.
    ///
    /// Deliberately not the graph's own name: AnimatorInstance resolves its
    /// graph through the shared clip cache, so staging under the real name
    /// would hand every other animator in the editor — and a game started
    /// without saving — the unsaved working copy.
    constexpr const char *kPreviewGraphReference = "__hades_animator_preview";

    constexpr ImU32 kCanvasBackground = IM_COL32(24, 26, 31, 255);
    constexpr ImU32 kGridLine = IM_COL32(36, 40, 48, 255);
    constexpr ImU32 kGridLineBold = IM_COL32(48, 54, 64, 255);
    constexpr ImU32 kNodeFill = IM_COL32(50, 56, 68, 245);
    constexpr ImU32 kNodeFillDefault = IM_COL32(46, 66, 58, 245);
    constexpr ImU32 kNodeBorder = IM_COL32(92, 100, 116, 255);
    constexpr ImU32 kNodeBorderHover = IM_COL32(150, 160, 178, 255);
    constexpr ImU32 kNodeBorderSelected = IM_COL32(236, 196, 96, 255);
    constexpr ImU32 kDefaultAccent = IM_COL32(120, 200, 140, 255);
    constexpr ImU32 kActiveBorder = IM_COL32(255, 226, 120, 255);
    constexpr ImU32 kProgressFill = IM_COL32(120, 200, 140, 190);
    constexpr ImU32 kTransitionColour = IM_COL32(148, 158, 176, 255);
    constexpr ImU32 kTransitionHover = IM_COL32(214, 222, 236, 255);
    constexpr ImU32 kTransitionSelected = IM_COL32(236, 196, 96, 255);
    constexpr ImU32 kTransitionRunning = IM_COL32(255, 226, 120, 255);
    constexpr ImU32 kAnyStateColour = IM_COL32(132, 172, 232, 255);
    constexpr ImU32 kTextPrimary = IM_COL32(238, 240, 244, 255);
    constexpr ImU32 kTextSecondary = IM_COL32(162, 170, 184, 255);

    template <std::size_t Size>
    void set_buffer_text(std::array<char, Size> &buffer, const std::string &value)
    {
      buffer.fill('\0');
      const std::size_t copyLength = std::min(value.size(), Size - 1);
      std::copy_n(value.data(), copyLength, buffer.data());
      buffer[copyLength] = '\0';
    }

    const char *param_type_icon(AnimParamType type)
    {
      switch (type)
      {
      case AnimParamType::Float:
        return ICON_FA_SLIDERS;
      case AnimParamType::Int:
        return ICON_FA_HASHTAG;
      case AnimParamType::Bool:
        return ICON_FA_TOGGLE_ON;
      case AnimParamType::Trigger:
        return ICON_FA_BOLT;
      }

      return ICON_FA_SLIDERS;
    }

    const char *state_kind_label(AnimStateKind kind)
    {
      switch (kind)
      {
      case AnimStateKind::Clip:
        return "Clip";
      case AnimStateKind::BlendTree1D:
        return "Blend Tree 1D";
      case AnimStateKind::BlendTree2D:
        return "Blend Tree 2D";
      }

      return "Clip";
    }

    /// A blend tree branches on numbers only; bools and triggers are for
    /// transitions.
    bool param_is_numeric(AnimParamType type)
    {
      return type == AnimParamType::Float || type == AnimParamType::Int;
    }

    std::string entity_label(Entity::EntityId entity, ComponentManager &componentManager)
    {
      if (componentManager.hasComponent<NameComponent>(entity))
      {
        const std::string &name = componentManager.getComponent<NameComponent>(entity).value;
        if (!name.empty())
        {
          return name + " (#" + std::to_string(entity) + ")";
        }
      }
      return "Entity #" + std::to_string(entity);
    }

    std::string unique_state_name(const AnimLayer &layer, const std::string &base)
    {
      if (layer.find_state(base) < 0)
      {
        return base;
      }

      for (int suffix = 1; suffix < 4096; ++suffix)
      {
        const std::string candidate = base + " " + std::to_string(suffix);
        if (layer.find_state(candidate) < 0)
        {
          return candidate;
        }
      }

      return base;
    }

    std::string node_subtitle(const AnimState &state)
    {
      if (state.kind == AnimStateKind::Clip)
      {
        return state.clip.empty() ? std::string("no clip") : state.clip;
      }

      return "blend tree, " + std::to_string(state.entries.size()) + " clips";
    }

    // ---- Geometry ----------------------------------------------------------

    ImVec2 rect_centre(const float minX, const float minY, const float maxX, const float maxY)
    {
      return ImVec2((minX + maxX) * 0.5f, (minY + maxY) * 0.5f);
    }

    /// Where the segment from the rectangle's centre towards `target` leaves
    /// the rectangle. Keeps arrows anchored on the border instead of vanishing
    /// under the node.
    ImVec2 rect_border_point(const ImVec2 &centre, float halfWidth, float halfHeight, const ImVec2 &target)
    {
      const float dx = target.x - centre.x;
      const float dy = target.y - centre.y;
      if (std::fabs(dx) < 0.0001f && std::fabs(dy) < 0.0001f)
      {
        return centre;
      }

      float scale = 1.0e6f;
      if (std::fabs(dx) > 0.0001f)
      {
        scale = std::min(scale, halfWidth / std::fabs(dx));
      }
      if (std::fabs(dy) > 0.0001f)
      {
        scale = std::min(scale, halfHeight / std::fabs(dy));
      }

      return ImVec2(centre.x + dx * scale, centre.y + dy * scale);
    }

    ImVec2 bezier_point(const ImVec2 &a, const ImVec2 &b, const ImVec2 &c, const ImVec2 &d, float t)
    {
      const float u = 1.0f - t;
      return ImVec2(
          u * u * u * a.x + 3.0f * u * u * t * b.x + 3.0f * u * t * t * c.x + t * t * t * d.x,
          u * u * u * a.y + 3.0f * u * u * t * b.y + 3.0f * u * t * t * c.y + t * t * t * d.y);
    }

    float distance_to_bezier(
        const ImVec2 &a, const ImVec2 &b, const ImVec2 &c, const ImVec2 &d, const ImVec2 &point)
    {
      float best = 1.0e9f;
      ImVec2 previous = a;

      for (int i = 1; i <= kTransitionSamples; ++i)
      {
        const float t = static_cast<float>(i) / static_cast<float>(kTransitionSamples);
        const ImVec2 current = bezier_point(a, b, c, d, t);

        const float segmentX = current.x - previous.x;
        const float segmentY = current.y - previous.y;
        const float lengthSquared = segmentX * segmentX + segmentY * segmentY;
        float projection = 0.0f;
        if (lengthSquared > 0.0f)
        {
          projection = std::clamp(
              ((point.x - previous.x) * segmentX + (point.y - previous.y) * segmentY) / lengthSquared,
              0.0f,
              1.0f);
        }

        const float closestX = previous.x + segmentX * projection;
        const float closestY = previous.y + segmentY * projection;
        const float dx = point.x - closestX;
        const float dy = point.y - closestY;
        best = std::min(best, std::sqrt(dx * dx + dy * dy));
        previous = current;
      }

      return best;
    }

    void draw_arrow_head(ImDrawList *drawList, const ImVec2 &tip, const ImVec2 &direction, float size, ImU32 colour)
    {
      const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
      const ImVec2 unit = length > 0.0001f
                              ? ImVec2(direction.x / length, direction.y / length)
                              : ImVec2(1.0f, 0.0f);
      const ImVec2 normal(-unit.y, unit.x);
      const ImVec2 back(tip.x - unit.x * size, tip.y - unit.y * size);
      drawList->AddTriangleFilled(
          tip,
          ImVec2(back.x + normal.x * size * 0.5f, back.y + normal.y * size * 0.5f),
          ImVec2(back.x - normal.x * size * 0.5f, back.y - normal.y * size * 0.5f),
          colour);
    }

    void draw_scaled_text(
        ImDrawList *drawList, const ImVec2 &position, ImU32 colour, float fontSize,
        const std::string &text, float wrapWidth, const ImVec2 &clipMin, const ImVec2 &clipMax)
    {
      if (fontSize < 5.0f)
      {
        return;
      }

      const ImVec4 clip(clipMin.x, clipMin.y, clipMax.x, clipMax.y);
      drawList->AddText(
          ImGui::GetFont(), fontSize, position, colour, text.c_str(), nullptr, wrapWidth, &clip);
    }

    // ---- Small reusable widgets --------------------------------------------

    /// `imported` holds "model.fbx#Walk" references — animation that came
    /// inside the character's model file, which the animator can now play
    /// without it first being baked into `.hades/animations`. Listed above
    /// the authored clips because it is what a freshly imported character
    /// already has.
    bool clip_combo(const char *label, std::string &value, const std::vector<std::string> &clips,
                    const std::vector<std::string> &imported = {})
    {
      bool changed = false;
      const char *preview = value.empty() ? "(none)" : value.c_str();
      if (ImGui::BeginCombo(label, preview))
      {
        if (ImGui::Selectable("(none)", value.empty()))
        {
          value.clear();
          changed = true;
        }

        if (!imported.empty())
        {
          ImGui::TextDisabled("In the model");
          for (const auto &clip : imported)
          {
            ImGui::PushID(clip.c_str());
            const bool selected = (clip == value);
            if (ImGui::Selectable(clip.c_str(), selected))
            {
              value = clip;
              changed = true;
            }
            if (selected)
            {
              ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
          }
          ImGui::Separator();
          ImGui::TextDisabled("Authored clips");
        }

        for (const auto &clip : clips)
        {
          const bool selected = (clip == value);
          if (ImGui::Selectable(clip.c_str(), selected))
          {
            value = clip;
            changed = true;
          }
          if (selected)
          {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      return changed;
    }

    bool parameter_combo(
        const char *label, std::string &value, const std::vector<AnimParameter> &parameters, bool numericOnly)
    {
      bool changed = false;
      const char *preview = value.empty() ? "(none)" : value.c_str();
      if (ImGui::BeginCombo(label, preview))
      {
        if (ImGui::Selectable("(none)", value.empty()))
        {
          value.clear();
          changed = true;
        }

        for (const auto &parameter : parameters)
        {
          if (numericOnly && !param_is_numeric(parameter.type))
          {
            continue;
          }

          const bool selected = (parameter.name == value);
          if (ImGui::Selectable(parameter.name.c_str(), selected))
          {
            value = parameter.name;
            changed = true;
          }
          if (selected)
          {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      return changed;
    }

    /// Operators that make sense for the parameter a condition names. A bool
    /// compared with `>` is the classic authoring mistake this rules out.
    bool op_allowed_for(AnimParamType type, AnimConditionOp op)
    {
      const bool boolean = (op == AnimConditionOp::IsTrue || op == AnimConditionOp::IsFalse);
      if (type == AnimParamType::Bool || type == AnimParamType::Trigger)
      {
        return boolean;
      }

      return !boolean;
    }

    AnimConditionOp default_op_for(AnimParamType type)
    {
      if (type == AnimParamType::Bool || type == AnimParamType::Trigger)
      {
        return AnimConditionOp::IsTrue;
      }

      return type == AnimParamType::Int ? AnimConditionOp::Equals : AnimConditionOp::Greater;
    }

    bool condition_op_combo(const char *label, AnimConditionOp &op, AnimParamType type)
    {
      constexpr std::array<AnimConditionOp, 8> kAllOps = {
          AnimConditionOp::Greater, AnimConditionOp::Less, AnimConditionOp::GreaterOrEqual,
          AnimConditionOp::LessOrEqual, AnimConditionOp::Equals, AnimConditionOp::NotEquals,
          AnimConditionOp::IsTrue, AnimConditionOp::IsFalse};

      bool changed = false;
      if (ImGui::BeginCombo(label, anim_condition_op_name(op)))
      {
        for (const AnimConditionOp candidate : kAllOps)
        {
          if (!op_allowed_for(type, candidate))
          {
            continue;
          }

          const bool selected = (candidate == op);
          if (ImGui::Selectable(anim_condition_op_name(candidate), selected))
          {
            op = candidate;
            changed = true;
          }
          if (selected)
          {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      return changed;
    }

    std::vector<std::string> split_lines(const char *text)
    {
      std::vector<std::string> lines;
      std::string current;
      for (const char *cursor = text; *cursor != '\0'; ++cursor)
      {
        if (*cursor == '\n' || *cursor == '\r')
        {
          if (!current.empty())
          {
            lines.push_back(current);
            current.clear();
          }
          continue;
        }

        current.push_back(*cursor);
      }

      if (!current.empty())
      {
        lines.push_back(current);
      }

      return lines;
    }

    std::string join_lines(const std::vector<std::string> &values)
    {
      std::string joined;
      for (const auto &value : values)
      {
        joined += value;
        joined += '\n';
      }

      return joined;
    }

    bool directory_stamp(const std::filesystem::path &directory, std::filesystem::file_time_type &out)
    {
      std::error_code errorCode;
      if (!std::filesystem::exists(directory, errorCode))
      {
        out = std::filesystem::file_time_type{};
        return true;
      }

      const auto stamp = std::filesystem::last_write_time(directory, errorCode);
      if (errorCode)
      {
        return false;
      }

      out = stamp;
      return true;
    }
  }

  // ---------------------------------------------------------------------------
  // Frame entry point
  // ---------------------------------------------------------------------------

  void AnimatorGraphPlugin::render(EditorPluginContext &context)
  {
    if (!visible_)
    {
      // render() runs every frame regardless of visibility, which is what
      // makes it the right place to notice the preview should stop.
      release_preview();
      return;
    }

    if (focusRequested_)
    {
      ImGui::SetNextWindowFocus();
      focusRequested_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(1180.0f, 720.0f), ImGuiCond_FirstUseEver);
    bool open = visible_;
    if (!ImGui::Begin(ICON_FA_PERSON_RUNNING "  Animator", &open))
    {
      ImGui::End();
      visible_ = open;
      // Collapsed counts as "not rendering": the preview would otherwise hold
      // the character in whatever state it reached and never advance again.
      release_preview();
      return;
    }
    visible_ = open;

    draw_panel(context);
    ImGui::End();

    if (!visible_)
    {
      release_preview();
    }
  }

  AnimatorGraphPlugin::~AnimatorGraphPlugin()
  {
    // AnimationRuntime outlives the editor's plugin list on shutdown, so a
    // palette left published here would keep a dead entity's pose alive.
    release_preview();
  }

  // ---- Edit-mode preview ----------------------------------------------------

  void AnimatorGraphPlugin::release_preview()
  {
    if (publishedPreviewEntity_ != Entity::INVALID)
    {
      AnimationRuntime::instance().clear_preview(publishedPreviewEntity_);
      publishedPreviewEntity_ = Entity::INVALID;
    }

    if (previewInstanceBound_)
    {
      previewInstance_.reset();
      previewInstance_.set_graph_reference(std::string());
      previewInstanceBound_ = false;
      // The staged copy is the panel's, not the workspace's; leaving it in
      // the cache would let a later lookup of the same reference answer with
      // a graph that has no file behind it.
      AnimationClipCache::instance().invalidate(kPreviewGraphReference);
    }
  }

  void AnimatorGraphPlugin::refresh_imported_clip_list(EditorPluginContext &context)
  {
    // Whichever character this graph is about: the preview target if one is
    // bound, otherwise the selected entity. Either way it is the model whose
    // own animation the states are most likely to want.
    Entity::EntityId source = previewEntity_;
    if (source == Entity::INVALID && context.editor.state.selectedEntity.has_value())
    {
      source = *context.editor.state.selectedEntity;
    }

    std::string modelPath;
    if (source != Entity::INVALID && context.componentManager.hasComponent<ModelComponent>(source))
    {
      modelPath = context.componentManager.getComponent<ModelComponent>(source).assetPath;
    }

    if (modelPath == importedClipModel_)
    {
      return;
    }

    importedClipModel_ = modelPath;
    importedClipList_ = AnimationClipCache::instance().listImportedClips(modelPath);
  }

  void AnimatorGraphPlugin::resolve_preview_target(EditorPluginContext &context)
  {
    previewCandidates_.clear();

    const auto activeWorld = context.editor.state.activeWorld;
    for (const Entity::EntityId entity : context.entityManager.getActiveEntities())
    {
      if (!context.componentManager.hasComponent<ModelComponent>(entity))
      {
        continue;
      }
      if (activeWorld.has_value() &&
          !entity_belongs_to_world(entity, *activeWorld, context.componentManager))
      {
        continue;
      }
      previewCandidates_.push_back(entity);
    }

    const auto known = [this](Entity::EntityId entity)
    {
      return std::find(previewCandidates_.begin(), previewCandidates_.end(), entity) !=
             previewCandidates_.end();
    };

    if (previewFollowSelection_ && context.editor.state.selectedEntity.has_value() &&
        known(*context.editor.state.selectedEntity))
    {
      previewEntity_ = *context.editor.state.selectedEntity;
    }

    if (previewEntity_ != Entity::INVALID && !known(previewEntity_))
    {
      // Deleted, or the world changed underneath the panel.
      previewEntity_ = Entity::INVALID;
    }

    if (previewEntity_ == Entity::INVALID && previewFollowSelection_ && !previewCandidates_.empty())
    {
      // Turning the preview on with nothing selected should show something
      // rather than an empty target and an explanation.
      previewEntity_ = previewCandidates_.front();
    }
  }

  void AnimatorGraphPlugin::update_preview(EditorPluginContext &context)
  {
    previewError_.clear();

    if (!previewEnabled_ || !graphLoaded_ || context.editor.state.isPlaying)
    {
      // Play mode owns the character: the game's own animator is running, and
      // a preview palette published over it would freeze exactly what the
      // user opened this panel to watch.
      release_preview();
      return;
    }

    resolve_preview_target(context);
    if (previewEntity_ == Entity::INVALID)
    {
      release_preview();
      previewError_ = "No entity with a Model component in this world to preview on.";
      return;
    }

    const std::string modelPath =
        context.componentManager.getComponent<ModelComponent>(previewEntity_).assetPath;
    const ModelAsset *asset =
        modelPath.empty() ? nullptr : ModelAssetCache::instance().get(modelPath);
    if (asset == nullptr)
    {
      release_preview();
      previewError_ = modelPath.empty() ? "The target entity has no model."
                                        : "Could not load " + modelPath + ".";
      return;
    }

    AnimationClipCache &clips = AnimationClipCache::instance();
    // Re-staged every frame. The panel edits `graph_` in place through a
    // dozen widgets and carries no revision counter, so re-staging is what
    // makes a retimed transition or a moved blend threshold show up on the
    // very next frame instead of on the next save.
    clips.stageGraph(kPreviewGraphReference, graph_);

    if (!previewInstanceBound_)
    {
      previewInstance_.set_graph_reference(kPreviewGraphReference);
      previewInstanceBound_ = true;
    }

    previewInstance_.set_playing(previewPlaying_);
    previewInstance_.set_speed(previewSpeed_);
    previewInstance_.update(context.deltaTime, *asset, clips);

    if (publishedPreviewEntity_ != Entity::INVALID && publishedPreviewEntity_ != previewEntity_)
    {
      AnimationRuntime::instance().clear_preview(publishedPreviewEntity_);
    }

    AnimationRuntime::instance().set_preview_palette(previewEntity_, previewInstance_.palette());
    publishedPreviewEntity_ = previewEntity_;
  }

  AnimatorInstance *AnimatorGraphPlugin::active_instance(EditorPluginContext &context)
  {
    if (context.editor.state.isPlaying)
    {
      const Entity::EntityId entity = debug_entity(context);
      return entity == Entity::INVALID ? nullptr : AnimationRuntime::instance().find(entity);
    }

    return publishedPreviewEntity_ != Entity::INVALID ? &previewInstance_ : nullptr;
  }

  void AnimatorGraphPlugin::assign_graph_to_entity(
      EditorPluginContext &context, Entity::EntityId entity)
  {
    if (entity == Entity::INVALID || !graphLoaded_ || graphName_.empty())
    {
      return;
    }

    if (!context.componentManager.hasComponent<AnimatorComponent>(entity))
    {
      context.componentManager.addComponent(entity, AnimatorComponent{});
    }

    auto &animator = context.componentManager.getComponent<AnimatorComponent>(entity);
    animator.graphPath = graphName_;
    statusMessage_ = "'" + graphName_ + "' assigned to the selected entity.";
    errorMessage_.clear();
  }

  void AnimatorGraphPlugin::draw_preview_bar(EditorPluginContext &context)
  {
    if (!graphLoaded_)
    {
      return;
    }

    ImGui::Separator();

    if (context.editor.state.isPlaying)
    {
      ImGui::TextColored(
          ImVec4(0.47f, 0.78f, 0.55f, 1.0f),
          ICON_FA_PLAY "  Play mode — mirroring the running animator on the selected entity.");
      return;
    }

    if (ImGui::Checkbox(ICON_FA_EYE "  Preview", &previewEnabled_) && !previewEnabled_)
    {
      release_preview();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip(
          "Run this graph on an entity in the viewport without entering play mode.\n"
          "Unsaved edits are previewed exactly as they stand.");
    }

    ImGui::BeginDisabled(!previewEnabled_);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    const std::string targetPreview =
        previewEntity_ == Entity::INVALID
            ? std::string("<no target>")
            : entity_label(previewEntity_, context.componentManager);
    if (ImGui::BeginCombo("##animator_preview_target", targetPreview.c_str()))
    {
      for (const Entity::EntityId entity : previewCandidates_)
      {
        ImGui::PushID(static_cast<int>(entity));
        const bool selected = entity == previewEntity_;
        if (ImGui::Selectable(entity_label(entity, context.componentManager).c_str(), selected))
        {
          previewEntity_ = entity;
          // Picking a target by hand is the user overriding the follow, and
          // leaving it on would snap straight back to the selection.
          previewFollowSelection_ = false;
        }
        ImGui::PopID();
      }
      if (previewCandidates_.empty())
      {
        ImGui::TextDisabled("no entities with a model in this world");
      }
      ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::Checkbox("Follow selection", &previewFollowSelection_);

    ImGui::SameLine();
    if (ImGui::Button(previewPlaying_ ? ICON_FA_PAUSE "##animator_preview_play"
                                      : ICON_FA_PLAY "##animator_preview_play"))
    {
      previewPlaying_ = !previewPlaying_;
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip(previewPlaying_ ? "Pause the preview" : "Resume the preview");
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ROTATE_LEFT "##animator_preview_restart"))
    {
      previewInstance_.reset();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip("Restart from the default state");
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::SliderFloat("##animator_preview_speed", &previewSpeed_, 0.0f, 4.0f, "speed %.2fx");

    ImGui::EndDisabled();

    if (!previewEnabled_)
    {
      ImGui::TextDisabled(
          "Turn on Preview to run this graph on a character now — the parameter rail then drives "
          "it live, and the canvas highlights the state it is in.");
    }
    else if (!previewError_.empty())
    {
      ImGui::TextColored(
          ImVec4(0.95f, 0.78f, 0.35f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s",
          previewError_.c_str());
    }
    else if (publishedPreviewEntity_ != Entity::INVALID)
    {
      const std::string state = previewInstance_.current_state(activeLayer_);
      const std::string clip = previewInstance_.current_clip(activeLayer_);
      ImGui::TextColored(
          ImVec4(0.63f, 0.67f, 0.72f, 1.0f), "State: %s   Clip: %s   t: %.2f%s",
          state.empty() ? "-" : state.c_str(), clip.empty() ? "-" : clip.c_str(),
          static_cast<double>(previewInstance_.normalized_time(activeLayer_)),
          previewInstance_.is_transitioning(activeLayer_) ? "   (blending)" : "");
    }

    // Authoring a graph and pointing an entity at it are two halves of one
    // job, and the second half used to live in another panel entirely.
    if (context.editor.state.selectedEntity.has_value())
    {
      const Entity::EntityId selected = *context.editor.state.selectedEntity;
      const bool alreadyAssigned =
          context.componentManager.hasComponent<AnimatorComponent>(selected) &&
          context.componentManager.getComponent<AnimatorComponent>(selected).graphPath == graphName_;

      ImGui::BeginDisabled(alreadyAssigned);
      const std::string label =
          std::string(ICON_FA_LINK "  Assign to ") + entity_label(selected, context.componentManager);
      if (ImGui::Button(label.c_str()))
      {
        assign_graph_to_entity(context, selected);
      }
      ImGui::EndDisabled();
      if (alreadyAssigned)
      {
        ImGui::SameLine();
        ImGui::TextDisabled("already runs this animator");
      }
    }
  }

  void AnimatorGraphPlugin::draw_panel(EditorPluginContext &context)
  {
    if (context.workspacePath.empty())
    {
      ImGui::TextDisabled("Open a workspace to author animator graphs.");
      return;
    }

    sync_asset_root(context);
    poll_asset_lists(context);
    clamp_selection();
    // Before capture_debug_state, so the highlight the canvas draws is this
    // frame's pose rather than the previous one's.
    update_preview(context);
    refresh_imported_clip_list(context);
    capture_debug_state(context);

    // Undo is window-scoped: the canvas has no keyboard focus of its own, so
    // the whole panel (children included) accepts the shortcut. `WantTextInput`
    // is the gate an active InputText raises: without it Ctrl+Z inside a name
    // field undoes the keystroke *and* rolls the whole graph back a step.
    const ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput &&
        (io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_Z, false))
    {
      // Shift has to be read here rather than left to fall through: without
      // it the redo chord every other panel honours would land on undo and
      // throw away a further step of the user's work.
      if (io.KeyShift)
      {
        redo();
      }
      else
      {
        undo();
      }
    }

    draw_toolbar(context);
    draw_preview_bar(context);

    if (!errorMessage_.empty())
    {
      ImGui::TextColored(
          ImVec4(0.93f, 0.45f, 0.42f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", errorMessage_.c_str());
    }
    else if (!statusMessage_.empty())
    {
      ImGui::TextDisabled("%s", statusMessage_.c_str());
    }

    if (!validationProblems_.empty())
    {
      if (ImGui::BeginChild("##animator_problems", ImVec2(0.0f, 74.0f), ImGuiChildFlags_Borders))
      {
        for (const auto &problem : validationProblems_)
        {
          ImGui::TextColored(
              ImVec4(0.93f, 0.66f, 0.35f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", problem.c_str());
        }
      }
      ImGui::EndChild();
    }

    ImGui::Separator();

    if (!graphLoaded_)
    {
      ImGui::TextDisabled("Select an animator above, or create a new one.");
      return;
    }

    const float railWidth = 250.0f;
    const float detailsWidth = 330.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    if (ImGui::BeginChild("##animator_rail", ImVec2(railWidth, 0.0f), ImGuiChildFlags_Borders))
    {
      draw_parameters(context);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    const float canvasWidth = -(detailsWidth + spacing);
    if (ImGui::BeginChild(
            "##animator_canvas_host",
            ImVec2(canvasWidth, 0.0f),
            ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove))
    {
      draw_canvas(context);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    if (ImGui::BeginChild("##animator_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
    {
      draw_details(context);
    }
    ImGui::EndChild();
  }

  // ---------------------------------------------------------------------------
  // Toolbar
  // ---------------------------------------------------------------------------

  void AnimatorGraphPlugin::draw_toolbar(EditorPluginContext &context)
  {
    ImGui::SetNextItemWidth(220.0f);
    const char *preview = graphLoaded_ ? graphName_.c_str() : "(no animator)";
    if (ImGui::BeginCombo("##animator_graph", preview))
    {
      for (const auto &name : graphList_)
      {
        const bool selected = graphLoaded_ && name == graphName_;
        if (ImGui::Selectable(name.c_str(), selected))
        {
          load_graph(context, name);
        }
        if (selected)
        {
          ImGui::SetItemDefaultFocus();
        }
      }

      if (graphList_.empty())
      {
        ImGui::TextDisabled("no animators in .hades/animators/");
      }
      ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FILE_CIRCLE_PLUS "  New"))
    {
      newGraphNameBuffer_.fill('\0');
      openNewGraphPopup_ = true;
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!graphLoaded_);
    if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save"))
    {
      save_graph(context);
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TRASH "  Delete"))
    {
      openDeleteGraphPopup_ = true;
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CIRCLE_CHECK "  Validate"))
    {
      if (graph_.validate(validationProblems_))
      {
        statusMessage_ = "Graph is valid.";
        errorMessage_.clear();
      }
      else
      {
        statusMessage_ = std::to_string(validationProblems_.size()) + " problem(s) found.";
      }
    }
    ImGui::EndDisabled();

    if (graphLoaded_ && graphDirty_)
    {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1.0f), ICON_FA_PEN "  unsaved");
    }

    if (graphLoaded_)
    {
      // ---- Layer row -------------------------------------------------------
      AnimLayer *layer = active_layer();

      ImGui::SetNextItemWidth(180.0f);
      const char *layerPreview = layer != nullptr ? layer->name.c_str() : "(no layers)";
      if (ImGui::BeginCombo(ICON_FA_LAYER_GROUP "##animator_layer", layerPreview))
      {
        for (int i = 0; i < static_cast<int>(graph_.layers.size()); ++i)
        {
          ImGui::PushID(i);
          const bool selected = (i == activeLayer_);
          if (ImGui::Selectable(graph_.layers[static_cast<std::size_t>(i)].name.c_str(), selected))
          {
            activeLayer_ = i;
            selectedState_ = -1;
            selectedTransition_ = -1;
            pendingTransitionFrom_ = -1;
            maskBufferLayer_ = -1;
          }
          if (selected)
          {
            ImGui::SetItemDefaultFocus();
          }
          ImGui::PopID();
        }
        ImGui::EndCombo();
      }

      ImGui::SameLine();
      if (ImGui::Button(ICON_FA_PLUS "##animator_add_layer"))
      {
        push_undo("Add layer");
        AnimLayer added;
        added.name = "Layer " + std::to_string(graph_.layers.size());
        added.weight = 1.0f;
        graph_.layers.push_back(std::move(added));
        graph_.ensure_default_layer();
        activeLayer_ = static_cast<int>(graph_.layers.size()) - 1;
        selectedState_ = -1;
        selectedTransition_ = -1;
        maskBufferLayer_ = -1;
        graphDirty_ = true;
      }
      if (ImGui::IsItemHovered())
      {
        ImGui::SetTooltip("Add layer");
      }

      ImGui::SameLine();
      ImGui::BeginDisabled(graph_.layers.size() <= 1);
      if (ImGui::Button(ICON_FA_XMARK "##animator_remove_layer"))
      {
        push_undo("Remove layer");
        graph_.layers.erase(graph_.layers.begin() + activeLayer_);
        activeLayer_ = std::max(0, activeLayer_ - 1);
        selectedState_ = -1;
        selectedTransition_ = -1;
        pendingTransitionFrom_ = -1;
        maskBufferLayer_ = -1;
        graphDirty_ = true;
      }
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered())
      {
        ImGui::SetTooltip("Remove layer");
      }

      layer = active_layer();
      if (layer != nullptr)
      {
        ImGui::SameLine();
        std::array<char, 96> layerNameBuffer{};
        set_buffer_text(layerNameBuffer, layer->name);
        ImGui::SetNextItemWidth(130.0f);
        if (ImGui::InputText("##animator_layer_name", layerNameBuffer.data(), layerNameBuffer.size()))
        {
          layer->name = layerNameBuffer.data();
          graphDirty_ = true;
        }
        if (ImGui::IsItemActivated())
        {
          push_undo("Rename layer");
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragFloat("Weight", &layer->weight, 0.01f, 0.0f, 1.0f, "%.2f"))
        {
          graphDirty_ = true;
        }
        if (ImGui::IsItemActivated())
        {
          push_undo("Layer weight");
        }

        ImGui::SameLine();
        // Checkboxes edit a copy so the undo snapshot is taken before the flip.
        bool additive = layer->additive;
        if (ImGui::Checkbox("Additive", &additive))
        {
          push_undo("Layer additive");
          layer->additive = additive;
          graphDirty_ = true;
        }

        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_RULER_HORIZONTAL "  Mask"))
        {
          maskBufferLayer_ = -1;
          ImGui::OpenPopup("##animator_layer_mask");
        }

        if (ImGui::BeginPopup("##animator_layer_mask"))
        {
          if (maskBufferLayer_ != activeLayer_)
          {
            set_buffer_text(maskBonesBuffer_, join_lines(layer->maskBones));
            maskBufferLayer_ = activeLayer_;
          }

          ImGui::TextDisabled("One joint name per line. Empty means the whole skeleton.");
          if (ImGui::InputTextMultiline(
                  "##animator_mask_text",
                  maskBonesBuffer_.data(),
                  maskBonesBuffer_.size(),
                  ImVec2(300.0f, 180.0f)))
          {
            layer->maskBones = split_lines(maskBonesBuffer_.data());
            graphDirty_ = true;
          }
          if (ImGui::IsItemActivated())
          {
            push_undo("Edit bone mask");
          }

          bool descendants = layer->maskIncludesDescendants;
          if (ImGui::Checkbox("Include descendants", &descendants))
          {
            push_undo("Mask descendants");
            layer->maskIncludesDescendants = descendants;
            graphDirty_ = true;
          }
          ImGui::EndPopup();
        }
      }
    }

    // ---- Modals ------------------------------------------------------------
    if (openNewGraphPopup_)
    {
      ImGui::OpenPopup("New Animator##animator_new");
      openNewGraphPopup_ = false;
    }

    if (ImGui::BeginPopupModal("New Animator##animator_new", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::TextUnformatted("Name (stored in .hades/animators/)");
      ImGui::SetNextItemWidth(300.0f);
      const bool submitted = ImGui::InputText(
          "##animator_new_name",
          newGraphNameBuffer_.data(),
          newGraphNameBuffer_.size(),
          ImGuiInputTextFlags_EnterReturnsTrue);

      const std::string requested = newGraphNameBuffer_.data();
      const bool nameValid = !requested.empty() &&
                             requested.find('/') == std::string::npos &&
                             requested.find('\\') == std::string::npos;
      if (!requested.empty() && !nameValid)
      {
        ImGui::TextColored(ImVec4(0.93f, 0.45f, 0.42f, 1.0f), "Name cannot contain path separators.");
      }

      ImGui::BeginDisabled(!nameValid);
      const bool confirmed = ImGui::Button("Create") || (submitted && nameValid);
      ImGui::EndDisabled();

      if (confirmed)
      {
        new_graph(context, requested);
        ImGui::CloseCurrentPopup();
      }

      ImGui::SameLine();
      if (ImGui::Button("Cancel"))
      {
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    if (openDeleteGraphPopup_)
    {
      ImGui::OpenPopup("Delete Animator##animator_delete");
      openDeleteGraphPopup_ = false;
    }

    if (ImGui::BeginPopupModal("Delete Animator##animator_delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::Text("Delete animator '%s'? This cannot be undone.", graphName_.c_str());
      if (ImGui::Button("Delete"))
      {
        std::string error;
        if (AnimationClipCache::instance().deleteGraph(graphName_, &error))
        {
          statusMessage_ = "Deleted '" + graphName_ + "'.";
          errorMessage_.clear();
          graph_ = AnimatorGraph{};
          graphName_.clear();
          graphLoaded_ = false;
          graphDirty_ = false;
          selectedState_ = -1;
          selectedTransition_ = -1;
          pendingTransitionFrom_ = -1;
          reset_history();
          validationProblems_.clear();
        }
        else
        {
          errorMessage_ = error;
        }

        refresh_graph_list(context);
        ImGui::CloseCurrentPopup();
      }

      ImGui::SameLine();
      if (ImGui::Button("Cancel"))
      {
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }

  // ---------------------------------------------------------------------------
  // Left rail: layers and parameters
  // ---------------------------------------------------------------------------

  void AnimatorGraphPlugin::draw_parameters(EditorPluginContext &context)
  {
    ImGui::SeparatorText(ICON_FA_LAYER_GROUP "  Layers");

    for (int i = 0; i < static_cast<int>(graph_.layers.size()); ++i)
    {
      const AnimLayer &layer = graph_.layers[static_cast<std::size_t>(i)];
      ImGui::PushID(i);
      char label[192];
      std::snprintf(
          label,
          sizeof(label),
          "%s  %s  (%.2f%s)",
          layer.additive ? ICON_FA_PLUS : ICON_FA_LAYER_GROUP,
          layer.name.c_str(),
          static_cast<double>(layer.weight),
          layer.maskBones.empty() ? "" : ", masked");
      if (ImGui::Selectable(label, i == activeLayer_))
      {
        activeLayer_ = i;
        selectedState_ = -1;
        selectedTransition_ = -1;
        pendingTransitionFrom_ = -1;
        maskBufferLayer_ = -1;
      }
      ImGui::PopID();
    }

    ImGui::SeparatorText(ICON_FA_SLIDERS "  Parameters");

    // The same rail drives a running game's animator in play mode and the
    // panel's own preview in edit mode: poking a parameter and watching the
    // character blend is the point either way.
    AnimatorInstance *instance = active_instance(context);

    if (instance != nullptr)
    {
      ImGui::TextColored(
          ImVec4(0.47f, 0.78f, 0.55f, 1.0f), ICON_FA_PLAY "  %s",
          context.editor.state.isPlaying ? "live values" : "preview values");
    }

    int removeParameter = -1;

    for (int i = 0; i < static_cast<int>(graph_.parameters.size()); ++i)
    {
      AnimParameter &parameter = graph_.parameters[static_cast<std::size_t>(i)];
      if (instance != nullptr)
      {
        ++liveParameterRows_;
      }
      ImGui::PushID(i);

      ImGui::TextUnformatted(param_type_icon(parameter.type));
      if (ImGui::IsItemHovered())
      {
        ImGui::SetTooltip("%s", anim_param_type_name(parameter.type));
      }

      ImGui::SameLine();
      std::array<char, 128> nameBuffer{};
      set_buffer_text(nameBuffer, parameter.name);
      ImGui::SetNextItemWidth(110.0f);
      if (ImGui::InputText("##name", nameBuffer.data(), nameBuffer.size()))
      {
        // An empty buffer is not committed. The rename runs per keystroke, so
        // backspacing a name to nothing would first orphan every reference and
        // then, on the next keystroke, re-bind every *unrelated* empty
        // reference to the new name. Holding the old name until a character
        // arrives keeps the rename a single continuous rebind.
        if (nameBuffer[0] != '\0')
        {
          rename_parameter(i, nameBuffer.data());
        }
      }
      if (ImGui::IsItemActivated())
      {
        push_undo("Rename parameter");
      }

      ImGui::SameLine();
      if (ImGui::SmallButton(ICON_FA_XMARK))
      {
        removeParameter = i;
      }
      if (ImGui::IsItemHovered())
      {
        ImGui::SetTooltip("Delete parameter");
      }

      // Value editor. During play this pokes the live instance, which is how
      // a graph gets tested without wiring gameplay code first.
      ImGui::SetNextItemWidth(-1.0f);
      switch (parameter.type)
      {
      case AnimParamType::Float:
      {
        float value = instance != nullptr ? instance->get_float(parameter.name) : parameter.floatValue;
        if (ImGui::DragFloat("##value", &value, 0.01f, 0.0f, 0.0f, "%.3f"))
        {
          if (instance != nullptr)
          {
            instance->set_float(parameter.name, value);
          }
          else
          {
            parameter.floatValue = value;
            graphDirty_ = true;
          }
        }
        if (ImGui::IsItemActivated() && instance == nullptr)
        {
          push_undo("Parameter value");
        }
        break;
      }
      case AnimParamType::Int:
      {
        int value = instance != nullptr ? instance->get_int(parameter.name) : parameter.intValue;
        if (ImGui::DragInt("##value", &value, 0.2f))
        {
          if (instance != nullptr)
          {
            instance->set_int(parameter.name, value);
          }
          else
          {
            parameter.intValue = value;
            graphDirty_ = true;
          }
        }
        if (ImGui::IsItemActivated() && instance == nullptr)
        {
          push_undo("Parameter value");
        }
        break;
      }
      case AnimParamType::Bool:
      {
        bool value = instance != nullptr ? instance->get_bool(parameter.name) : parameter.boolValue;
        if (ImGui::Checkbox("##value", &value))
        {
          if (instance != nullptr)
          {
            instance->set_bool(parameter.name, value);
          }
          else
          {
            push_undo("Parameter value");
            parameter.boolValue = value;
            graphDirty_ = true;
          }
        }
        break;
      }
      case AnimParamType::Trigger:
      {
        ImGui::BeginDisabled(instance == nullptr);
        if (ImGui::Button(ICON_FA_BOLT "  Fire", ImVec2(-1.0f, 0.0f)) && instance != nullptr)
        {
          instance->set_trigger(parameter.name);
          statusMessage_ = "Fired trigger '" + parameter.name + "'.";
        }
        ImGui::EndDisabled();
        if (instance != nullptr && instance->get_bool(parameter.name))
        {
          ImGui::SameLine();
          ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1.0f), "set");
        }
        break;
      }
      }

      ImGui::PopID();
      ImGui::Separator();
    }

    if (removeParameter >= 0 && removeParameter < static_cast<int>(graph_.parameters.size()))
    {
      push_undo("Delete parameter");
      graph_.parameters.erase(graph_.parameters.begin() + removeParameter);
      graphDirty_ = true;
    }

    // ---- Add row -----------------------------------------------------------
    ImGui::SetNextItemWidth(110.0f);
    const bool submitted = ImGui::InputTextWithHint(
        "##new_parameter",
        "name",
        newParameterNameBuffer_.data(),
        newParameterNameBuffer_.size(),
        ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    const AnimParamType newType = static_cast<AnimParamType>(std::clamp(newParameterType_, 0, 3));
    if (ImGui::BeginCombo("##new_parameter_type", anim_param_type_name(newType)))
    {
      for (int i = 0; i < 4; ++i)
      {
        const bool selected = (i == newParameterType_);
        if (ImGui::Selectable(anim_param_type_name(static_cast<AnimParamType>(i)), selected))
        {
          newParameterType_ = i;
        }
        if (selected)
        {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    ImGui::SameLine();
    const std::string requested = newParameterNameBuffer_.data();
    ImGui::BeginDisabled(requested.empty());
    const bool add = ImGui::Button(ICON_FA_PLUS "##add_parameter");
    ImGui::EndDisabled();

    if ((add || submitted) && !requested.empty())
    {
      if (graph_.find_parameter(requested) != nullptr)
      {
        errorMessage_ = "A parameter named '" + requested + "' already exists.";
      }
      else
      {
        push_undo("Add parameter");
        AnimParameter parameter;
        parameter.name = requested;
        parameter.type = static_cast<AnimParamType>(std::clamp(newParameterType_, 0, 3));
        graph_.parameters.push_back(std::move(parameter));
        graphDirty_ = true;
        errorMessage_.clear();
        newParameterNameBuffer_.fill('\0');
      }
    }
  }

  // ---------------------------------------------------------------------------
  // Canvas
  // ---------------------------------------------------------------------------

  void AnimatorGraphPlugin::draw_canvas(EditorPluginContext &context)
  {
    AnimLayer *layer = active_layer();
    if (layer == nullptr)
    {
      ImGui::TextDisabled("This graph has no layers.");
      return;
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 64.0f);
    size.y = std::max(size.y, 64.0f);
    const ImVec2 canvasMax(origin.x + size.x, origin.y + size.y);

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImGuiIO &io = ImGui::GetIO();

    drawList->PushClipRect(origin, canvasMax, true);
    drawList->AddRectFilled(origin, canvasMax, kCanvasBackground);

    // Grid, aligned to the panned/zoomed graph space so it reads as ground.
    const float step = kGridStep * zoom_;
    if (step > 4.0f)
    {
      const float startX = std::fmod(panX_, step);
      for (float x = startX; x < size.x; x += step)
      {
        const bool bold = std::fabs(std::fmod((x - panX_) / step, 4.0f)) < 0.01f;
        drawList->AddLine(
            ImVec2(origin.x + x, origin.y),
            ImVec2(origin.x + x, canvasMax.y),
            bold ? kGridLineBold : kGridLine);
      }

      const float startY = std::fmod(panY_, step);
      for (float y = startY; y < size.y; y += step)
      {
        const bool bold = std::fabs(std::fmod((y - panY_) / step, 4.0f)) < 0.01f;
        drawList->AddLine(
            ImVec2(origin.x, origin.y + y),
            ImVec2(canvasMax.x, origin.y + y),
            bold ? kGridLineBold : kGridLine);
      }
    }

    // Node rectangles first: transitions and hit tests both need them, and the
    // vector is rebuilt every frame so a deleted state cannot linger.
    nodeRects_.clear();
    nodeRects_.reserve(layer->states.size());
    for (const auto &state : layer->states)
    {
      NodeRect rect;
      rect.minX = origin.x + panX_ + state.x * zoom_;
      rect.minY = origin.y + panY_ + state.y * zoom_;
      rect.maxX = rect.minX + kNodeWidth * zoom_;
      rect.maxY = rect.minY + kNodeHeight * zoom_;
      nodeRects_.push_back(rect);
    }

    // Background hit target. It covers the whole canvas and is submitted
    // before the nodes, so without AllowOverlap ImGui hands it every hover
    // and the state nodes are dead: they cannot be clicked, dragged or
    // right-clicked, and nothing on the canvas can ever be selected. Being
    // submitted first is not enough on its own -- that is what decides the
    // *loser* of an overlap, not the winner.
    ImGui::SetNextItemAllowOverlap();
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton(
        "##animator_canvas_input",
        size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
            ImGuiButtonFlags_MouseButtonMiddle);
    const bool canvasHovered = ImGui::IsItemHovered();
    const bool canvasActive = ImGui::IsItemActive();

    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
      rightDragPanned_ = false;
    }

    if (canvasActive &&
        (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)))
    {
      panX_ += io.MouseDelta.x;
      panY_ += io.MouseDelta.y;
      if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
      {
        rightDragPanned_ = true;
      }
    }

    if (canvasHovered && io.KeyCtrl && io.MouseWheel != 0.0f)
    {
      // Zoom about the cursor so the graph does not slide out from under it.
      const float anchorX = (io.MousePos.x - origin.x - panX_) / zoom_;
      const float anchorY = (io.MousePos.y - origin.y - panY_) / zoom_;
      zoom_ = std::clamp(zoom_ * (1.0f + io.MouseWheel * 0.12f), kMinZoom, kMaxZoom);
      panX_ = io.MousePos.x - origin.x - anchorX * zoom_;
      panY_ = io.MousePos.y - origin.y - anchorY * zoom_;
    }

    bool mouseOverNode = false;
    for (const auto &rect : nodeRects_)
    {
      if (io.MousePos.x >= rect.minX && io.MousePos.x <= rect.maxX &&
          io.MousePos.y >= rect.minY && io.MousePos.y <= rect.maxY)
      {
        mouseOverNode = true;
        break;
      }
    }

    draw_transitions(origin);

    if (canvasHovered && !mouseOverNode && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
      if (pendingTransitionFrom_ >= 0)
      {
        pendingTransitionFrom_ = -1;
        statusMessage_ = "Transition cancelled.";
      }
      else if (hoveredTransition_ >= 0)
      {
        selectedTransition_ = hoveredTransition_;
        selectedState_ = -1;
      }
      else
      {
        selectedState_ = -1;
        selectedTransition_ = -1;
      }
    }

    for (int i = 0; i < static_cast<int>(layer->states.size()); ++i)
    {
      draw_state_node(i, origin);
    }

    // Rubber band for a transition that is waiting for its destination.
    if (pendingTransitionFrom_ >= 0 && pendingTransitionFrom_ < static_cast<int>(nodeRects_.size()))
    {
      const NodeRect &rect = nodeRects_[static_cast<std::size_t>(pendingTransitionFrom_)];
      const ImVec2 centre = rect_centre(rect.minX, rect.minY, rect.maxX, rect.maxY);
      const ImVec2 start = rect_border_point(
          centre, (rect.maxX - rect.minX) * 0.5f, (rect.maxY - rect.minY) * 0.5f, io.MousePos);
      drawList->AddLine(start, io.MousePos, kTransitionSelected, 2.0f * zoom_);
      draw_arrow_head(
          drawList,
          io.MousePos,
          ImVec2(io.MousePos.x - start.x, io.MousePos.y - start.y),
          11.0f * zoom_,
          kTransitionSelected);

      if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
      {
        pendingTransitionFrom_ = -1;
        statusMessage_ = "Transition cancelled.";
      }
    }

    drawList->PopClipRect();

    // ---- Empty-canvas context menu ----------------------------------------
    if (canvasHovered && !mouseOverNode && !rightDragPanned_ &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
      if (pendingTransitionFrom_ >= 0)
      {
        // A right-click while wiring means "never mind", not "open a menu".
        pendingTransitionFrom_ = -1;
        statusMessage_ = "Transition cancelled.";
      }
      else
      {
        pendingAddX_ = (io.MousePos.x - origin.x - panX_) / zoom_;
        pendingAddY_ = (io.MousePos.y - origin.y - panY_) / zoom_;
        ImGui::OpenPopup("##animator_canvas_menu");
      }
    }

    if (ImGui::BeginPopup("##animator_canvas_menu"))
    {
      if (ImGui::MenuItem(ICON_FA_PLUS "  Add state"))
      {
        pendingAddState_ = true;
        pendingAddKind_ = AnimStateKind::Clip;
      }
      if (ImGui::MenuItem(ICON_FA_PLUS "  Add blend tree 1D"))
      {
        pendingAddState_ = true;
        pendingAddKind_ = AnimStateKind::BlendTree1D;
      }
      if (ImGui::MenuItem(ICON_FA_PLUS "  Add blend tree 2D"))
      {
        pendingAddState_ = true;
        pendingAddKind_ = AnimStateKind::BlendTree2D;
      }
      ImGui::Separator();
      if (ImGui::MenuItem(ICON_FA_PASTE "  Paste", nullptr, false, clipboardValid_))
      {
        pendingPaste_ = true;
      }
      ImGui::EndPopup();
    }

    apply_pending_canvas_actions();

    (void)context;
  }

  void AnimatorGraphPlugin::draw_state_node(int stateIndex, const ImVec2 &canvasOrigin)
  {
    AnimLayer *layer = active_layer();
    if (layer == nullptr || stateIndex < 0 || stateIndex >= static_cast<int>(layer->states.size()) ||
        stateIndex >= static_cast<int>(nodeRects_.size()))
    {
      return;
    }

    (void)canvasOrigin; // Node rectangles were already resolved for this frame.

    AnimState &state = layer->states[static_cast<std::size_t>(stateIndex)];
    const NodeRect &rect = nodeRects_[static_cast<std::size_t>(stateIndex)];
    const ImVec2 min(rect.minX, rect.minY);
    const ImVec2 max(rect.maxX, rect.maxY);
    const ImVec2 nodeSize(max.x - min.x, max.y - min.y);

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImGuiIO &io = ImGui::GetIO();

    ImGui::SetCursorScreenPos(min);
    ImGui::PushID(stateIndex);
    ImGui::InvisibleButton(
        "##animator_state", nodeSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
      if (draggingState_ != stateIndex)
      {
        push_undo("Move state");
        draggingState_ = stateIndex;
      }
      state.x += io.MouseDelta.x / zoom_;
      state.y += io.MouseDelta.y / zoom_;
      graphDirty_ = true;
    }

    if (ImGui::IsItemDeactivated() && draggingState_ == stateIndex)
    {
      draggingState_ = -1;
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && draggingState_ != stateIndex)
    {
      if (pendingTransitionFrom_ >= 0)
      {
        complete_transition(stateIndex);
      }
      else
      {
        selectedState_ = stateIndex;
        selectedTransition_ = -1;
      }
    }

    if (ImGui::BeginPopupContextItem("##animator_state_menu"))
    {
      selectedState_ = stateIndex;
      selectedTransition_ = -1;

      if (ImGui::MenuItem(ICON_FA_STAR "  Set as default"))
      {
        pendingDefaultState_ = stateIndex;
      }
      if (ImGui::MenuItem(ICON_FA_ARROW_RIGHT_LONG "  Make transition"))
      {
        begin_transition(stateIndex);
      }
      if (ImGui::MenuItem(ICON_FA_COPY "  Copy"))
      {
        clipboardState_ = state;
        clipboardValid_ = true;
        statusMessage_ = "Copied state '" + state.name + "'.";
      }
      if (ImGui::MenuItem(ICON_FA_CLONE "  Duplicate"))
      {
        pendingDuplicateState_ = stateIndex;
      }
      ImGui::Separator();
      if (ImGui::MenuItem(ICON_FA_TRASH "  Delete"))
      {
        pendingDeleteState_ = stateIndex;
      }
      ImGui::EndPopup();
    }
    ImGui::PopID();

    const bool isDefault = (layer->defaultState == stateIndex);
    const bool isSelected = (selectedState_ == stateIndex);
    const bool isActiveState = debugAvailable_ && !debugStateName_.empty() && debugStateName_ == state.name;

    const float rounding = 6.0f * zoom_;
    drawList->AddRectFilled(min, max, isDefault ? kNodeFillDefault : kNodeFill, rounding);

    if (isActiveState)
    {
      // Progress bar across the node body: normalised time of the live source.
      const float fraction = std::clamp(debugNormalizedTime_, 0.0f, 1.0f);
      if (fraction > 0.0f)
      {
        drawList->AddRectFilled(
            ImVec2(min.x, max.y - 5.0f * zoom_),
            ImVec2(min.x + nodeSize.x * fraction, max.y),
            kProgressFill,
            rounding,
            ImDrawFlags_RoundCornersBottom);
      }
    }

    ImU32 border = kNodeBorder;
    float thickness = 1.5f * zoom_;
    if (isActiveState)
    {
      border = kActiveBorder;
      thickness = 3.0f * zoom_;
    }
    else if (isSelected)
    {
      border = kNodeBorderSelected;
      thickness = 2.5f * zoom_;
    }
    else if (hovered)
    {
      border = kNodeBorderHover;
    }
    drawList->AddRect(min, max, border, rounding, ImDrawFlags_None, thickness);

    if (isDefault)
    {
      // Entry marker: a stub arrow running into the node's left border.
      const float centreY = (min.y + max.y) * 0.5f;
      const ImVec2 tail(min.x - 26.0f * zoom_, centreY);
      const ImVec2 tip(min.x - 2.0f * zoom_, centreY);
      drawList->AddLine(tail, tip, kDefaultAccent, 2.0f * zoom_);
      draw_arrow_head(drawList, tip, ImVec2(1.0f, 0.0f), 10.0f * zoom_, kDefaultAccent);
      draw_scaled_text(
          drawList,
          ImVec2(tail.x - 2.0f * zoom_, centreY - 20.0f * zoom_),
          kDefaultAccent,
          10.0f * zoom_,
          "entry",
          0.0f,
          ImVec2(tail.x - 40.0f * zoom_, min.y - 40.0f * zoom_),
          ImVec2(min.x, max.y));
    }

    const ImVec2 textClipMin(min.x + 4.0f * zoom_, min.y);
    const ImVec2 textClipMax(max.x - 4.0f * zoom_, max.y);
    draw_scaled_text(
        drawList,
        ImVec2(min.x + 9.0f * zoom_, min.y + 8.0f * zoom_),
        kTextPrimary,
        14.0f * zoom_,
        state.name,
        nodeSize.x - 16.0f * zoom_,
        textClipMin,
        textClipMax);
    draw_scaled_text(
        drawList,
        ImVec2(min.x + 9.0f * zoom_, min.y + 27.0f * zoom_),
        kTextSecondary,
        11.0f * zoom_,
        node_subtitle(state),
        nodeSize.x - 16.0f * zoom_,
        textClipMin,
        textClipMax);
  }

  void AnimatorGraphPlugin::draw_transitions(const ImVec2 &canvasOrigin)
  {
    hoveredTransition_ = -1;

    AnimLayer *layer = active_layer();
    if (layer == nullptr)
    {
      return;
    }

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const int stateCount = static_cast<int>(nodeRects_.size());

    for (int i = 0; i < static_cast<int>(layer->transitions.size()); ++i)
    {
      const AnimTransition &transition = layer->transitions[static_cast<std::size_t>(i)];
      if (transition.toState < 0 || transition.toState >= stateCount)
      {
        continue;
      }

      const NodeRect &toRect = nodeRects_[static_cast<std::size_t>(transition.toState)];
      const ImVec2 toCentre = rect_centre(toRect.minX, toRect.minY, toRect.maxX, toRect.maxY);
      const float toHalfWidth = (toRect.maxX - toRect.minX) * 0.5f;
      const float toHalfHeight = (toRect.maxY - toRect.minY) * 0.5f;

      const bool selected = (selectedTransition_ == i);
      const bool running = debugAvailable_ && debugTransitioning_ && !debugStateName_.empty() &&
                           transition.toState < static_cast<int>(layer->states.size()) &&
                           layer->states[static_cast<std::size_t>(transition.toState)].name == debugStateName_;

      if (transition.fromState == AnimTransition::kAnyState)
      {
        // "Any state" transitions have no source node: draw a short stub into
        // the destination so they are still visible and selectable.
        const ImVec2 tip(toRect.minX - 3.0f * zoom_, toRect.minY + 14.0f * zoom_);
        const ImVec2 tail(tip.x - 34.0f * zoom_, tip.y - 18.0f * zoom_);
        const ImU32 colour = selected ? kTransitionSelected : (running ? kTransitionRunning : kAnyStateColour);
        drawList->AddLine(tail, tip, colour, 2.0f * zoom_);
        draw_arrow_head(drawList, tip, ImVec2(tip.x - tail.x, tip.y - tail.y), 10.0f * zoom_, colour);

        const float distance = distance_to_bezier(tail, tail, tip, tip, mouse);
        if (distance <= kTransitionHitRadius)
        {
          hoveredTransition_ = i;
        }
        continue;
      }

      if (transition.fromState < 0 || transition.fromState >= stateCount)
      {
        continue;
      }

      const NodeRect &fromRect = nodeRects_[static_cast<std::size_t>(transition.fromState)];
      const ImVec2 fromCentre = rect_centre(fromRect.minX, fromRect.minY, fromRect.maxX, fromRect.maxY);

      if (transition.fromState == transition.toState)
      {
        // Self transition: a loop above the node.
        const float loop = 26.0f * zoom_;
        const ImVec2 start(fromCentre.x - loop * 0.6f, fromRect.minY);
        const ImVec2 end(fromCentre.x + loop * 0.6f, fromRect.minY);
        const ImVec2 controlA(start.x - loop, start.y - loop * 1.6f);
        const ImVec2 controlB(end.x + loop, end.y - loop * 1.6f);
        const ImU32 colour = selected ? kTransitionSelected : (running ? kTransitionRunning : kTransitionColour);
        drawList->AddBezierCubic(start, controlA, controlB, end, colour, (selected ? 3.0f : 2.0f) * zoom_);
        draw_arrow_head(
            drawList, end, ImVec2(end.x - controlB.x, end.y - controlB.y), 10.0f * zoom_, colour);

        if (distance_to_bezier(start, controlA, controlB, end, mouse) <= kTransitionHitRadius)
        {
          hoveredTransition_ = i;
        }
        continue;
      }

      // Offset both endpoints along the normal so A->B and B->A stay apart.
      const float deltaX = toCentre.x - fromCentre.x;
      const float deltaY = toCentre.y - fromCentre.y;
      const float length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
      const ImVec2 unit = length > 0.0001f ? ImVec2(deltaX / length, deltaY / length) : ImVec2(1.0f, 0.0f);
      const ImVec2 normal(-unit.y, unit.x);
      const float offset = kTransitionOffset * zoom_;

      ImVec2 start = rect_border_point(
          fromCentre, (fromRect.maxX - fromRect.minX) * 0.5f, (fromRect.maxY - fromRect.minY) * 0.5f, toCentre);
      ImVec2 end = rect_border_point(toCentre, toHalfWidth, toHalfHeight, fromCentre);
      start = ImVec2(start.x + normal.x * offset, start.y + normal.y * offset);
      end = ImVec2(end.x + normal.x * offset, end.y + normal.y * offset);

      const float reach = std::clamp(length * 0.35f, 24.0f * zoom_, 140.0f * zoom_);
      const float bend = 14.0f * zoom_;
      const ImVec2 controlA(
          start.x + unit.x * reach + normal.x * bend, start.y + unit.y * reach + normal.y * bend);
      const ImVec2 controlB(
          end.x - unit.x * reach + normal.x * bend, end.y - unit.y * reach + normal.y * bend);

      const float distance = distance_to_bezier(start, controlA, controlB, end, mouse);
      const bool hovered = (distance <= kTransitionHitRadius);
      if (hovered)
      {
        hoveredTransition_ = i;
      }

      ImU32 colour = kTransitionColour;
      float thickness = 2.0f * zoom_;
      if (selected)
      {
        colour = kTransitionSelected;
        thickness = 3.0f * zoom_;
      }
      else if (running)
      {
        colour = kTransitionRunning;
        thickness = 3.0f * zoom_;
      }
      else if (hovered)
      {
        colour = kTransitionHover;
      }

      drawList->AddBezierCubic(start, controlA, controlB, end, colour, thickness);
      draw_arrow_head(
          drawList, end, ImVec2(end.x - controlB.x, end.y - controlB.y), 11.0f * zoom_, colour);

      if (!transition.conditions.empty())
      {
        const ImVec2 label = bezier_point(start, controlA, controlB, end, 0.5f);
        drawList->AddCircleFilled(label, 4.0f * zoom_, colour);
      }
    }

    (void)canvasOrigin;
  }

  void AnimatorGraphPlugin::apply_pending_canvas_actions()
  {
    AnimLayer *layer = active_layer();
    if (layer == nullptr)
    {
      pendingAddState_ = false;
      pendingPaste_ = false;
      pendingDeleteState_ = -1;
      pendingDuplicateState_ = -1;
      pendingDefaultState_ = -1;
      pendingDeleteTransition_ = -1;
      return;
    }

    if (pendingAddState_)
    {
      add_state_of_kind(pendingAddX_, pendingAddY_, pendingAddKind_);
      pendingAddState_ = false;
    }

    if (pendingPaste_)
    {
      pendingPaste_ = false;
      if (clipboardValid_)
      {
        push_undo("Paste state");
        AnimState pasted = clipboardState_;
        pasted.name = unique_state_name(*layer, clipboardState_.name);
        pasted.x = pendingAddX_;
        pasted.y = pendingAddY_;
        layer->states.push_back(std::move(pasted));
        selectedState_ = static_cast<int>(layer->states.size()) - 1;
        selectedTransition_ = -1;
        graphDirty_ = true;
      }
    }

    if (pendingDefaultState_ >= 0)
    {
      if (pendingDefaultState_ < static_cast<int>(layer->states.size()))
      {
        push_undo("Set default state");
        layer->defaultState = pendingDefaultState_;
        graphDirty_ = true;
      }
      pendingDefaultState_ = -1;
    }

    if (pendingDuplicateState_ >= 0)
    {
      if (pendingDuplicateState_ < static_cast<int>(layer->states.size()))
      {
        push_undo("Duplicate state");
        AnimState copy = layer->states[static_cast<std::size_t>(pendingDuplicateState_)];
        copy.name = unique_state_name(*layer, copy.name);
        copy.x += 32.0f;
        copy.y += 32.0f;
        layer->states.push_back(std::move(copy));
        selectedState_ = static_cast<int>(layer->states.size()) - 1;
        selectedTransition_ = -1;
        graphDirty_ = true;
      }
      pendingDuplicateState_ = -1;
    }

    if (pendingDeleteTransition_ >= 0)
    {
      delete_transition(pendingDeleteTransition_);
      pendingDeleteTransition_ = -1;
    }

    if (pendingDeleteState_ >= 0)
    {
      delete_state(pendingDeleteState_);
      pendingDeleteState_ = -1;
    }
  }

  // ---------------------------------------------------------------------------
  // Details pane
  // ---------------------------------------------------------------------------

  void AnimatorGraphPlugin::draw_details(EditorPluginContext &context)
  {
    AnimLayer *layer = active_layer();
    if (layer == nullptr)
    {
      ImGui::TextDisabled("No layer selected.");
      return;
    }

    if (selectedTransition_ >= 0 && selectedTransition_ < static_cast<int>(layer->transitions.size()))
    {
      draw_transition_details(layer->transitions[static_cast<std::size_t>(selectedTransition_)]);
      return;
    }

    if (selectedState_ >= 0 && selectedState_ < static_cast<int>(layer->states.size()))
    {
      draw_state_details(layer->states[static_cast<std::size_t>(selectedState_)], context);
      return;
    }

    ImGui::SeparatorText("Details");
    ImGui::TextWrapped(
        "Select a state or a transition on the canvas. Right-click empty canvas to add a state, "
        "right-click a node to make a transition.");

    ImGui::Spacing();
    ImGui::TextDisabled("Graph");
    std::array<char, 256> descriptionBuffer{};
    set_buffer_text(descriptionBuffer, graph_.description);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##graph_description", descriptionBuffer.data(), descriptionBuffer.size()))
    {
      graph_.description = descriptionBuffer.data();
      graphDirty_ = true;
    }
    if (ImGui::IsItemActivated())
    {
      push_undo("Edit description");
    }

    std::array<char, 512> modelBuffer{};
    set_buffer_text(modelBuffer, graph_.sourceModel);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint(
            "##graph_model", "source model (hint)", modelBuffer.data(), modelBuffer.size()))
    {
      graph_.sourceModel = modelBuffer.data();
      graphDirty_ = true;
    }
    if (ImGui::IsItemActivated())
    {
      push_undo("Edit source model");
    }
  }

  void AnimatorGraphPlugin::draw_state_details(AnimState &state, EditorPluginContext &context)
  {
    ImGui::SeparatorText(ICON_FA_CIRCLE_NODES "  State");

    std::array<char, 128> nameBuffer{};
    set_buffer_text(nameBuffer, state.name);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##state_name", "name", nameBuffer.data(), nameBuffer.size()))
    {
      state.name = nameBuffer.data();
      graphDirty_ = true;
    }
    if (ImGui::IsItemActivated())
    {
      push_undo("Rename state");
    }

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##state_kind", state_kind_label(state.kind)))
    {
      for (int i = 0; i < 3; ++i)
      {
        const AnimStateKind candidate = static_cast<AnimStateKind>(i);
        const bool selected = (candidate == state.kind);
        if (ImGui::Selectable(state_kind_label(candidate), selected))
        {
          if (candidate != state.kind)
          {
            push_undo("Change state kind");
            state.kind = candidate;
            graphDirty_ = true;
          }
        }
        if (selected)
        {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    if (state.kind == AnimStateKind::Clip)
    {
      // Combos are handed a copy: the undo snapshot has to predate the change.
      std::string clip = state.clip;
      ImGui::SetNextItemWidth(-40.0f);
      if (clip_combo("##state_clip", clip, clipList_, importedClipList_) && clip != state.clip)
      {
        push_undo("Set clip");
        state.clip = std::move(clip);
        graphDirty_ = true;
      }

      // The clip a state plays is authored in the other panel, and finding it
      // there used to mean remembering its name and hunting the Clips tab.
      ImGui::SameLine();
      ImGui::BeginDisabled(state.clip.empty());
      if (ImGui::Button(ICON_FA_FILM "##state_open_clip"))
      {
        if (auto *panel = context.editor.find_plugin("animation-editor"))
        {
          static_cast<AnimationEditorPlugin *>(panel)->reveal_clip(context, state.clip);
        }
      }
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
        ImGui::SetTooltip("Open this clip in the Animation editor");
      }
    }

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::DragFloat("##state_speed", &state.speed, 0.01f, -8.0f, 8.0f, "speed %.2f"))
    {
      graphDirty_ = true;
    }
    if (ImGui::IsItemActivated())
    {
      push_undo("State speed");
    }

    bool looping = state.looping;
    if (ImGui::Checkbox("Looping", &looping))
    {
      push_undo("State looping");
      state.looping = looping;
      graphDirty_ = true;
    }

    if (state.kind != AnimStateKind::Clip)
    {
      ImGui::SeparatorText("Blend");

      std::string blendX = state.blendParameterX;
      ImGui::SetNextItemWidth(-1.0f);
      if (parameter_combo("##blend_x", blendX, graph_.parameters, true) && blendX != state.blendParameterX)
      {
        push_undo("Blend parameter");
        state.blendParameterX = std::move(blendX);
        graphDirty_ = true;
      }

      if (state.kind == AnimStateKind::BlendTree2D)
      {
        std::string blendY = state.blendParameterY;
        ImGui::SetNextItemWidth(-1.0f);
        if (parameter_combo("##blend_y", blendY, graph_.parameters, true) && blendY != state.blendParameterY)
        {
          push_undo("Blend parameter");
          state.blendParameterY = std::move(blendY);
          graphDirty_ = true;
        }
      }

      const bool twoDimensional = (state.kind == AnimStateKind::BlendTree2D);
      const int columns = twoDimensional ? 5 : 4;
      int removeEntry = -1;

      if (ImGui::BeginTable("##blend_entries", columns, ImGuiTableFlags_SizingStretchProp))
      {
        ImGui::TableSetupColumn("clip");
        ImGui::TableSetupColumn("x");
        if (twoDimensional)
        {
          ImGui::TableSetupColumn("y");
        }
        ImGui::TableSetupColumn("speed");
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(state.entries.size()); ++i)
        {
          AnimBlendEntry &entry = state.entries[static_cast<std::size_t>(i)];
          ImGui::PushID(i);
          ImGui::TableNextRow();

          ImGui::TableNextColumn();
          ImGui::SetNextItemWidth(-1.0f);
          std::string entryClip = entry.clip;
          if (clip_combo("##entry_clip", entryClip, clipList_, importedClipList_) &&
              entryClip != entry.clip)
          {
            push_undo("Blend entry clip");
            entry.clip = std::move(entryClip);
            graphDirty_ = true;
          }

          ImGui::TableNextColumn();
          ImGui::SetNextItemWidth(-1.0f);
          if (ImGui::DragFloat("##entry_x", &entry.thresholdX, 0.01f, 0.0f, 0.0f, "%.2f"))
          {
            graphDirty_ = true;
          }
          if (ImGui::IsItemActivated())
          {
            push_undo("Blend threshold");
          }

          if (twoDimensional)
          {
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##entry_y", &entry.thresholdY, 0.01f, 0.0f, 0.0f, "%.2f"))
            {
              graphDirty_ = true;
            }
            if (ImGui::IsItemActivated())
            {
              push_undo("Blend threshold");
            }
          }

          ImGui::TableNextColumn();
          ImGui::SetNextItemWidth(-1.0f);
          if (ImGui::DragFloat("##entry_speed", &entry.speed, 0.01f, -8.0f, 8.0f, "%.2f"))
          {
            graphDirty_ = true;
          }
          if (ImGui::IsItemActivated())
          {
            push_undo("Blend entry speed");
          }

          ImGui::TableNextColumn();
          if (ImGui::SmallButton(ICON_FA_XMARK))
          {
            removeEntry = i;
          }

          ImGui::PopID();
        }

        // Add row: picking a clip appends the entry straight away.
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##add_entry", ICON_FA_PLUS "  add clip"))
        {
          for (const auto &clip : clipList_)
          {
            if (ImGui::Selectable(clip.c_str()))
            {
              push_undo("Add blend entry");
              AnimBlendEntry added;
              added.clip = clip;
              state.entries.push_back(std::move(added));
              graphDirty_ = true;
            }
          }
          if (clipList_.empty())
          {
            ImGui::TextDisabled("no clips in .hades/animations/");
          }
          ImGui::EndCombo();
        }
        ImGui::EndTable();
      }

      if (removeEntry >= 0 && removeEntry < static_cast<int>(state.entries.size()))
      {
        push_undo("Remove blend entry");
        state.entries.erase(state.entries.begin() + removeEntry);
        graphDirty_ = true;
      }
    }

    // ---- Play-mode helpers ---------------------------------------------------
    const Entity::EntityId entity = debug_entity(context);
    AnimatorInstance *instance = entity != Entity::INVALID ? AnimationRuntime::instance().find(entity) : nullptr;
    if (instance != nullptr)
    {
      ImGui::SeparatorText(ICON_FA_PLAY "  Debug");
      if (ImGui::Button(ICON_FA_PLAY "  Play this state", ImVec2(-1.0f, 0.0f)))
      {
        if (!instance->goto_state(state.name, 0.15f, activeLayer_))
        {
          errorMessage_ = "The running animator has no state named '" + state.name +
                          "'. Save the graph and restart play mode.";
        }
      }
      ImGui::Text("current: %s", instance->current_state(activeLayer_).c_str());
      ImGui::Text("time: %.3f", static_cast<double>(instance->normalized_time(activeLayer_)));
    }
  }

  void AnimatorGraphPlugin::draw_transition_details(AnimTransition &transition)
  {
    ImGui::SeparatorText(ICON_FA_ARROW_RIGHT_LONG "  Transition");

    const AnimLayer *layer = active_layer();
    const auto state_label = [layer](int index) -> std::string
    {
      if (index == AnimTransition::kAnyState)
      {
        return "Any State";
      }
      if (layer == nullptr || index < 0 || index >= static_cast<int>(layer->states.size()))
      {
        return "<missing>";
      }
      return layer->states[static_cast<std::size_t>(index)].name;
    };

    ImGui::Text("%s", state_label(transition.fromState).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(ICON_FA_ARROW_RIGHT);
    ImGui::SameLine();
    ImGui::Text("%s", state_label(transition.toState).c_str());

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::DragFloat("##duration", &transition.duration, 0.005f, 0.0f, 10.0f, "duration %.3f s"))
    {
      graphDirty_ = true;
    }
    if (ImGui::IsItemActivated())
    {
      push_undo("Transition duration");
    }

    bool hasExitTime = transition.hasExitTime;
    if (ImGui::Checkbox("Has exit time", &hasExitTime))
    {
      push_undo("Exit time");
      transition.hasExitTime = hasExitTime;
      graphDirty_ = true;
    }

    ImGui::BeginDisabled(!transition.hasExitTime);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::DragFloat("##exit_time", &transition.exitTime, 0.01f, 0.0f, 4.0f, "exit at %.2f"))
    {
      graphDirty_ = true;
    }
    if (ImGui::IsItemActivated())
    {
      push_undo("Exit time");
    }
    ImGui::EndDisabled();

    bool canInterrupt = transition.canInterrupt;
    if (ImGui::Checkbox("Can interrupt", &canInterrupt))
    {
      push_undo("Can interrupt");
      transition.canInterrupt = canInterrupt;
      graphDirty_ = true;
    }

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::DragInt("##priority", &transition.priority, 0.2f, -128, 128, "priority %d"))
    {
      graphDirty_ = true;
    }
    if (ImGui::IsItemActivated())
    {
      push_undo("Transition priority");
    }

    ImGui::SeparatorText("Conditions");

    int removeCondition = -1;
    for (int i = 0; i < static_cast<int>(transition.conditions.size()); ++i)
    {
      AnimCondition &condition = transition.conditions[static_cast<std::size_t>(i)];
      ImGui::PushID(i);

      const AnimParameter *parameter = graph_.find_parameter(condition.parameter);
      const AnimParamType type = parameter != nullptr ? parameter->type : AnimParamType::Float;

      ImGui::SetNextItemWidth(120.0f);
      std::string conditionParameter = condition.parameter;
      if (parameter_combo("##condition_parameter", conditionParameter, graph_.parameters, false) &&
          conditionParameter != condition.parameter)
      {
        push_undo("Condition parameter");
        condition.parameter = std::move(conditionParameter);

        // Keep the operator meaningful for the newly chosen parameter.
        const AnimParameter *updated = graph_.find_parameter(condition.parameter);
        const AnimParamType updatedType = updated != nullptr ? updated->type : AnimParamType::Float;
        if (!op_allowed_for(updatedType, condition.op))
        {
          condition.op = default_op_for(updatedType);
        }
        graphDirty_ = true;
      }

      ImGui::SameLine();
      ImGui::SetNextItemWidth(110.0f);
      AnimConditionOp conditionOp = condition.op;
      if (condition_op_combo("##condition_op", conditionOp, type) && conditionOp != condition.op)
      {
        push_undo("Condition operator");
        condition.op = conditionOp;
        graphDirty_ = true;
      }

      ImGui::SameLine();
      if (ImGui::SmallButton(ICON_FA_XMARK))
      {
        removeCondition = i;
      }

      if (op_allowed_for(AnimParamType::Float, condition.op))
      {
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragFloat("##condition_threshold", &condition.threshold, 0.01f, 0.0f, 0.0f, "%.3f"))
        {
          graphDirty_ = true;
        }
        if (ImGui::IsItemActivated())
        {
          push_undo("Condition threshold");
        }
      }

      if (parameter == nullptr && !condition.parameter.empty())
      {
        ImGui::TextColored(
            ImVec4(0.93f, 0.45f, 0.42f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  unknown parameter");
      }

      ImGui::PopID();
      ImGui::Separator();
    }

    if (removeCondition >= 0 && removeCondition < static_cast<int>(transition.conditions.size()))
    {
      push_undo("Remove condition");
      transition.conditions.erase(transition.conditions.begin() + removeCondition);
      graphDirty_ = true;
    }

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##add_condition", ICON_FA_PLUS "  add condition"))
    {
      for (const auto &parameter : graph_.parameters)
      {
        if (ImGui::Selectable(parameter.name.c_str()))
        {
          push_undo("Add condition");
          AnimCondition condition;
          condition.parameter = parameter.name;
          condition.op = default_op_for(parameter.type);
          transition.conditions.push_back(std::move(condition));
          graphDirty_ = true;
        }
      }
      if (graph_.parameters.empty())
      {
        ImGui::TextDisabled("add a parameter first");
      }
      ImGui::EndCombo();
    }

    ImGui::Spacing();
    if (ImGui::Button(ICON_FA_TRASH "  Delete transition", ImVec2(-1.0f, 0.0f)))
    {
      pendingDeleteTransition_ = selectedTransition_;
    }

    // The delete cannot run inside the details pane either: the reference this
    // function holds would dangle the moment the vector shrinks.
    if (pendingDeleteTransition_ >= 0)
    {
      const int index = pendingDeleteTransition_;
      pendingDeleteTransition_ = -1;
      delete_transition(index);
    }
  }

  // ---------------------------------------------------------------------------
  // Graph lifecycle
  // ---------------------------------------------------------------------------

  void AnimatorGraphPlugin::refresh_graph_list(EditorPluginContext &context)
  {
    (void)context;
    AnimationClipCache &cache = AnimationClipCache::instance();
    graphList_ = cache.listGraphs();
    clipList_ = cache.listClips();
  }

  void AnimatorGraphPlugin::load_graph(EditorPluginContext &context, const std::string &name)
  {
    AnimationClipCache &cache = AnimationClipCache::instance();
    cache.invalidate(name);

    const AnimatorGraph *loaded = cache.graph(name);
    if (loaded == nullptr)
    {
      const std::string error = cache.errorFor(name);
      errorMessage_ = error.empty() ? ("Could not load animator '" + name + "'.") : error;
      return;
    }

    graph_ = *loaded;
    graphName_ = name;
    graphLoaded_ = true;
    graphDirty_ = false;
    activeLayer_ = 0;
    selectedState_ = -1;
    selectedTransition_ = -1;
    draggingState_ = -1;
    pendingTransitionFrom_ = -1;
    maskBufferLayer_ = -1;
    panX_ = 40.0f;
    panY_ = 40.0f;
    zoom_ = 1.0f;
    reset_history();
    errorMessage_.clear();
    statusMessage_ = "Loaded '" + name + "'.";
    graph_.validate(validationProblems_);
    refresh_graph_list(context);
  }

  void AnimatorGraphPlugin::save_graph(EditorPluginContext &context)
  {
    if (!graphLoaded_ || graphName_.empty())
    {
      errorMessage_ = "No animator to save.";
      return;
    }

    graph_.name = graphName_;

    std::string error;
    if (!AnimationClipCache::instance().saveGraph(graphName_, graph_, &error))
    {
      errorMessage_ = error.empty() ? "Could not save the animator." : error;
      return;
    }

    graphDirty_ = false;
    errorMessage_.clear();
    statusMessage_ = "Saved '" + graphName_ + "'.";
    graph_.validate(validationProblems_);
    refresh_graph_list(context);
  }

  void AnimatorGraphPlugin::new_graph(EditorPluginContext &context, const std::string &name)
  {
    if (name.empty())
    {
      errorMessage_ = "An animator needs a name.";
      return;
    }

    if (std::find(graphList_.begin(), graphList_.end(), name) != graphList_.end())
    {
      errorMessage_ = "An animator named '" + name + "' already exists.";
      return;
    }

    graph_ = AnimatorGraph{};
    graph_.name = name;
    // A brand new graph gets a base layer with one state, so it is playable
    // the moment it is assigned to an entity.
    graph_.ensure_default_layer();

    graphName_ = name;
    graphLoaded_ = true;
    graphDirty_ = false;
    activeLayer_ = 0;
    selectedState_ = 0;
    selectedTransition_ = -1;
    draggingState_ = -1;
    pendingTransitionFrom_ = -1;
    maskBufferLayer_ = -1;
    panX_ = 40.0f;
    panY_ = 40.0f;
    zoom_ = 1.0f;
    reset_history();
    validationProblems_.clear();
    errorMessage_.clear();

    std::string error;
    if (!AnimationClipCache::instance().saveGraph(graphName_, graph_, &error))
    {
      errorMessage_ = error.empty() ? "Could not create the animator." : error;
      graphDirty_ = true;
    }
    else
    {
      statusMessage_ = "Created '" + name + "'.";
    }

    refresh_graph_list(context);
  }

  // ---------------------------------------------------------------------------
  // Structural edits
  // ---------------------------------------------------------------------------

  void AnimatorGraphPlugin::add_state(float x, float y)
  {
    add_state_of_kind(x, y, AnimStateKind::Clip);
  }

  void AnimatorGraphPlugin::add_state_of_kind(float x, float y, AnimStateKind kind)
  {
    AnimLayer *layer = active_layer();
    if (layer == nullptr)
    {
      return;
    }

    push_undo("Add state");

    AnimState state;
    state.kind = kind;
    state.name = unique_state_name(*layer, kind == AnimStateKind::Clip ? "New State" : "Blend Tree");
    state.x = x;
    state.y = y;
    layer->states.push_back(std::move(state));

    selectedState_ = static_cast<int>(layer->states.size()) - 1;
    selectedTransition_ = -1;
    graphDirty_ = true;
  }

  void AnimatorGraphPlugin::delete_state(int stateIndex)
  {
    AnimLayer *layer = active_layer();
    if (layer == nullptr || stateIndex < 0 || stateIndex >= static_cast<int>(layer->states.size()))
    {
      return;
    }

    push_undo("Delete state");

    layer->states.erase(layer->states.begin() + stateIndex);

    // Drop transitions that touched the state and re-base the indices of the
    // states that shifted down.
    for (auto it = layer->transitions.begin(); it != layer->transitions.end();)
    {
      if (it->toState == stateIndex ||
          (it->fromState != AnimTransition::kAnyState && it->fromState == stateIndex))
      {
        it = layer->transitions.erase(it);
        continue;
      }

      if (it->toState > stateIndex)
      {
        --it->toState;
      }
      if (it->fromState != AnimTransition::kAnyState && it->fromState > stateIndex)
      {
        --it->fromState;
      }
      ++it;
    }

    if (layer->defaultState == stateIndex)
    {
      layer->defaultState = 0;
    }
    else if (layer->defaultState > stateIndex)
    {
      --layer->defaultState;
    }

    if (layer->states.empty())
    {
      layer->defaultState = 0;
    }

    selectedState_ = -1;
    selectedTransition_ = -1;
    draggingState_ = -1;
    pendingTransitionFrom_ = -1;
    graphDirty_ = true;
    clamp_selection();
  }

  void AnimatorGraphPlugin::begin_transition(int fromState)
  {
    AnimLayer *layer = active_layer();
    if (layer == nullptr || fromState < 0 || fromState >= static_cast<int>(layer->states.size()))
    {
      return;
    }

    pendingTransitionFrom_ = fromState;
    statusMessage_ = "Click the destination state (Esc or right-click to cancel).";
  }

  void AnimatorGraphPlugin::complete_transition(int toState)
  {
    AnimLayer *layer = active_layer();
    if (layer == nullptr || pendingTransitionFrom_ < 0 ||
        pendingTransitionFrom_ >= static_cast<int>(layer->states.size()) ||
        toState < 0 || toState >= static_cast<int>(layer->states.size()))
    {
      pendingTransitionFrom_ = -1;
      return;
    }

    push_undo("Add transition");

    AnimTransition transition;
    transition.fromState = pendingTransitionFrom_;
    transition.toState = toState;
    layer->transitions.push_back(std::move(transition));

    selectedTransition_ = static_cast<int>(layer->transitions.size()) - 1;
    selectedState_ = -1;
    pendingTransitionFrom_ = -1;
    graphDirty_ = true;
    statusMessage_.clear();
  }

  void AnimatorGraphPlugin::delete_transition(int transitionIndex)
  {
    AnimLayer *layer = active_layer();
    if (layer == nullptr || transitionIndex < 0 ||
        transitionIndex >= static_cast<int>(layer->transitions.size()))
    {
      return;
    }

    push_undo("Delete transition");
    layer->transitions.erase(layer->transitions.begin() + transitionIndex);
    selectedTransition_ = -1;
    hoveredTransition_ = -1;
    graphDirty_ = true;
  }

  void AnimatorGraphPlugin::rename_parameter(int index, const std::string &newName)
  {
    if (index < 0 || index >= static_cast<int>(graph_.parameters.size()))
    {
      return;
    }

    AnimParameter &parameter = graph_.parameters[static_cast<std::size_t>(index)];
    const std::string previous = parameter.name;
    if (previous == newName)
    {
      return;
    }

    parameter.name = newName;

    if (previous.empty() || newName.empty())
    {
      // An empty name matches every unset blend parameter and every condition
      // whose parameter is "(none)", so propagating it would rebind all of
      // them. A graph loaded with an unnamed parameter is the only way to get
      // here; the editor itself refuses to commit an empty name.
      graphDirty_ = true;
      return;
    }

    // Follow the rename through every reference, otherwise a two-keystroke
    // rename silently breaks every transition that used the parameter.
    for (auto &layer : graph_.layers)
    {
      for (auto &state : layer.states)
      {
        if (state.blendParameterX == previous)
        {
          state.blendParameterX = newName;
        }
        if (state.blendParameterY == previous)
        {
          state.blendParameterY = newName;
        }
      }

      for (auto &transition : layer.transitions)
      {
        for (auto &condition : transition.conditions)
        {
          if (condition.parameter == previous)
          {
            condition.parameter = newName;
          }
        }
      }
    }

    graphDirty_ = true;
  }

  // ---------------------------------------------------------------------------
  // Undo
  // ---------------------------------------------------------------------------

  void AnimatorGraphPlugin::push_undo(const std::string &label)
  {
    UndoEntry entry;
    entry.label = label;
    entry.graph = graph_.to_json();
    undoStack_.push_back(std::move(entry));

    while (undoStack_.size() > kMaxUndo)
    {
      undoStack_.pop_front();
    }

    // A fresh edit forks the history: the states the redo branch would put
    // back never followed from this graph.
    redoStack_.clear();
  }

  void AnimatorGraphPlugin::undo()
  {
    if (undoStack_.empty())
    {
      statusMessage_ = "Nothing to undo.";
      return;
    }

    UndoEntry entry = std::move(undoStack_.back());
    undoStack_.pop_back();

    std::string error;
    AnimatorGraph restored;
    if (!AnimatorGraph::from_json(entry.graph, restored, &error))
    {
      errorMessage_ = error.empty() ? "Could not undo." : error;
      return;
    }

    // Snapshot taken only once the entry is known to be restorable: a redo
    // entry for a step that never happened would move the user forward into
    // a graph they never saw.
    UndoEntry forward;
    forward.label = entry.label;
    forward.graph = graph_.to_json();
    redoStack_.push_back(std::move(forward));
    while (redoStack_.size() > kMaxUndo)
    {
      redoStack_.pop_front();
    }

    graph_ = std::move(restored);
    graphDirty_ = true;
    draggingState_ = -1;
    pendingTransitionFrom_ = -1;
    maskBufferLayer_ = -1;
    clamp_selection();
    graph_.validate(validationProblems_);
    statusMessage_ = "Undid: " + entry.label;
  }

  void AnimatorGraphPlugin::redo()
  {
    if (redoStack_.empty())
    {
      statusMessage_ = "Nothing to redo.";
      return;
    }

    UndoEntry entry = std::move(redoStack_.back());
    redoStack_.pop_back();

    std::string error;
    AnimatorGraph restored;
    if (!AnimatorGraph::from_json(entry.graph, restored, &error))
    {
      errorMessage_ = error.empty() ? "Could not redo." : error;
      return;
    }

    // Straight onto undoStack_ rather than through push_undo(), which would
    // clear the very redo branch being walked.
    UndoEntry back;
    back.label = entry.label;
    back.graph = graph_.to_json();
    undoStack_.push_back(std::move(back));
    while (undoStack_.size() > kMaxUndo)
    {
      undoStack_.pop_front();
    }

    graph_ = std::move(restored);
    graphDirty_ = true;
    draggingState_ = -1;
    pendingTransitionFrom_ = -1;
    maskBufferLayer_ = -1;
    clamp_selection();
    graph_.validate(validationProblems_);
    statusMessage_ = "Redid: " + entry.label;
  }

  void AnimatorGraphPlugin::reset_history()
  {
    undoStack_.clear();
    redoStack_.clear();
  }

  // ---------------------------------------------------------------------------
  // Helpers
  // ---------------------------------------------------------------------------

  AnimLayer *AnimatorGraphPlugin::active_layer()
  {
    if (graph_.layers.empty())
    {
      return nullptr;
    }

    activeLayer_ = std::clamp(activeLayer_, 0, static_cast<int>(graph_.layers.size()) - 1);
    return &graph_.layers[static_cast<std::size_t>(activeLayer_)];
  }

  Entity::EntityId AnimatorGraphPlugin::debug_entity(EditorPluginContext &context) const
  {
    const EditorState &editorState = context.editor.state;
    if (!editorState.isPlaying || !editorState.selectedEntity.has_value())
    {
      return Entity::INVALID;
    }

    if (!graphLoaded_ || graphName_.empty())
    {
      return Entity::INVALID;
    }

    const Entity::EntityId entity = *editorState.selectedEntity;
    if (!context.componentManager.hasComponent<AnimatorComponent>(entity))
    {
      return Entity::INVALID;
    }

    // The canvas matches the live state *by name* and the rail writes live
    // parameters by name, so both are wrong unless the entity is running this
    // very graph. Selecting an enemy that happens to own an "Idle" state while
    // the player's animator is open would otherwise light up the wrong node
    // and poke the wrong instance's parameters.
    const AnimatorInstance *instance = AnimationRuntime::instance().find(entity);
    if (instance == nullptr || instance->graph_reference().empty())
    {
      return Entity::INVALID;
    }

    const AnimationClipCache &cache = AnimationClipCache::instance();
    if (cache.resolveGraphPath(instance->graph_reference()) != cache.resolveGraphPath(graphName_))
    {
      return Entity::INVALID;
    }

    return entity;
  }

  void AnimatorGraphPlugin::capture_debug_state(EditorPluginContext &context)
  {
    debugAvailable_ = false;
    debugStateName_.clear();
    debugNormalizedTime_ = 0.0f;
    debugTransitioning_ = false;
    // Cleared here, filled in by the parameter rail further down the same
    // frame. Counted rather than inferred from `debugAvailable_`: the rail
    // resolves its own instance, and the two lookups have to be able to
    // disagree in a test before they disagree in front of a user.
    liveParameterRows_ = 0;

    const AnimatorInstance *instance = active_instance(context);
    if (instance == nullptr)
    {
      return;
    }

    debugAvailable_ = true;
    debugStateName_ = instance->current_state(activeLayer_);
    debugNormalizedTime_ = instance->normalized_time(activeLayer_);
    debugTransitioning_ = instance->is_transitioning(activeLayer_);
  }

  void AnimatorGraphPlugin::sync_asset_root(EditorPluginContext &context)
  {
    // `listedRoot_` is the workspace this panel last configured itself for, and
    // it is the only reliable switch signal: the inspector and the animation
    // editor re-root the shared cache too, so by the time this runs the cache
    // root can already match and the change would go unnoticed. Sampled before
    // the re-root below, which clears it.
    const bool workspaceChanged = !listedRoot_.empty() && listedRoot_ != context.workspacePath;

    AnimationClipCache &cache = AnimationClipCache::instance();
    if (cache.assetRoot() != context.workspacePath)
    {
      cache.setAssetRoot(context.workspacePath);
      listedRoot_.clear();
    }

    if (workspaceChanged && graphLoaded_)
    {
      // graphName_ is a bare stem, so Save would resolve it against the newly
      // opened workspace and write the previous project's states over the
      // animator of the same name there.
      release_preview();
      previewEntity_ = Entity::INVALID;
      graph_ = AnimatorGraph{};
      graphName_.clear();
      graphLoaded_ = false;
      graphDirty_ = false;
      activeLayer_ = 0;
      selectedState_ = -1;
      selectedTransition_ = -1;
      draggingState_ = -1;
      pendingTransitionFrom_ = -1;
      hoveredTransition_ = -1;
      maskBufferLayer_ = -1;
      clipboardValid_ = false;
      nodeRects_.clear();
      reset_history();
      validationProblems_.clear();
      errorMessage_.clear();
      statusMessage_ = "Workspace changed; the open animator was closed.";
    }
  }

  void AnimatorGraphPlugin::poll_asset_lists(EditorPluginContext &context)
  {
    const std::filesystem::path root = context.workspacePath;
    const std::filesystem::path graphsDirectory = AnimationClipCache::graphs_directory(root);
    const std::filesystem::path clipsDirectory = AnimationClipCache::clips_directory(root);

    if (listedRoot_ != root)
    {
      listedRoot_ = root;
      directory_stamp(graphsDirectory, graphsStamp_);
      directory_stamp(clipsDirectory, clipsStamp_);
      refresh_graph_list(context);
      listPollAccumulator_ = 0.0f;
      return;
    }

    listPollAccumulator_ += context.deltaTime;
    if (listPollAccumulator_ < kListPollInterval)
    {
      return;
    }
    listPollAccumulator_ = 0.0f;

    // Assets are written by other panels (and by hand), so watch the directory
    // mtimes rather than re-scanning every frame.
    std::filesystem::file_time_type graphsStamp{};
    std::filesystem::file_time_type clipsStamp{};
    bool changed = false;

    if (directory_stamp(graphsDirectory, graphsStamp) && graphsStamp != graphsStamp_)
    {
      graphsStamp_ = graphsStamp;
      changed = true;
    }
    if (directory_stamp(clipsDirectory, clipsStamp) && clipsStamp != clipsStamp_)
    {
      clipsStamp_ = clipsStamp;
      changed = true;
    }

    if (changed)
    {
      refresh_graph_list(context);
    }
  }

  void AnimatorGraphPlugin::clamp_selection()
  {
    if (graph_.layers.empty())
    {
      activeLayer_ = 0;
      selectedState_ = -1;
      selectedTransition_ = -1;
      draggingState_ = -1;
      pendingTransitionFrom_ = -1;
      return;
    }

    activeLayer_ = std::clamp(activeLayer_, 0, static_cast<int>(graph_.layers.size()) - 1);
    const AnimLayer &layer = graph_.layers[static_cast<std::size_t>(activeLayer_)];
    const int stateCount = static_cast<int>(layer.states.size());
    const int transitionCount = static_cast<int>(layer.transitions.size());

    if (selectedState_ >= stateCount)
    {
      selectedState_ = -1;
    }
    if (selectedTransition_ >= transitionCount)
    {
      selectedTransition_ = -1;
    }
    if (draggingState_ >= stateCount)
    {
      draggingState_ = -1;
    }
    if (pendingTransitionFrom_ >= stateCount)
    {
      pendingTransitionFrom_ = -1;
    }
    if (hoveredTransition_ >= transitionCount)
    {
      hoveredTransition_ = -1;
    }
  }

  HADES_REGISTER_EDITOR_PLUGIN(AnimatorGraphPlugin, 45)
}
