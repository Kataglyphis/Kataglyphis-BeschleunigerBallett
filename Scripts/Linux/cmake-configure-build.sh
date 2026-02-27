#!/usr/bin/env bash
set -euo pipefail

git config --global --add safe.directory /workspace || true

DEFAULT_PRESET="linux-debug-clang"
DEFAULT_BUILD_DIR="build"
DEFAULT_CLEAN_BUILD_DIR="false"
DEFAULT_SKIP_CONFIGURE="false"
DEFAULT_VULKAN_SETUP_SCRIPT="/opt/vulkan/1.4.341.1/setup-env.sh"

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

  if command -v glslc >/dev/null 2>&1; then
    return
  fi

  echo "[cmake-configure-build] Vulkan setup-env.sh nicht gefunden – fahre ohne explizites Sourcing fort."
}

PRESET_ARG=""
BUILD_DIR_ARG=""
CLEAN_BUILD_DIR_ARG=""
SKIP_CONFIGURE_ARG=""
CMAKE_BUILD_CONFIG_ARG=""
CMAKE_BUILD_TARGET_ARG=""
VULKAN_VERSION_ARG=""
VULKAN_SETUP_SCRIPT_ARG=""
VULKAN_SDK_ARG=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      PRESET_ARG="${2:-}"
      shift 2
      ;;
    --build-dir)
      BUILD_DIR_ARG="${2:-}"
      shift 2
      ;;
    --clean-build-dir)
      CLEAN_BUILD_DIR_ARG="${2:-}"
      shift 2
      ;;
    --skip-configure)
      if [[ $# -ge 2 && "${2}" != -* ]]; then
        SKIP_CONFIGURE_ARG="${2}"
        shift 2
      else
        SKIP_CONFIGURE_ARG="true"
        shift
      fi
      ;;
    --build-config)
      CMAKE_BUILD_CONFIG_ARG="${2:-}"
      shift 2
      ;;
    --build-target)
      CMAKE_BUILD_TARGET_ARG="${2:-}"
      shift 2
      ;;
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

if [[ -z "${VULKAN_SETUP_SCRIPT_ARG}" && -z "${VULKAN_SETUP_SCRIPT:-}" && -f "${DEFAULT_VULKAN_SETUP_SCRIPT}" ]]; then
  VULKAN_SETUP_SCRIPT="${DEFAULT_VULKAN_SETUP_SCRIPT}"
fi

source_vulkan_env

PRESET="${PRESET_ARG:-${PRESET:-${1:-${DEFAULT_PRESET}}}}"
BUILD_DIR="${BUILD_DIR_ARG:-${BUILD_DIR:-${DEFAULT_BUILD_DIR}}}"
CLEAN_BUILD_DIR="${CLEAN_BUILD_DIR_ARG:-${CLEAN_BUILD_DIR:-${DEFAULT_CLEAN_BUILD_DIR}}}"
SKIP_CONFIGURE="${SKIP_CONFIGURE_ARG:-${SKIP_CONFIGURE:-${DEFAULT_SKIP_CONFIGURE}}}"
CMAKE_BUILD_CONFIG="${CMAKE_BUILD_CONFIG_ARG:-${CMAKE_BUILD_CONFIG:-}}"
CMAKE_BUILD_TARGET="${CMAKE_BUILD_TARGET_ARG:-${CMAKE_BUILD_TARGET:-}}"

if [[ "${CLEAN_BUILD_DIR}" == "true" && -n "${BUILD_DIR}" ]]; then
  rm -rf "${BUILD_DIR}"
fi

if [[ "${SKIP_CONFIGURE}" != "true" ]]; then
  if [[ -n "${BUILD_DIR}" ]]; then
    cmake -B "${BUILD_DIR}" --preset "${PRESET}"
  else
    cmake --preset "${PRESET}"
  fi
fi

build_cmd=(cmake --build)
if [[ -n "${BUILD_DIR}" ]]; then
  build_cmd+=("${BUILD_DIR}")
fi
if [[ -n "${PRESET}" ]]; then
  build_cmd+=(--preset "${PRESET}")
fi
if [[ -n "${CMAKE_BUILD_CONFIG}" ]]; then
  build_cmd+=(--config "${CMAKE_BUILD_CONFIG}")
fi
if [[ -n "${CMAKE_BUILD_TARGET}" ]]; then
  build_cmd+=(--target "${CMAKE_BUILD_TARGET}")
fi

"${build_cmd[@]}"
