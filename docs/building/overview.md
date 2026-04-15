# Building Hades

Cross-platform C++20 project built with CMake. Pick your OS for minimal instructions:

- [macOS](macos.md)
- [Linux](linux.md)
- [Windows](windows.md)

## Common notes

### Clone with submodules

`hades-neural-engine` is a pinned git submodule at `lib/hades-neural-engine`.

```bash
git clone --recurse-submodules https://github.com/nenuadrian/hades-game-engine.git
```

If you already cloned, or pulled a change that moved the pin:

```bash
git submodule update --init --recursive
```

To bump HNE to its upstream `main`:

```bash
git submodule update --remote --init --recursive lib/hades-neural-engine
git add lib/hades-neural-engine && git commit -m "Update hades-neural-engine"
```

### Dependencies

All third-party dependencies are vendored under `lib/` or fetched automatically. To force fresh downloads:

```bash
cmake -S . -B build -DHADES_USE_BUNDLED_DEPS=OFF
```

To vendor everything locally for offline builds:

```bash
./scripts/vendor-deps.sh
```

### Test

```bash
ctest --test-dir build --output-on-failure
```

### Optional CMake flags

| Flag | Default | Purpose |
|------|---------|---------|
| `HADES_ENABLE_API` | OFF | Build REST API support (see [api.md](../api.md)) |
| `HADES_FRAME_METRICS` | OFF | Per-frame profiling (see [metrics.md](../metrics.md)) |
| `HADES_USE_BUNDLED_DEPS` | ON | Prefer sources under `lib/` |
| `HADES_ALLOW_DOWNLOADS` | ON | Allow FetchContent to fill gaps |
| `HADES_ENABLE_HNE_TRAINING` | ON | Build neural-engine training |
| `HADES_ENABLE_HNE_INFERENCE` | ON | Build neural-engine inference |
