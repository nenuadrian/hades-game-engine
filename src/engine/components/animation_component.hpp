#ifndef HADES_ENGINE_COMPONENTS_ANIMATION_COMPONENT_HPP
#define HADES_ENGINE_COMPONENTS_ANIMATION_COMPONENT_HPP

namespace hades
{
  /// Plays an animation clip from the entity's ModelComponent asset.
  /// The AnimationSystem advances `time` during play mode; outside play
  /// mode the pose can be scrubbed from the inspector.
  struct AnimationComponent
  {
    int clipIndex = 0;
    bool playing = true;
    bool looping = true;
    float speed = 1.0f;
    float time = 0.0f;
  };
}

#endif
