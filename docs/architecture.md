# Architecture Overview

Hades is split into a few core areas:

- `src/engine/core/ecs`: entity/component/system management primitives
- `src/engine/components`: data-only gameplay/render components
- `src/engine/systems`: ECS systems operating on components
- `src/engine/rendering`: renderer abstraction and Vulkan implementation
- `src/editor`: editor and window/runtime coordination

## Diagram Generation

The class relationships page is generated from project headers in `src/`:

```bash
python3 scripts/generate_class_diagram.py
```

The generator detects:

- inheritance (`A <|-- B`)
- usage references inside class/struct bodies (`A --> B : uses`)
