#ifndef HADES_EDITOR_ANIMATION_TIMELINE_HPP
#define HADES_EDITOR_ANIMATION_TIMELINE_HPP

#include <string>
#include <vector>

#include "../engine/animation/animation_clip.hpp"
#include "../engine/animation/animation_types.hpp"

namespace hades
{
  class Skeleton;

  /// One line of the dope sheet.
  struct TimelineRow
  {
    enum class Kind
    {
      /// Every key in the clip, collapsed onto one line.
      Summary = 0,
      /// Every key of one bone, collapsed.
      BoneSummary,
      /// One channel of one bone.
      Channel,
      /// The clip's animation events.
      Events,
    };

    Kind kind = Kind::Channel;
    std::string label;
    /// Track name; empty for Summary and Events rows.
    std::string bone;
    TrackChannel channel = TrackChannel::Translation;
    /// Skeleton joint index, or -1 when the track binds to no joint of the
    /// current skeleton (a retargeting mismatch — drawn greyed out).
    int joint = -1;
    int depth = 0;
  };

  /// Identifies one key: which row it sits on and its time.
  struct TimelineKeyRef
  {
    int row = -1;
    float time = 0.0f;

    bool operator==(const TimelineKeyRef &other) const
    {
      return row == other.row && time == other.time;
    }
  };

  /// Persistent view/interaction state, owned by the panel across frames.
  struct AnimationTimelineState
  {
    float time = 0.0f;
    float viewStart = 0.0f;
    float viewEnd = 2.0f;
    float rowHeight = 20.0f;
    float labelWidth = 200.0f;
    bool snapToFrames = true;
    bool showCurves = false;
    /// Rows the user collapsed, by label.
    std::vector<std::string> collapsedBones;
    std::vector<TimelineKeyRef> selection;

    // Interaction bookkeeping — owned by the widget, do not poke from outside.
    bool draggingKeys = false;
    bool draggingPlayhead = false;
    bool boxSelecting = false;
    float dragOriginTime = 0.0f;
    float dragLastDelta = 0.0f;
    float boxSelectStartX = 0.0f;
    float boxSelectStartY = 0.0f;
    /// Clipboard of cut/copied keys, as a serialised fragment.
    std::string clipboard;

    bool isCollapsed(const std::string &bone) const;
    void toggleCollapsed(const std::string &bone);
    bool isSelected(const TimelineKeyRef &key) const;
  };

  /// What the user did to the clip this frame. `clipChanged` means the widget
  /// already mutated the clip and the panel should snapshot for undo using
  /// `changeLabel`.
  struct AnimationTimelineResult
  {
    bool timeChanged = false;
    bool clipChanged = false;
    std::string changeLabel;
    bool selectionChanged = false;
    /// The user asked for a key at `contextTime` on `contextRow`; the panel
    /// services it because only the panel knows the current pose.
    bool requestInsertKey = false;
    bool requestDeleteSelection = false;
    int contextRow = -1;
    float contextTime = 0.0f;
  };

  /// Build the dope-sheet rows for a clip. `filter` is a case-insensitive
  /// substring match on the bone name; when `selectedOnly` is set, only the
  /// track for `selectedJoint` is emitted.
  void build_timeline_rows(
      const AnimationClipAsset &clip,
      const Skeleton &skeleton,
      const std::string &filter,
      int selectedJoint,
      bool selectedOnly,
      std::vector<TimelineRow> &out);

  /// Draw the dope sheet: a ruler, a scrollable row list with a key diamond
  /// per keyframe, a draggable play head, box selection, key dragging, and a
  /// right-click context menu (insert key, delete, set interpolation).
  ///
  /// Mutates `clip` directly for moves and deletions; reports what it did.
  /// Must be called inside a window, and consumes the remaining content
  /// region unless `availableHeight` is positive.
  AnimationTimelineResult draw_animation_timeline(
      const char *id,
      AnimationClipAsset &clip,
      const std::vector<TimelineRow> &rows,
      AnimationTimelineState &state,
      float availableHeight = 0.0f);

  /// Draw the curve editor for the rows currently selected: one polyline per
  /// vector component (rotations are shown as euler degrees), with draggable
  /// key points and a visible easing shape between them.
  ///
  /// Merges its own edits into `result`.
  void draw_animation_curves(
      const char *id,
      AnimationClipAsset &clip,
      const std::vector<TimelineRow> &rows,
      AnimationTimelineState &state,
      float height,
      AnimationTimelineResult &result);

  /// Map a time to an x position inside the timeline's key area, and back.
  float timeline_time_to_x(const AnimationTimelineState &state, float regionX, float regionWidth, float time);
  float timeline_x_to_time(const AnimationTimelineState &state, float regionX, float regionWidth, float x);
  /// Round `time` to the clip's frame grid when snapping is enabled.
  float timeline_snap(const AnimationTimelineState &state, const AnimationClipAsset &clip, float time);
}

#endif
