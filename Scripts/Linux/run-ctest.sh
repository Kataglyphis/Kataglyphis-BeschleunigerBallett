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

source_vulkan_env

BUILD_DIR="${BUILD_DIR:-}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
CTEST_EXCLUDE="${CTEST_EXCLUDE:-Integration.VulkanEngine RendererTest.BasicSetup}"

if [[ -n "${BUILD_DIR}" ]]; then
  cd "${BUILD_DIR}"
fi

ctest \
  -C "${BUILD_TYPE}" \
  --verbose \
  --extra-verbose \
  --debug \
  -T test \
  --output-on-failure \
  -E "${CTEST_EXCLUDE}" \
  "$@"
