#ifndef HADES_ENGINE_COMPONENTS_ANIMATOR_COMPONENT_HPP
#define HADES_ENGINE_COMPONENTS_ANIMATOR_COMPONENT_HPP

#include <string>
#include <vector>

namespace hades
{
  /// Authored override of one animator graph parameter, applied when the
  /// entity starts playing.
  struct AnimatorParamOverride
  {
    std::string name;
    /// "float", "int", "bool" — triggers are never authored.
    std::string type = "float";
    float floatValue = 0.0f;
    int intValue = 0;
    bool boolValue = false;
  };

  /// Drives skeletal animation for the entity's ModelComponent through an
  /// animator graph, an authored clip, or both.
  ///
  /// This component is only the *description*: playback state lives in
  /// AnimationRuntime, keyed by entity, so play-mode snapshots stay small and
  /// scripts can reach the player without a ComponentManager.
  ///
  /// It supersedes AnimationComponent, which plays a clip embedded in the
  /// imported model by index. Both may coexist on a scene; when an entity has
  /// an AnimatorComponent, it wins.
  struct AnimatorComponent
  {
    /// Animator graph under `.hades/animators/`. Empty means clip mode.
    std::string graphPath;
    /// Clip under `.hades/animations/` played on start in clip mode, and used
    /// as the editor preview target.
    std::string defaultClip;
    bool playOnStart = true;
    bool looping = true;
    float speed = 1.0f;
    /// Seconds of crossfade applied by `hades::Animation::play` when the
    /// caller does not pass one explicitly.
    float defaultBlendSeconds = 0.15f;
    std::vector<AnimatorParamOverride> parameters;
  };
}

#endif
