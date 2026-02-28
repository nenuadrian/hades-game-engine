# Hades - Light C++ 3D Game Engine

[![CMake on multiple platforms](https://github.com/nenuadrian/hades-game-engine/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/nenuadrian/hades-game-engine/actions/workflows/cmake-multi-platform.yml)

## table of contents

- [Hades - Light C++ 3D Game Engine](#hades---light-c-3d-game-engine)
  - [table of contents](#table-of-contents)
  - [overview](#overview)
  - [goals](#goals)
  - [features](#features)
  - [Libraries](#libraries)
  - [Entity-Component-System (ECS)](#entity-component-system-ecs)
    - [Example](#example)
  - [Build \& Run \& Test](#build--run--test)
    - [Build](#build)
    - [Run](#run)
    - [Test](#test)
  - [Documentation (MkDocs Material)](#documentation-mkdocs-material)
  - [Previous version](#previous-version)

## overview

A game engine written in C++, which has the ability to handle 3D graphics, sound, entity management, and game mechanics using scripts. It currently supports the OpenGL rendering system, but there are plans to include Vulkan as a renderer in the future. The build system used for this engine is cmake.

The purpose is educational and experimental in nature to explore the intriguing world of game engine development.

Not maintained or supported.

![logo](docs/logo.jpeg)

## goals

- develop C++ software engineering skills
- understand graphics 2D and 3D rendering pipelines with OpenGL and other frameworks
- build a usable engine for making a small game

## features

- entity management, with camera and model features
- save / load project from JSON
- model loading using assimp and stb
- sound
- tests
- generating shaders dynamically

## Libraries

- SDL2
- glfw
- OpenGL
- imgui
- googletest
- bgfx

## Entity-Component-System (ECS)

Custom built to build up a natural understanding of the pattern. For example, it is not an Entity-Component System, made from entity and components, but one made out of three parts, the system being an essential component of the triad, acting on entities with specific components.

### Example

## Build & Run & Test

### Build

```bash
mkdir build
cd build
cmake ..
make
```

### Run

```bash
cd build
./HadesGameEngine
```

### Test

```bash
mkdir build
cd build
cmake ..
make
ctest --output-on-failure
```

## Documentation (MkDocs Material)

```bash
python3 -m pip install -r requirements-docs.txt
python3 scripts/generate_class_diagram.py
mkdocs serve
```

Deployments to GitHub Pages are automated by `.github/workflows/docs-pages.yml` on pushes to `main`.

## Previous version

Multiple versions overrode each other in this repository, going through Metal, Vulkan and Open GL (<https://github.com/nenuadrian/hades-game-engine/tree/507e1d5c3bece7e09d78d668d4e3d652be0b2431>).
