<p align="center">
  <h1>Hades - Light C++ 3D Game Engine</h1>

[![CMake on multiple platforms](https://github.com/nenuadrian/hades-game-engine/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/nenuadrian/hades-game-engine/actions/workflows/cmake-multi-platform.yml)
[![Docs](https://github.com/nenuadrian/hades-game-engine/actions/workflows/docs-pages.yml/badge.svg)](https://github.com/nenuadrian/hades-game-engine/actions/workflows/docs-pages.yml)
[![Docker test image](https://github.com/nenuadrian/hades-game-engine/actions/workflows/docker-build.yml/badge.svg)](https://github.com/nenuadrian/hades-game-engine/actions/workflows/docker-build.yml)

</p>

## table of contents

- [table of contents](#table-of-contents)
- [overview](#overview)
- [goals](#goals)
- [features](#features)
- [Entity-Component-System (ECS)](#entity-component-system-ecs)
- [Build \& Run \& Test](#build--run--test)
  - [Build](#build)
  - [Run](#run)
  - [Test](#test)
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

## Entity-Component-System (ECS)

Custom built to build up a natural understanding of the pattern. For example, it is not an Entity-Component System, made from entity and components, but one made out of three parts, the system being an essential component of the triad, acting on entities with specific components.

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
Linux, or Windows.

### Test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Scripting

For the scripting workflow, runtime behavior, and the current limitations, see
[SCRIPTS.md](SCRIPTS.md).

## Documentation

```bash
python3 -m pip install -r requirements-docs.txt
python3 scripts/generate_class_diagram.py
python3 -m mkdocs serve
```

## Previous version

Multiple versions overrode each other in this repository, going through Metal, Vulkan and Open GL (<https://github.com/nenuadrian/hades-game-engine/tree/507e1d5c3bece7e09d78d668d4e3d652be0b2431>).
