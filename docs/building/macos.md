# Build on macOS

Tested on Apple Silicon with Apple clang 17.

## Prerequisites

```bash
xcode-select --install
brew install cmake molten-vk vulkan-headers vulkan-loader vulkan-tools glslang
```

## Build & run

```bash
git clone --recurse-submodules https://github.com/nenuadrian/hades-game-engine.git
cd hades-game-engine
cmake -S . -B build
cmake --build build
sh scripts/speedrun.sh
```

`speedrun.sh` wires up the MoltenVK ICD/layer paths before launching `build/Hades`.

See [overview](overview.md) for tests and flags.
