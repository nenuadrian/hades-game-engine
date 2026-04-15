# Build on Windows

## Prerequisites

- Visual Studio 2022 with the *Desktop development with C++* workload
- CMake 3.20+
- [Vulkan SDK](https://sdk.lunarg.com) (LunarG installer)

## Build & run

Open a *Developer PowerShell for VS 2022*:

```powershell
git clone --recurse-submodules https://github.com/nenuadrian/hades-game-engine.git
cd hades-game-engine
cmake -S . -B build
cmake --build build --config Release
build\Release\Hades.exe
```

See [overview](overview.md) for tests and flags.
