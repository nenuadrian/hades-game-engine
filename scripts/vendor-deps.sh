#!/usr/bin/env bash
# vendor-deps.sh — Clone all FetchContent dependencies at their pinned tags
# into lib/ so the project can be built fully offline.
#
# Usage: ./scripts/vendor-deps.sh [--lib-dir <path>]
#
# Reads tag variables from cmake/Dependencies.cmake to stay in sync.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEPS_CMAKE="$REPO_ROOT/cmake/Dependencies.cmake"
LIB_DIR="$REPO_ROOT/lib"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --lib-dir) LIB_DIR="$2"; shift 2 ;;
    *) echo "Unknown option: $1" >&2; exit 1 ;;
  esac
done

# Extract a CMake cache variable value from Dependencies.cmake.
cmake_tag() {
  local var="$1"
  grep "set(${var}" "$DEPS_CMAKE" | sed -E 's/^[^"]*"([^"]+)".*/\1/'
}

clone_dep() {
  local name="$1" url="$2" tag="$3" dest="$4"
  if [[ -d "$dest" ]]; then
    echo "  [skip] $name already exists at $dest"
    return
  fi
  echo "  [clone] $name @ $tag -> $dest"
  git clone --depth 1 --branch "$tag" "$url" "$dest"
  # Remove .git to save space in the archive.
  rm -rf "$dest/.git"
}

echo "Vendoring dependencies into $LIB_DIR ..."
mkdir -p "$LIB_DIR"

clone_dep "CLI11" \
  "https://github.com/CLIUtils/CLI11.git" \
  "$(cmake_tag HADES_CLI11_TAG)" \
  "$LIB_DIR/CLI11"

clone_dep "GoogleTest" \
  "https://github.com/google/googletest.git" \
  "$(cmake_tag HADES_GOOGLETEST_TAG)" \
  "$LIB_DIR/googletest"

clone_dep "Dear ImGui" \
  "https://github.com/ocornut/imgui.git" \
  "$(cmake_tag HADES_IMGUI_TAG)" \
  "$LIB_DIR/imgui"

clone_dep "miniaudio" \
  "https://github.com/mackron/miniaudio.git" \
  "$(cmake_tag HADES_MINIAUDIO_TAG)" \
  "$LIB_DIR/miniaudio"

# SDL2 is expected at lib/imgui/lib/SDL2 per Dependencies.cmake
mkdir -p "$LIB_DIR/imgui/lib"
clone_dep "SDL2" \
  "https://github.com/libsdl-org/SDL.git" \
  "$(cmake_tag HADES_SDL2_TAG)" \
  "$LIB_DIR/imgui/lib/SDL2"

clone_dep "Assimp" \
  "https://github.com/assimp/assimp.git" \
  "$(cmake_tag HADES_ASSIMP_TAG)" \
  "$LIB_DIR/assimp"

clone_dep "ImGuiColorTextEdit" \
  "https://github.com/BalazsJako/ImGuiColorTextEdit.git" \
  "$(cmake_tag HADES_IMGUI_TEXTEDIT_TAG)" \
  "$LIB_DIR/ImGuiColorTextEdit"

clone_dep "nlohmann/json" \
  "https://github.com/nlohmann/json.git" \
  "$(cmake_tag HADES_NLOHMANN_JSON_TAG)" \
  "$LIB_DIR/json"

clone_dep "JoltPhysics" \
  "https://github.com/jrouwe/JoltPhysics.git" \
  "$(cmake_tag HADES_JOLTPHYSICS_TAG)" \
  "$LIB_DIR/JoltPhysics"

echo "Done. All dependencies vendored into $LIB_DIR"
