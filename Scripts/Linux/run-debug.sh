#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

DEFAULT_BUILD_DIR="build"
DEFAULT_EXE_NAME="GraphicsEngine"
DEFAULT_BUILD_TYPE="Debug"

EXE_NAME="${DEFAULT_EXE_NAME}"
BUILD_DIR="${DEFAULT_BUILD_DIR}"
BUILD_TYPE="${DEFAULT_BUILD_TYPE}"
APP_ARGS=()

usage() {
  cat <<EOF
Usage: $(basename "$0") [--exe-name NAME] [--build-dir DIR] [--build-type TYPE] [--clean-and-rebuild-shaders] [--] [app args...]

Starts the built application from the debug build directory. Defaults:
  --exe-name ${DEFAULT_EXE_NAME}
  --build-dir ${DEFAULT_BUILD_DIR}
  --build-type ${DEFAULT_BUILD_TYPE}
  --clean-and-rebuild-shaders: Deletes all .spv files and runs the compiler script before running
EOF
}

CLEAN_AND_REBUILD_SHADERS=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --exe-name)
      EXE_NAME="${2:-}"
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
    --clean-and-rebuild-shaders)
      CLEAN_AND_REBUILD_SHADERS=true
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      APP_ARGS+=("$@")
      break
      ;;
    -* )
      err "Unknown option: $1"
      ;;
    *)
      APP_ARGS+=("$1")
      shift
      ;;
  esac
done

PROJECT_ROOT="$(get_project_root)"

# Resolve build dir absolute path (accept absolute or repo-relative)
if [[ "${BUILD_DIR}" = /* ]]; then
  ABS_BUILD_DIR="${BUILD_DIR}"
else
  ABS_BUILD_DIR="${PROJECT_ROOT}/${BUILD_DIR}"
fi

source_vulkan_env

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

# Try to install Vulkan SDK using the ContainerHub helper script bundled in ExternalLib
install_vulkan_via_containerhub() {
  local sd
  # Prefer the 02-toolchain helper if present, fallback to top-level helper
  sd="${PROJECT_ROOT}/ExternalLib/Kataglyphis-ContainerHub/linux/scripts/02-toolchain/setup-dependencies.sh"
  if [[ ! -f "${sd}" ]]; then
    sd="${PROJECT_ROOT}/ExternalLib/Kataglyphis-ContainerHub/linux/scripts/setup-dependencies.sh"
  fi
  if [[ ! -f "${sd}" ]]; then
    warn "No ContainerHub setup-dependencies.sh found under ExternalLib/Kataglyphis-ContainerHub; cannot auto-install Vulkan."
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
if ! check_vulkan; then
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
fi

# Candidate locations to look for the executable
CANDIDATES=(
  "${ABS_BUILD_DIR}/${EXE_NAME}"
  "${ABS_BUILD_DIR}/bin/${EXE_NAME}"
  "${ABS_BUILD_DIR}/${BUILD_TYPE}/${EXE_NAME}"
  "${ABS_BUILD_DIR}/bin/${BUILD_TYPE}/${EXE_NAME}"
)

EXE_PATH=""
for c in "${CANDIDATES[@]}"; do
  if [[ -x "${c}" ]]; then
    EXE_PATH="${c}"
    break
  fi
done

if [[ -z "${EXE_PATH}" && -d "${ABS_BUILD_DIR}" ]]; then
  EXE_PATH=$(find "${ABS_BUILD_DIR}" -maxdepth 3 -type f -executable -name "${EXE_NAME}" -print -quit || true)
fi

if [[ -z "${EXE_PATH}" ]]; then
  err "Executable '${EXE_NAME}' not found in '${ABS_BUILD_DIR}'. Please build the project first."
fi

# Ensure runtime loader finds built shared libraries
export LD_LIBRARY_PATH="${ABS_BUILD_DIR}:${ABS_BUILD_DIR}/bin:${LD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH

# Enable Vulkan loader debug output by default (can be overridden externally)
export VK_LOADER_DEBUG="${VK_LOADER_DEBUG:-all}"

if [[ "${CLEAN_AND_REBUILD_SHADERS}" = true ]]; then
  info "Cleaning and rebuilding shaders..."
  find "${PROJECT_ROOT}/Resources/Shaders" -name "*.spv" -delete
  if [[ -f "${SCRIPT_DIR}/compile-shaders.sh" ]]; then
    bash "${SCRIPT_DIR}/compile-shaders.sh"
  else
    warn "compile-shaders.sh not found, skipping rebuild step"
  fi
fi

WORK_DIR="${PROJECT_ROOT}"
info "Starting: ${EXE_PATH}"
info "Working directory: ${WORK_DIR}"
info "LD_LIBRARY_PATH=${LD_LIBRARY_PATH}"
info "VK_LOADER_DEBUG=${VK_LOADER_DEBUG}"

cd "${WORK_DIR}"

if [[ ${#APP_ARGS[@]} -gt 0 ]]; then
  exec "${EXE_PATH}" "${APP_ARGS[@]}"
else
  exec "${EXE_PATH}"
fi
