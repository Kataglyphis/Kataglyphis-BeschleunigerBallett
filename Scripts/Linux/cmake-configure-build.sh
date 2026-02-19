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

  echo "[cmake-configure-build] Vulkan setup-env.sh nicht gefunden – fahre ohne explizites Sourcing fort."
}

source_vulkan_env

PRESET="${PRESET:-${1:-}}"
BUILD_DIR="${BUILD_DIR:-}"
CLEAN_BUILD_DIR="${CLEAN_BUILD_DIR:-false}"
SKIP_CONFIGURE="${SKIP_CONFIGURE:-false}"
CMAKE_BUILD_CONFIG="${CMAKE_BUILD_CONFIG:-}"
CMAKE_BUILD_TARGET="${CMAKE_BUILD_TARGET:-}"

if [[ "${CLEAN_BUILD_DIR}" == "true" && -n "${BUILD_DIR}" ]]; then
  rm -rf "${BUILD_DIR}"
fi

if [[ "${SKIP_CONFIGURE}" != "true" ]]; then
  if [[ -z "${PRESET}" ]]; then
    echo "PRESET ist erforderlich (Env oder erstes Argument)." >&2
    exit 1
  fi

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
