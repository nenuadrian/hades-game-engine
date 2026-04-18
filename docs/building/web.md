# Building for the Web (Emscripten / WebAssembly)

Hades compiles to WebAssembly via Emscripten and renders through WebGPU. The
MVP produces a browser-loadable editor (`Hades.html`) and standalone runtime
(`HadesRuntime.html`) that load the bundled sample project out of the box.

## Prerequisites

- A POSIX shell (macOS, Linux, or WSL).
- Python 3 for `serve-web.sh`.
- The Emscripten SDK.

Install Emscripten. Either option works:

**Homebrew (macOS / Linux)**

```bash
brew install emscripten
```

This puts `emcc`, `emcmake`, etc. on `PATH` permanently.

**emsdk (cross-platform)**

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

`source emsdk_env.sh` must be re-run each new shell — it puts `emcmake` on `PATH`.

## Build

From the repo root:

```bash
./scripts/build-web.sh
```

This configures the `build-web/` directory with the Emscripten toolchain and
builds the `Hades` and `HadesRuntime` targets. Artifacts land at:

- `build-web/Hades.{html,js,wasm,data}` — the editor
- `build-web/HadesRuntime.{html,js,wasm,data}` — the standalone runtime

Override the build type with `HADES_WEB_BUILD_TYPE=Debug ./scripts/build-web.sh`.

## Serve

WebAssembly + WebGPU will not run from `file://`. Use the bundled helper:

```bash
./scripts/serve-web.sh
```

Then open <http://localhost:8080/Hades.html> in a recent Chrome or Edge (113+),
or Firefox Nightly with `dom.webgpu.enabled`.

## What works on web

- ECS core (entity/component manager, systems)
- WebGPU rendering of the scene viewport
- Scene / entity / properties editor panels
- Workspace loading from the embedded project
- Audio (SoLoud), physics (Jolt)
- Settings + debug console windows

## What's disabled on web

- Export / game packaging (native toolchain required)
- Play mode — script compilation uses `dlopen` and is unavailable in the browser
- External editor integration (VS Code / Rider launch)
- Native file / folder picker dialogs
- Multi-viewport ImGui (browser canvas cannot spawn OS windows)
- Neural engine (LibTorch has no web target)

A banner in the editor surfaces these limitations at runtime.

## Bundling a different sample project

By default the web build embeds `tests/test_project/.hades/` at `/assets/.hades`
inside the WASM virtual filesystem. Point it elsewhere with:

```bash
emcmake cmake -S . -B build-web \
  -DHADES_WEB_ASSET_DIR=/absolute/path/to/your_project/.hades
cmake --build build-web --target Hades HadesRuntime -j
```

## Troubleshooting

- **"WebGPU not available"** — browser is too old, WebGPU flag is off, or the
  page is being served over `file://`. Use `scripts/serve-web.sh` and a current
  Chromium-based browser.
- **Blank canvas, WASM loads** — check DevTools for WebGPU device creation
  errors. On Linux, make sure Vulkan drivers are installed so Dawn can map to
  them.
- **`EMSDK not set`** — run `source "$EMSDK/emsdk_env.sh"` in the current shell.
- **Configure fails looking for Vulkan** — you forgot `emcmake`. The build
  script always uses it; if you invoke CMake directly, prefix with `emcmake`.
