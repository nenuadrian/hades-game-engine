---
hide:
  - toc
---

# Hades Game Engine

`> source-first docs for the engine, editor, and generated type maps`

## Jump In

- [Architecture overview](architecture.md)
- [Class relationships](generated/class-relationships.md)

<div class="grid cards" markdown>

-   ### Architecture overview

    Start with the engine/editor boundaries, ECS flow, and rendering structure
    before dropping into source.

-   ### Generated class map

    Open the generated relationship map when you need the concrete type-level
    links between systems.

-   ### Local workflow

    Build and preview the docs locally:

    ```bash
    python3 -m pip install -r requirements-docs.txt
    python3 scripts/generate_class_diagram.py
    python3 -m mkdocs serve
    ```

</div>

## What This Site Covers

- engine and editor module boundaries
- generated class relationships from `src/**/*.h*`
- the GitHub Pages docs workflow
