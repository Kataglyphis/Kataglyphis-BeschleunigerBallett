#!/usr/bin/env bash
set -euo pipefail

git config --global --add safe.directory /workspace || true

source_vulkan_env() {
  if [[ -n "${VULKAN_SETUP_SCRIPT:-}" && -f "${VULKAN_SETUP_SCRIPT}" ]]; then
    . "${VULKAN_SETUP_SCRIPT}"
    return
  fi

  if [[ -n "${VULKAN_VERSION:-}" ]]; then
    if [[ -f "/opt/vulkan/${VULKAN_VERSION}/setup-env.sh" ]]; then
      . "/opt/vulkan/${VULKAN_VERSION}/setup-env.sh"
      return
    fi
    if [[ -f "${HOME}/vulkan/${VULKAN_VERSION}/setup-env.sh" ]]; then
      . "${HOME}/vulkan/${VULKAN_VERSION}/setup-env.sh"
      return
    fi
  fi

  if [[ -n "${VULKAN_SDK:-}" && -f "${VULKAN_SDK}/setup-env.sh" ]]; then
    . "${VULKAN_SDK}/setup-env.sh"
    return
  fi

  echo "[run-ctest] Vulkan setup-env.sh nicht gefunden – fahre ohne explizites Sourcing fort."
}

VULKAN_VERSION_ARG=""
VULKAN_SETUP_SCRIPT_ARG=""
VULKAN_SDK_ARG=""
BUILD_DIR_ARG=""
BUILD_TYPE_ARG=""
CTEST_EXCLUDE_ARG=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --vulkan-version)
      VULKAN_VERSION_ARG="${2:-}"
      shift 2
      ;;
    --vulkan-setup-script)
      VULKAN_SETUP_SCRIPT_ARG="${2:-}"
      shift 2
      ;;
    --vulkan-sdk)
      VULKAN_SDK_ARG="${2:-}"
      shift 2
      ;;
    --build-dir)
      BUILD_DIR_ARG="${2:-}"
      shift 2
      ;;
    --build-type)
      BUILD_TYPE_ARG="${2:-}"
      shift 2
      ;;
    --ctest-exclude)
      CTEST_EXCLUDE_ARG="${2:-}"
      shift 2
      ;;
    --)
      shift
      break
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

if [[ -n "${VULKAN_VERSION_ARG}" ]]; then
  VULKAN_VERSION="${VULKAN_VERSION_ARG}"
fi
if [[ -n "${VULKAN_SETUP_SCRIPT_ARG}" ]]; then
  VULKAN_SETUP_SCRIPT="${VULKAN_SETUP_SCRIPT_ARG}"
fi
if [[ -n "${VULKAN_SDK_ARG}" ]]; then
  VULKAN_SDK="${VULKAN_SDK_ARG}"
fi

source_vulkan_env

BUILD_DIR="${BUILD_DIR_ARG:-${BUILD_DIR:-}}"
BUILD_TYPE="${BUILD_TYPE_ARG:-${BUILD_TYPE:-Debug}}"
CTEST_EXCLUDE="${CTEST_EXCLUDE_ARG:-${CTEST_EXCLUDE:-}}"

if [[ -n "${BUILD_DIR}" ]]; then
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
  CTEST_CMD+=( -E "${CTEST_EXCLUDE}" )
fi

CTEST_CMD+=( "$@" )

"${CTEST_CMD[@]}"
