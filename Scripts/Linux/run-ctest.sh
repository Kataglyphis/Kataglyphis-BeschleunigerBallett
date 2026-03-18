#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

git config --global --add safe.directory /workspace || true

while [[ $# -gt 0 ]]; do
  case "$1" in
    --vulkan-version)
      VULKAN_VERSION="${2:-}"
      shift 2
      ;;
    --vulkan-setup-script)
      VULKAN_SETUP_SCRIPT="${2:-}"
      shift 2
      ;;
    --vulkan-sdk)
      VULKAN_SDK="${2:-}"
      shift 2
      ;;
    --build-dir)
      BUILD_DIR="${2:-}"
      shift 2
      ;;
    --build-type)
      BUILD_TYPE="${2:-}"
      shift 2
      ;;
    --ctest-exclude)
      CTEST_EXCLUDE="${2:-}"
      shift 2
      ;;
    --)
      shift
      break
      ;;
    -*)
      err "Unknown argument: $1"
      ;;
    *)
      break
      ;;
  esac
done

source_vulkan_env

BUILD_DIR="${BUILD_DIR:-${BUILD_DIR_DEFAULT:-build}}"
BUILD_TYPE="${BUILD_TYPE:-${BUILD_TYPE_DEFAULT:-Debug}}"
CTEST_EXCLUDE="${CTEST_EXCLUDE:-${CTEST_EXCLUDE_DEFAULT:-}}"

if [[ -n "${BUILD_DIR}" ]]; then
  info "Changing to build directory: ${BUILD_DIR}"
  cd "${BUILD_DIR}"
fi

CTEST_CMD=(
  ctest
  -C "${BUILD_TYPE}"
  --verbose
  --extra-verbose
  --debug
  -T test
  --output-on-failure
)

if [[ -n "${CTEST_EXCLUDE}" ]]; then
  info "Excluding tests matching: ${CTEST_EXCLUDE}"
  CTEST_CMD+=( -E "${CTEST_EXCLUDE}" )
fi

CTEST_CMD+=( "$@" )

info "Executing: ${CTEST_CMD[*]}"
"${CTEST_CMD[@]}"