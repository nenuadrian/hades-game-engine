#ifndef HADES_EDITOR_ANIMATION_EDIT_STATE_HPP
#define HADES_EDITOR_ANIMATION_EDIT_STATE_HPP

#include <string>
#include <vector>

#include "../engine/core/ecs/entity.hpp"
#include "../engine/rendering/math3d.hpp"

namespace hades
{
  /// Shared state between the Animation panel and the 3D viewport.
  ///
  /// The viewport lives inside Editor's private scene code and the panel is a
  /// plugin, so neither can reach the other. This is the seam: the panel
  /// publishes what it wants drawn and the viewport publishes what the user
  /// clicked or dragged. Both sides run on the frame loop, single threaded.
  ///
  /// Everything here is per-frame conversation, not persisted state.
  struct AnimationEditState
  {
    /// True while the Animation panel has a live target. The viewport only
    /// draws the skeleton and only hijacks the gizmo when this is set.
    bool active = false;
    Entity::EntityId entity = Entity::INVALID;
    /// Model asset path of the target, for the viewport to resolve.
    std::string modelPath;

    // ---- Published by the panel, consumed by the viewport ----------------

    bool showSkeleton = true;
    bool showJointNames = false;
    /// Draw joints that skin no vertices (hierarchy-only nodes).
    bool showUnskinnedJoints = true;
    /// Transform of every joint at the currently previewed pose, in the same
    /// space the skinned mesh is drawn in — i.e. already premultiplied by the
    /// model's `globalInverseTransform`, exactly as the bone palette is.
    /// Publishing raw node globals instead would draw the skeleton in a
    /// different space than the mesh whenever that transform is not identity
    /// (which is most FBX and Collada exports). Sized to the skeleton,
    /// republished every frame the panel renders.
    std::vector<math::Mat4> jointGlobals;

    /// The model's `globalInverseTransform`. `jointGlobals` already has it
    /// applied; this is here so the viewport can build the parent space of a
    /// ROOT joint, whose parent is that transform itself.
    math::Mat4 modelGlobalInverse = math::Mat4::identity();
    /// Joint parent indices, so the viewport can draw bones without
    /// rebuilding the skeleton itself.
    std::vector<int> jointParents;
    std::vector<std::string> jointNames;
    std::vector<bool> jointSkinned;

    int selectedJoint = -1;
    /// When set, the transform gizmo edits the selected joint instead of the
    /// entity transform.
    bool poseEditing = false;

    // ---- Published by the viewport, consumed by the panel ----------------

    int hoveredJoint = -1;
    /// Joint the user clicked this frame, or -1. The panel clears it after
    /// reading, which is what makes this a one-shot request.
    int pickedJoint = -1;

    /// A gizmo drag produced a new local transform for `selectedJoint`.
    /// The panel applies it to the pose and, with auto-key on, writes a key.
    bool jointEdited = false;
    math::Vec3 editedTranslation;
    math::Quat editedRotation;
    math::Vec3 editedScale{1.0f, 1.0f, 1.0f};
    /// True on the frame the drag ends, so the panel can close an undo entry
    /// instead of recording one per mouse-move.
    bool jointEditFinished = false;

    /// Local TRS of the selected joint at the previewed pose, published by
    /// the panel so the viewport gizmo starts from the right place.
    math::Vec3 selectedLocalTranslation;
    math::Quat selectedLocalRotation;
    math::Vec3 selectedLocalScale{1.0f, 1.0f, 1.0f};

    /// Drop everything the panel published. Called when the panel closes or
    /// loses its target so the viewport stops drawing a stale skeleton.
    void deactivate();
  };

  /// The single shared instance.
  AnimationEditState &animation_edit_state();
}

#endif
