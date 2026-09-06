#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

APP_RUNNER_LIB="${SCRIPT_DIR}/../../third_party/ContainerHub/linux/scripts/lib/app-runner.sh"
if [[ ! -f "${APP_RUNNER_LIB}" ]]; then
  err "Shared app-runner library not found at '${APP_RUNNER_LIB}'. Initialize the ContainerHub submodule first."
fi
# shellcheck source=../../third_party/ContainerHub/linux/scripts/lib/app-runner.sh
source "${APP_RUNNER_LIB}"

APP_RUNNER_DEFAULT_EXE_NAME="GraphicsEngine"
APP_RUNNER_DEFAULT_BUILD_DIR="build"
APP_RUNNER_DEFAULT_BUILD_TYPE="Debug"
APP_RUNNER_USAGE_INTRO="Starts the built application from the debug build directory."
APP_RUNNER_ENABLE_SHADER_CLEAN=true
APP_RUNNER_SHADER_CLEAN_DIR="Resources/ShadersSlang/build"
APP_RUNNER_SHADER_COMPILE_SCRIPT="${SCRIPT_DIR}/compile-slang-shaders.sh"

# Helper: check whether Vulkan SDK/tools are available in the current environment
check_vulkan() {
  # common detections: glslc or vulkaninfo in PATH
  if command -v glslc >/dev/null 2>&1; then
    return 0
  fi
  if command -v vulkaninfo >/dev/null 2>&1; then
    return 0
  fi
  # VULKAN_SDK env pointing to a setup script
  if [[ -n "${VULKAN_SDK:-}" && -f "${VULKAN_SDK}/setup-env.sh" ]]; then
    return 0
  fi
  # look for any /opt/vulkan/*/setup-env.sh or ~/vulkan/<version>/setup-env.sh
  shopt -s nullglob >/dev/null 2>&1 || true
  local candidates=(/opt/vulkan/*/setup-env.sh "${HOME}/vulkan/*/setup-env.sh")
  shopt -u nullglob >/dev/null 2>&1 || true
  for c in "${candidates[@]}"; do
    if [[ -f "${c}" ]]; then
      return 0
    fi
  done
  return 1
}

# Try to install Vulkan SDK using the ContainerHub helper script bundled in third_party
install_vulkan_via_containerhub() {
  local sd
  # Prefer the 02-toolchain helper if present, fallback to top-level helper
  sd="${PROJECT_ROOT}/third_party/ContainerHub/linux/scripts/02-toolchain/setup-dependencies.sh"
  if [[ ! -f "${sd}" ]]; then
    sd="${PROJECT_ROOT}/third_party/ContainerHub/linux/scripts/setup-dependencies.sh"
  fi
  if [[ ! -f "${sd}" ]]; then
    warn "No ContainerHub setup-dependencies.sh found under third_party/ContainerHub; cannot auto-install Vulkan."
    return 1
  fi

  local ver="${VULKAN_VERSION:-1.4.341.1}"
  info "Attempting to install Vulkan SDK ${ver} using ${sd} (may require sudo and take several minutes)"
  # Run the helper; it contains its own privilege escalation where needed
  if bash "${sd}" --vulkan-version "${ver}" vulkan; then
    info "ContainerHub helper finished (attempted Vulkan install)."
    return 0
  else
    warn "ContainerHub helper returned non-zero while attempting Vulkan install."
    return 2
  fi
}

# If Vulkan wasn't detected, attempt auto-install then source the installed SDK
app_runner_post_vulkan_hook() {
  if check_vulkan; then
    return 0
  fi

  info "Vulkan SDK/tools not detected on PATH. Attempting automatic install..."
  if install_vulkan_via_containerhub; then
    # Prefer explicit version install location, then fallback to first found
    VER_TO_SOURCE="${VULKAN_VERSION:-1.4.341.1}"
    if [[ -f "/opt/vulkan/${VER_TO_SOURCE}/setup-env.sh" ]]; then
      info "Sourcing Vulkan env from /opt/vulkan/${VER_TO_SOURCE}/setup-env.sh"
      # shellcheck disable=SC1090
      . "/opt/vulkan/${VER_TO_SOURCE}/setup-env.sh"
    elif [[ -f "${HOME}/vulkan/${VER_TO_SOURCE}/setup-env.sh" ]]; then
      info "Sourcing Vulkan env from ${HOME}/vulkan/${VER_TO_SOURCE}/setup-env.sh"
      . "${HOME}/vulkan/${VER_TO_SOURCE}/setup-env.sh"
    else
      # fallback: source the first matching setup-env.sh under /opt/vulkan
      shopt -s nullglob >/dev/null 2>&1 || true
      found_setup=""
      for s in /opt/vulkan/*/setup-env.sh; do
        if [[ -f "${s}" ]]; then
          found_setup="${s}"
          break
        fi
      done
      shopt -u nullglob >/dev/null 2>&1 || true
      if [[ -n "${found_setup}" ]]; then
        info "Sourcing Vulkan env from ${found_setup}"
        . "${found_setup}"
      else
        warn "Vulkan installation completed but no setup-env.sh found to source. You may need to source it manually."
      fi
    fi
  else
    warn "Automatic Vulkan installation failed or was not available. Continue at your own risk."
  fi
}

app_runner_env_hook() {
  export LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH

  # Enable Vulkan loader debug output by default (can be overridden externally)
  export VK_LOADER_DEBUG="${VK_LOADER_DEBUG:-all}"
  info "VK_LOADER_DEBUG=${VK_LOADER_DEBUG}"
}

app_runner_main "$@"
