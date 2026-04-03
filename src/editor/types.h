#ifndef EDITOR_TYPES_H
#define EDITOR_TYPES_H

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
  };

  enum class EditorPlayAction
  {
    None,
    Start,
    Stop,
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
  };
}

#endif
