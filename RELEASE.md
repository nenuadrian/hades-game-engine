# Releasing Hades Engine

## Creating a Release

### Tag-triggered (recommended)

Push a version tag to `main` to automatically create a published GitHub Release:

```bash
git tag v1.0.0
git push origin v1.0.0
```

### Manual

Go to **Actions > Release > Run workflow** on GitHub. Optionally provide a tag name — if omitted, a draft release is created from HEAD.

## What the Release Contains

Each release includes:

| Artifact | Description |
|----------|-------------|
| `*-full-source.tar.gz` / `.zip` | Complete source with all 9 dependencies vendored — build offline with no internet required |
| `*-linux-x64.tar.gz` | Pre-built editor + runtime for Linux x86_64 |
| `*-windows-x64.zip` | Pre-built editor + runtime for Windows x64 |
| `*-macos-arm64.tar.gz` | Pre-built editor + runtime for macOS Apple Silicon |

The vendored source archives are insurance against upstream dependencies disappearing. Every release is fully self-contained.

## Building from a Vendored Source Archive

Download the `*-full-source.tar.gz` or `.zip` from the release, then:

```bash
tar xzf hades-engine-v1.0.0-full-source.tar.gz
cd hades-engine-v1.0.0-full-source
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHADES_ALLOW_DOWNLOADS=OFF
cmake --build build --config Release
```

The Vulkan SDK is still required on the host system:
- **Linux**: `sudo apt-get install libvulkan-dev xorg-dev`
- **macOS**: Install from https://sdk.lunarg.com
- **Windows**: Install from https://sdk.lunarg.com or use the LunarG installer

## Vendoring Dependencies Locally

To vendor dependencies without creating a release (e.g. for offline development):

```bash
./scripts/vendor-deps.sh
```

This clones all dependencies at their pinned versions into `lib/`. The project then builds with `HADES_USE_BUNDLED_DEPS=ON` (the default) and does not require internet access.

## Dependency Versions

Pinned versions are defined in `cmake/Dependencies.cmake`. To update a dependency:

1. Change the tag variable (e.g. `HADES_SDL2_TAG`)
2. Run `./scripts/vendor-deps.sh` to re-vendor
3. Verify the build passes on all platforms
4. Tag a new release
