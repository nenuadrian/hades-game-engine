#include "animation_edit_state.hpp"

namespace hades
{
  void AnimationEditState::deactivate()
  {
    active = false;
    entity = Entity::INVALID;
    modelPath.clear();
    jointGlobals.clear();
    modelGlobalInverse = math::Mat4::identity();
    jointParents.clear();
    jointNames.clear();
    jointSkinned.clear();
    selectedJoint = -1;
    hoveredJoint = -1;
    pickedJoint = -1;
    poseEditing = false;
    jointEdited = false;
    jointEditFinished = false;
  }

  AnimationEditState &animation_edit_state()
  {
    static AnimationEditState state;
    return state;
  }
}
