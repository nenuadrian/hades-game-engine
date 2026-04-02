---
hide:
  - toc
---

<section class="hero-panel">
  <p class="hero-panel__eyebrow">C++ Engine Documentation</p>
  <h1>Hades Game Engine</h1>
  <p class="hero-panel__lead">
    Architecture notes, engine internals, and generated class maps in a darker,
    wider layout built for actual reading instead of a narrow default docs column.
  </p>
  <div class="hero-panel__actions">
    <a class="md-button md-button--primary" href="architecture/">Explore architecture</a>
    <a class="md-button" href="generated/class-relationships/">Open class relationships</a>
  </div>
</section>

<div class="grid cards" markdown>

-   ### Architecture overview

    Get the high-level map of ECS, rendering, and editor modules before dropping
    into source.

-   ### Generated class map

    Open the auto-generated relationship diagram when you need the concrete
    type-level links between systems.

-   ### Local workflow

    Build and preview the docs locally:

    ```bash
    python3 -m pip install -r requirements-docs.txt
    python3 scripts/generate_class_diagram.py
    python3 -m mkdocs serve
    ```

</div>

## What This Site Covers

- engine/editor module boundaries
- generated class relationships from `src/**/*.h*`
- the local docs workflow used for GitHub Pages
