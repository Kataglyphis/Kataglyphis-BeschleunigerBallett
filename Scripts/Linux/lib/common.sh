#!/usr/bin/env bash
# common.sh - shared utilities for BeschleunigerBallett Linux scripts
# Sources utilities from ContainerHub when available, provides fallbacks

SCRIPT_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONTAINER_HUB_CORE="${SCRIPT_LIB_DIR}/../../../ExternalLib/Kataglyphis-ContainerHub/linux/scripts/01-core"

# Source module loader (mirrors ContainerHub pattern)
source_module() {
  local name="$1"
  if [[ -z "${name}" ]]; then
    echo "[ERROR] source_module requires a filename" >&2
    return 1
  fi

  local candidates=(
    "${SCRIPT_LIB_DIR}/${name}"
    "${CONTAINER_HUB_CORE}/${name}"
    "${SCRIPT_LIB_DIR}/../${name}"
    "/opt/scripts/core/${name}"
  )

  for c in "${candidates[@]}"; do
    if [[ -f "${c}" ]]; then
      # shellcheck disable=SC1090
      source "${c}"
      return 0
    fi
  done

  echo "[ERROR] required module '${name}' not found (searched: ${candidates[*]})" >&2
  return 1
}

# Import ContainerHub logging via source_module pattern
if source_module logging.sh 2>/dev/null; then
  : # Successfully sourced
else
  # Fallback logging functions
  info() { printf '\033[1;34m[INFO]\033[0m %s\n' "$*"; }
  warn() { printf '\033[1;33m[WARN]\033[0m %s\n' "$*" >&2; }
  err() { printf '\033[1;31m[ERROR]\033[0m %s\n' "$*" >&2; exit 1; }
  die() { err "$@"; }
  log() { info "$@"; }
fi

# Import ContainerHub platform utilities via source_module pattern
source_module platform.sh 2>/dev/null || true

# Import ContainerHub verify utilities via source_module pattern
source_module verify.sh 2>/dev/null || true

# Import ContainerHub parallelism utilities via source_module pattern
source_module parallelism.sh 2>/dev/null || true

# Standard Vulkan environment sourcing (shared across all scripts)
source_vulkan_env() {
  if [[ -n "${VULKAN_SETUP_SCRIPT:-}" && -f "${VULKAN_SETUP_SCRIPT}" ]]; then
    info "Sourcing Vulkan env from: ${VULKAN_SETUP_SCRIPT}"
    # shellcheck disable=SC1090
    . "${VULKAN_SETUP_SCRIPT}"
    return 0
  fi

  if [[ -n "${VULKAN_VERSION:-}" ]]; then
    if [[ -f "/opt/vulkan/${VULKAN_VERSION}/setup-env.sh" ]]; then
      info "Sourcing Vulkan env from /opt/vulkan/${VULKAN_VERSION}/setup-env.sh"
      . "/opt/vulkan/${VULKAN_VERSION}/setup-env.sh"
      return 0
    fi
    if [[ -f "${HOME}/vulkan/${VULKAN_VERSION}/setup-env.sh" ]]; then
      info "Sourcing Vulkan env from ${HOME}/vulkan/${VULKAN_VERSION}/setup-env.sh"
      . "${HOME}/vulkan/${VULKAN_VERSION}/setup-env.sh"
      return 0
    fi
  fi

  if [[ -n "${VULKAN_SDK:-}" && -f "${VULKAN_SDK}/setup-env.sh" ]]; then
    info "Sourcing Vulkan env from \${VULKAN_SDK}/setup-env.sh"
    . "${VULKAN_SDK}/setup-env.sh"
    return 0
  fi

  if command -v glslc >/dev/null 2>&1; then
    info "glslc found in PATH, skipping explicit Vulkan env sourcing"
    return 0
  fi

  warn "Vulkan setup-env.sh not found – continuing without explicit sourcing"
  return 0
}

# Ensure required tools are installed
require_tools() {
  local missing=()
  for tool in "$@"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      missing+=("$tool")
    fi
  done
  
  if [[ ${#missing[@]} -gt 0 ]]; then
    err "Required tools not found: ${missing[*]}"
  fi
}

# Check if tool exists (without error)
has_tool() {
  command -v "$1" >/dev/null 2>&1
}

# Compute optimal parallel jobs (with memory cap)
# Falls back to nproc if parallelism.sh not available
get_build_jobs() {
  local mb_per_job="${1:-4000}"  # Default: 4GB per job
  
  if declare -f compute_jobs_with_mem_cap &>/dev/null; then
    compute_jobs_with_mem_cap "" "${mb_per_job}"
  else
    local cores
    cores=$(nproc --all 2>/dev/null || echo 1)
    echo "${cores}"
  fi
}

# Get script root directory
get_script_root() {
  cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd
}

# Get project root directory
get_project_root() {
  cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd
}

# Source module from ContainerHub's specific category (optional advanced use)
source_hub_module() {
  local category="$1"
  local name="$2"
  local path="${SCRIPT_LIB_DIR}/../../../ExternalLib/Kataglyphis-ContainerHub/linux/scripts/${category}/${name}"
  if [[ -f "${path}" ]]; then
    # shellcheck disable=SC1090
    source "${path}"
    return 0
  fi
  return 1
}