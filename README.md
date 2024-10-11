# Hades - Light C++ 3D Game Engine

[![CMake on multiple platforms](https://github.com/nenuadrian/hades-game-engine/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/nenuadrian/hades-game-engine/actions/workflows/cmake-multi-platform.yml)

A game engine written in C++, which has the ability to handle 3D graphics, sound, entity management, and game mechanics using scripts. It currently supports the OpenGL rendering system, but there are plans to include Vulkan as a renderer in the future. The build system used for this engine is cmake.

The purpose is educational and experimental in nature to explore the intriguing world of game engine development.

Not maintained or supported.

![logo](docs/logo.jpeg)

## goals
 * develop C++ software engineering skills
 * understand graphics 2D and 3D rendering pipelines with OpenGL and other frameworks
 * build a usable engine for making a small game

## features

 * entity management, with camera and model features
 * save / load project from JSON
 * model loading using assimp and stb
 * sound
 * tests
 * generating shaders dynamically

## libraries

 * assimp
 * glad
 * glew
 * glfw
 * glm
 * imgui
 * nativefiledialog
 * stb
 * catch2
 * soloud
 * json
 * entt

## Build & Run

### Build

```
mkdir build
cd build
cmake ..
make
```

### Run

```
cd build
./HadesGameEngine
```

### Test

```
mkdir build
cd build
cmake ..
make
ctest --output-on-failure
```

## setup

```
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig"
```

