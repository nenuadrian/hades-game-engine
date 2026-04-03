# Hades - Light C++ 3D Game Engine

[![CMake on multiple platforms](https://github.com/nenuadrian/hades-game-engine/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/nenuadrian/hades-game-engine/actions/workflows/cmake-multi-platform.yml)
[![Docs](https://github.com/nenuadrian/hades-game-engine/actions/workflows/docs-pages.yml/badge.svg)](https://github.com/nenuadrian/hades-game-engine/actions/workflows/docs-pages.yml)
[![Docker test image](https://github.com/nenuadrian/hades-game-engine/actions/workflows/docker-build.yml/badge.svg)](https://github.com/nenuadrian/hades-game-engine/actions/workflows/docker-build.yml)

## table of contents

- [Hades - Light C++ 3D Game Engine](#hades---light-c-3d-game-engine)
  - [table of contents](#table-of-contents)
  - [overview](#overview)
  - [goals](#goals)
  - [features](#features)
  - [Entity-Component-System (ECS)](#entity-component-system-ecs)
  - [Build \& Run \& Test](#build--run--test)
    - [Build](#build)
    - [Run](#run)
    - [Test](#test)
  - [ECS Benchmark](#ecs-benchmark)
  - [Scripting](#scripting)
  - [Documentation](#documentation)
  - [Previous version](#previous-version)

## overview

A game engine written in C++, which has the ability to handle 3D graphics, sound, entity management, and game mechanics using scripts. It currently supports the OpenGL rendering system, but there are plans to include Vulkan as a renderer in the future. The build system used for this engine is cmake.

The purpose is educational and experimental in nature to explore the intriguing world of game engine development.

Not maintained or supported.

![logo](docs/logo.png)

## goals

- develop C++ software engineering skills
- understand graphics 2D and 3D rendering pipelines with OpenGL and other frameworks
- build a usable engine for making a small game

## features

- entity management, with camera and model features
- C# scripts attachable to entities and compiled on play through the local dotnet SDK
- save / load project from JSON
- model loading using assimp and stb
- sound via miniaudio, with streaming, buses, and basic 3D spatial audio support
- tests
- generating shaders dynamically

![editor](assets/editor.png)
![workspace](assets/workspace.png)

## Entity-Component-System (ECS)

Custom built to build up a natural understanding of the pattern. For example, it is not an Entity-Component System, made from entity and components, but one made out of three parts, the system being an essential component of the triad, acting on entities with specific components.

<a id="build-run-test"></a>

## Build & Run & Test

### Build

```bash
cmake -S . -B build
cmake --build build
```

To force fresh downloads instead of using `lib/`:

```bash
cmake -S . -B build -DHADES_USE_BUNDLED_DEPS=OFF
cmake --build build
```

### Run

```bash
./build/HadesGameEngine
```

Entity scripts are compiled when Play starts. Install a local `dotnet` SDK if you
want to attach `.cs` files to entities and run them in play mode on macOS,
Linux, or Windows. CMake captures the resolved `dotnet` path at configure time
when available, so rerun `cmake -S . -B build` after installing or moving the
SDK. You can also point CMake at it explicitly with
`-DHADES_DOTNET_EXECUTABLE=/absolute/path/to/dotnet`.

### Test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## ECS Benchmark

`scripts/run_ecs_benchmark.py` builds a small standalone benchmark binary around
the ECS core (`EntityManager`, `ComponentManager`, `SystemManager`) with an
optimized compiler invocation, so you can profile ECS hot paths without pulling
in the full editor or renderer build.

Run the default benchmark suite:

```bash
python3 scripts/run_ecs_benchmark.py
```

Tune the workload:

```bash
python3 scripts/run_ecs_benchmark.py --entities 250000 --frames 1000 --iterations 5 --warmup 1
```

The runner expects a C++17 compiler on `PATH` and writes the compiled binary to
`build/benchmarks/ecs_benchmark`.

Results captured on April 3, 2026 on `Darwin arm64` with `Apple clang 17.0.0`
using the default benchmark settings (`100000` entities, `500` frames,
`5` measured iterations, `1` warmup iteration):

```text
Hades ECS benchmark
config: entities=100000, frames=500, iterations=5, warmup=1

spawn_entities                       avg     0.447 ms  min     0.390 ms  max     0.487 ms  throughput 223734368.24 entities/s
spawn_and_attach_position_velocity   avg    64.682 ms  min    56.758 ms  max    76.831 ms  throughput   1546034.33 entities/s
system_update_position_velocity      avg  1598.043 ms  (  3.196 ms/frame)  min  1401.624 ms  max  2007.233 ms  throughput  31288267.51 entity updates/s
destroy_entities                     avg   314.592 ms  min   299.797 ms  max   331.084 ms  throughput    317871.80 entities/s
```

## Scripting

For C# scripting workflow, runtime behavior, and the current limitations, see
[SCRIPTS.md](SCRIPTS.md).

## Documentation

```bash
python3 -m pip install -r requirements-docs.txt
python3 scripts/generate_class_diagram.py
python3 -m mkdocs serve
```

## Previous version

Multiple versions overrode each other in this repository, going through Metal, Vulkan and Open GL (<https://github.com/nenuadrian/hades-game-engine/tree/507e1d5c3bece7e09d78d668d4e3d652be0b2431>).
