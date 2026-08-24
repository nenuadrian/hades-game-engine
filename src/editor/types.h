#ifndef EDITOR_TYPES_H
#define EDITOR_TYPES_H

#include <chrono>
#include <optional>
#include <queue>
#include <string>

#include "../engine/core/ecs/entity.hpp"

namespace hades
{
  typedef enum EDITOR_EventType
  {
    EDITOR_FIRSTEVENT = 0, /**< Unused (do not remove) */

    EDITOR_QUIT = 0x100,
  } EditorEventType;

  enum class EditorEntityPreset
  {
    None,
    Camera,
    Cube,
    Text,
    AudioEmitter,
    PhysicsCube,
    Plane,
    Sphere,
    Model,
    DirectionalLight,
    PointLight,
    SpotLight,
  };

  enum class EditorPlayAction
  {
    None,
    Start,
    Stop,
  };

  enum class SceneGizmoMode
  {
    Translate,
    Rotate,
    Scale,
  };

  enum class SceneGizmoAxis
  {
    None,
    X,
    Y,
    Z,
  };

  enum class DebugMessageLevel
  {
    Info,
    Warning,
    Error,
  };

  struct DebugMessage
  {
    DebugMessageLevel level = DebugMessageLevel::Info;
    std::string text;
    std::chrono::steady_clock::time_point timestamp;
    std::chrono::system_clock::time_point wallClockTimestamp;
  };

  // A "policy preview" request: when set before EditorPlayAction::Start is
  // consumed, start_play_mode swaps in the named world, injects the given
  // .pt file into the matching ScriptAttachment (so the runtime runs it in
  // Inference mode), and then enters play mode normally. The snapshot taken
  // at play start reverts the injected modelPath on stop — nothing persists.
  struct PreviewRequest
  {
    std::string worldName;
    std::string entityName;
    std::string className;
    std::string modelPath;
  };

  struct EditorState
  {
    std::queue<EDITOR_EventType> events = std::queue<EDITOR_EventType>();
    bool showDebugInfo = false;
    std::optional<Entity::EntityId> selectedEntity;
    EditorEntityPreset pendingEntityPreset = EditorEntityPreset::None;
    EditorPlayAction pendingPlayAction = EditorPlayAction::None;
    bool isPlaying = false;
    std::optional<Entity::EntityId> loadedWorld;
    std::optional<Entity::EntityId> activeWorld;
    std::optional<Entity::EntityId> activeCamera;
    std::string playModeMessage;
    bool threeFingerDragActive = false;

    std::optional<PreviewRequest> pendingPreviewRequest;
    bool isPreviewing = false;
  };
}

#endif
