# Class Relationship Diagram

This page is auto-generated from `src/**/*.h*` by `scripts/generate_class_diagram.py`.

```mermaid
classDiagram
  direction LR
  class ComponentArray
  class ComponentManager
  class Editor
  class EditorState
  class Entity
  class EntityManager
  class GUI
  class ImGui_GUI
  class MenuBarItem
  class MovementSystem
  class PositionComponent2D
  class PositionComponent3D
  class RenderComponent
  class RenderSystem
  class Renderer
  class System
  class SystemManager
  class TransformHierarchyComponent
  class VulkanRenderer
  class Vulkan_Frame
  class Vulkan_FrameSemaphores
  class Vulkan_Window
  class WindowManager
  GUI <|-- ImGui_GUI
  Renderer <|-- VulkanRenderer
  System <|-- MovementSystem
  System <|-- RenderSystem
  ComponentArray --> Entity : uses
  ComponentManager --> ComponentArray : uses
  ComponentManager --> Entity : uses
  Editor --> ComponentManager : uses
  Editor --> EditorState : uses
  Editor --> Entity : uses
  Editor --> EntityManager : uses
  Editor --> GUI : uses
  Editor --> ImGui_GUI : uses
  Editor --> MenuBarItem : uses
  Editor --> TransformHierarchyComponent : uses
  EntityManager --> Entity : uses
  GUI --> MenuBarItem : uses
  MovementSystem --> ComponentManager : uses
  MovementSystem --> EntityManager : uses
  MovementSystem --> PositionComponent3D : uses
  RenderSystem --> ComponentManager : uses
  RenderSystem --> EntityManager : uses
  RenderSystem --> RenderComponent : uses
  System --> ComponentManager : uses
  System --> EntityManager : uses
  SystemManager --> ComponentManager : uses
  SystemManager --> EntityManager : uses
  SystemManager --> System : uses
  TransformHierarchyComponent --> Entity : uses
  VulkanRenderer --> Vulkan_Frame : uses
  VulkanRenderer --> Vulkan_FrameSemaphores : uses
  VulkanRenderer --> Vulkan_Window : uses
  Vulkan_Window --> Vulkan_Frame : uses
  Vulkan_Window --> Vulkan_FrameSemaphores : uses
  WindowManager --> ComponentManager : uses
  WindowManager --> Editor : uses
  WindowManager --> EntityManager : uses
  WindowManager --> MovementSystem : uses
  WindowManager --> RenderSystem : uses
  WindowManager --> Renderer : uses
  WindowManager --> SystemManager : uses
  WindowManager --> VulkanRenderer : uses
```

## Classes

| Class | Kind | Header |
|---|---|---|
| `ComponentArray` | `class` | `src/engine/core/ecs/component_array.hpp` |
| `ComponentManager` | `class` | `src/engine/core/ecs/component_manager.hpp` |
| `Editor` | `class` | `src/editor/editor.hpp` |
| `EditorState` | `struct` | `src/editor/types.h` |
| `Entity` | `class` | `src/engine/core/ecs/entity.hpp` |
| `EntityManager` | `class` | `src/engine/core/ecs/entity_manager.hpp` |
| `GUI` | `class` | `src/engine/gui/gui.hpp` |
| `ImGui_GUI` | `class` | `src/engine/gui/imgui.hpp` |
| `MenuBarItem` | `struct` | `src/engine/gui/gui.hpp` |
| `MovementSystem` | `class` | `src/engine/systems/movement_system.hpp` |
| `PositionComponent2D` | `class` | `src/engine/components/position_component_2d.hpp` |
| `PositionComponent3D` | `class` | `src/engine/components/position_component_3d.hpp` |
| `RenderComponent` | `struct` | `src/engine/components/render_component.hpp` |
| `RenderSystem` | `class` | `src/engine/systems/render_system.hpp` |
| `Renderer` | `class` | `src/engine/rendering/renderer.hpp` |
| `System` | `class` | `src/engine/core/ecs/system.hpp` |
| `SystemManager` | `class` | `src/engine/core/ecs/system_manager.hpp` |
| `TransformHierarchyComponent` | `class` | `src/engine/components/transform_hierarchy_component.hpp` |
| `VulkanRenderer` | `class` | `src/engine/rendering/vulkan.hpp` |
| `Vulkan_Frame` | `struct` | `src/engine/rendering/vulkan.hpp` |
| `Vulkan_FrameSemaphores` | `struct` | `src/engine/rendering/vulkan.hpp` |
| `Vulkan_Window` | `struct` | `src/engine/rendering/vulkan.hpp` |
| `WindowManager` | `class` | `src/editor/window_manager.hpp` |
