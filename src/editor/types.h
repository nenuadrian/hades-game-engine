#ifndef EDITOR_TYPES_H
#define EDITOR_TYPES_H

#include <queue>

typedef enum EDITOR_EventType
{
  EDITOR_FIRSTEVENT = 0, /**< Unused (do not remove) */

  EDITOR_QUIT = 0x100,
} EditorEventType;

struct EditorState
{
  std::queue<EDITOR_EventType> events = std::queue<EDITOR_EventType>();
  bool showDebugInfo = false;
};

#endif
