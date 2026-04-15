# Build on Linux

## Prerequisites

GCC 12+ or Clang 15+, CMake 3.20+, Vulkan SDK, and X11 dev libs.

Debian / Ubuntu:

```bash
sudo apt-get install build-essential cmake libvulkan-dev vulkan-tools \
  glslang-tools xorg-dev
```

## Build & run

```bash
git clone --recurse-submodules https://github.com/nenuadrian/hades-game-engine.git
cd hades-game-engine
cmake -S . -B build
cmake --build build
./build/Hades
```

See [overview](overview.md) for tests and flags.
