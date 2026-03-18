#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

git config --global --add safe.directory /workspace || true

DEFAULT_PRESET="linux-debug-clang"
DEFAULT_BUILD_DIR="build"
DEFAULT_CLEAN_BUILD_DIR="false"
DEFAULT_SKIP_CONFIGURE="false"
DEFAULT_USE_THREAD_SANITIZER="false"
DEFAULT_VULKAN_SETUP_SCRIPT="/opt/vulkan/1.4.341.1/setup-env.sh"
DEFAULT_MB_PER_JOB="4000"  # 4GB RAM per parallel job

PARALLEL_JOBS=""

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
    --use-thread-sanitizer)
      if [[ $# -ge 2 && "${2}" != -* ]]; then
        USE_THREAD_SANITIZER_ARG="${2}"
        shift 2
      else
        USE_THREAD_SANITIZER_ARG="true"
        shift
      fi
      ;;
    --parallel)
      PARALLEL_JOBS="${2:-}"
      shift 2
      ;;
    --mb-per-job)
      DEFAULT_MB_PER_JOB="${2:-}"
      shift 2
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
      err "Unknown argument: $1"
      ;;
    *)
      break
      ;;
  esac
done

if [[ -n "${VULKAN_VERSION_ARG:-}" ]]; then
  VULKAN_VERSION="${VULKAN_VERSION_ARG}"
fi
if [[ -n "${VULKAN_SETUP_SCRIPT_ARG:-}" ]]; then
  VULKAN_SETUP_SCRIPT="${VULKAN_SETUP_SCRIPT_ARG}"
fi
if [[ -n "${VULKAN_SDK_ARG:-}" ]]; then
  VULKAN_SDK="${VULKAN_SDK_ARG}"
fi

if [[ -z "${VULKAN_SETUP_SCRIPT_ARG:-}" && -z "${VULKAN_SETUP_SCRIPT:-}" && -f "${DEFAULT_VULKAN_SETUP_SCRIPT}" ]]; then
  VULKAN_SETUP_SCRIPT="${DEFAULT_VULKAN_SETUP_SCRIPT}"
fi

source_vulkan_env

PRESET="${PRESET_ARG:-${PRESET:-${1:-${DEFAULT_PRESET}}}}"
BUILD_DIR="${BUILD_DIR_ARG:-${BUILD_DIR:-${DEFAULT_BUILD_DIR}}}"
CLEAN_BUILD_DIR="${CLEAN_BUILD_DIR_ARG:-${CLEAN_BUILD_DIR:-${DEFAULT_CLEAN_BUILD_DIR}}}"
SKIP_CONFIGURE="${SKIP_CONFIGURE_ARG:-${SKIP_CONFIGURE:-${DEFAULT_SKIP_CONFIGURE}}}"
USE_THREAD_SANITIZER="${USE_THREAD_SANITIZER_ARG:-${USE_THREAD_SANITIZER:-${DEFAULT_USE_THREAD_SANITIZER}}}"
CMAKE_BUILD_CONFIG="${CMAKE_BUILD_CONFIG_ARG:-${CMAKE_BUILD_CONFIG:-}}"
CMAKE_BUILD_TARGET="${CMAKE_BUILD_TARGET_ARG:-${CMAKE_BUILD_TARGET:-}}"

if [[ "${SKIP_CONFIGURE}" != "true" && -z "${PRESET}" ]]; then
  PRESET="${DEFAULT_PRESET}"
fi

if [[ "${CLEAN_BUILD_DIR}" == "true" && -n "${BUILD_DIR}" ]]; then
  info "Cleaning build directory: ${BUILD_DIR}"
  rm -rf "${BUILD_DIR}"
fi

if [[ "${SKIP_CONFIGURE}" != "true" ]]; then
  if [[ -z "${PRESET}" ]]; then
    err "Missing --preset for configure step."
  fi
  info "Configuring CMake with preset: ${PRESET}"
  if [[ -n "${BUILD_DIR}" ]]; then
    cmake -B "${BUILD_DIR}" --preset "${PRESET}"
  else
    cmake --preset "${PRESET}"
  fi
fi

# Compute optimal parallel jobs based on available memory
if [[ -z "${PARALLEL_JOBS}" ]]; then
  PARALLEL_JOBS=$(get_build_jobs "${DEFAULT_MB_PER_JOB}")
  info "Auto-detected parallel jobs: ${PARALLEL_JOBS} (memory-aware)"
else
  info "Using specified parallel jobs: ${PARALLEL_JOBS}"
fi

build_cmd=(cmake --build)
if [[ -n "${BUILD_DIR}" ]]; then
  build_cmd+=("${BUILD_DIR}")
fi
if [[ -n "${PRESET}" && -z "${BUILD_DIR}" ]]; then
  build_cmd+=(--preset "${PRESET}")
fi
if [[ -n "${CMAKE_BUILD_CONFIG}" ]]; then
  build_cmd+=(--config "${CMAKE_BUILD_CONFIG}")
fi
if [[ -n "${CMAKE_BUILD_TARGET}" ]]; then
  build_cmd+=(--target "${CMAKE_BUILD_TARGET}")
fi
build_cmd+=(--parallel "${PARALLEL_JOBS}")

info "Executing: ${build_cmd[*]}"
"${build_cmd[@]}"