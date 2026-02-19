#!/usr/bin/env bash
set -euo pipefail

git config --global --add safe.directory /workspace || true

BUILD_DIR="${BUILD_DIR:-build}"
PRESET="${PRESET:-}"
SCAN_BUILD_OUT="${SCAN_BUILD_OUT:-scan-build-reports}"

mkdir -p "${SCAN_BUILD_OUT}"

scan_cmd=(scan-build -o "${SCAN_BUILD_OUT}" cmake --build "${BUILD_DIR}")
if [[ -n "${PRESET}" ]]; then
  scan_cmd+=(--preset "${PRESET}")
fi

"${scan_cmd[@]}"
