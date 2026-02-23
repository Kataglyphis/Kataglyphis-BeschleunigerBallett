#!/usr/bin/env bash
set -euo pipefail

git config --global --add safe.directory /workspace || true

BUILD_DIR_ARG=""
PRESET_ARG=""
SCAN_BUILD_OUT_ARG=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR_ARG="${2:-}"
      shift 2
      ;;
    --preset)
      PRESET_ARG="${2:-}"
      shift 2
      ;;
    --scan-build-out)
      SCAN_BUILD_OUT_ARG="${2:-}"
      shift 2
      ;;
    -*)
      echo "Unbekanntes Argument: $1" >&2
      exit 1
      ;;
    *)
      break
      ;;
  esac
done

BUILD_DIR="${BUILD_DIR_ARG:-${BUILD_DIR:-build}}"
PRESET="${PRESET_ARG:-${PRESET:-}}"
SCAN_BUILD_OUT="${SCAN_BUILD_OUT_ARG:-${SCAN_BUILD_OUT:-scan-build-reports}}"

mkdir -p "${SCAN_BUILD_OUT}"

scan_cmd=(scan-build -o "${SCAN_BUILD_OUT}" cmake --build "${BUILD_DIR}")
if [[ -n "${PRESET}" ]]; then
  scan_cmd+=(--preset "${PRESET}")
fi

"${scan_cmd[@]}"
