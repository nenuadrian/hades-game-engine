#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build-web"
BUILD_TYPE="${HADES_WEB_BUILD_TYPE:-Release}"

if ! command -v emcmake >/dev/null 2>&1; then
  cat >&2 <<'EOF'
error: emcmake is not on PATH.

Install Emscripten one of these ways, then re-run this script:

  Homebrew (macOS/Linux):
    brew install emscripten

  emsdk (cross-platform):
    git clone https://github.com/emscripten-core/emsdk.git
    cd emsdk && ./emsdk install latest && ./emsdk activate latest
    source ./emsdk_env.sh
EOF
  exit 1
fi

emcmake cmake -S "${ROOT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build "${BUILD_DIR}" --target Hades HadesRuntime -j

echo
echo "Web build complete. Serve it with:"
echo "  scripts/serve-web.sh"
echo "Then open http://localhost:8080/Hades.html"
