# Architecture Overview

Hades is split into a small set of core areas so runtime systems, rendering,
and editor code stay easy to trace.

<div class="grid cards architecture-grid" markdown>

-   ### ECS core

    `src/engine/core/ecs`

    Entity, component, and system management primitives.

-   ### Components

    `src/engine/components`

    Data-only gameplay and render state definitions.

-   ### Systems

    `src/engine/systems`

    Runtime behavior that operates across component sets.

-   ### Rendering

    `src/engine/rendering`

    Renderer abstraction plus the Vulkan implementation.

-   ### Editor

    `src/editor`

    Editor, window management, and runtime coordination.

</div>

## Reading Order

Start here for the module map, then use the generated class relationship page
when you need to trace concrete dependencies between types.

## Diagram Generation

The class relationships page is generated from project headers in `src/`:

```bash
python3 scripts/generate_class_diagram.py
```

The generator detects:

- inheritance (`A <|-- B`)
- usage references inside class/struct bodies (`A --> B : uses`)
