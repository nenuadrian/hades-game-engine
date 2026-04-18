#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build-web"
PORT="${HADES_WEB_PORT:-8080}"

if [[ ! -f "${BUILD_DIR}/Hades.html" ]]; then
  echo "error: ${BUILD_DIR}/Hades.html not found. Run scripts/build-web.sh first." >&2
  exit 1
fi

cd "${BUILD_DIR}"
echo "Serving ${BUILD_DIR} on http://localhost:${PORT}"
echo "  editor:  http://localhost:${PORT}/Hades.html"
echo "  runtime: http://localhost:${PORT}/HadesRuntime.html"
exec python3 -m http.server "${PORT}"
