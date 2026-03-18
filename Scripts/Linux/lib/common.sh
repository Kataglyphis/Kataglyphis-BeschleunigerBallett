#!/usr/bin/env bash
# common.sh - shared utilities for BeschleunigerBallett Linux scripts
# Sources utilities from ContainerHub when available, provides fallbacks

SCRIPT_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONTAINER_HUB_CORE="${SCRIPT_LIB_DIR}/../../../ExternalLib/Kataglyphis-ContainerHub/linux/scripts/01-core"

# Import ContainerHub logging if available
if [[ -f "${CONTAINER_HUB_CORE}/logging.sh" ]]; then
  # shellcheck disable=SC1090
  source "${CONTAINER_HUB_CORE}/logging.sh"
else
  # Fallback logging functions
  info() { printf '\033[1;34m[INFO]\033[0m %s\n' "$*"; }
  warn() { printf '\033[1;33m[WARN]\033[0m %s\n' "$*" >&2; }
  err() { printf '\033[1;31m[ERROR]\033[0m %s\n' "$*" >&2; exit 1; }
  die() { err "$@"; }
  log() { info "$@"; }
fi

# Import ContainerHub platform utilities if available
if [[ -f "${CONTAINER_HUB_CORE}/platform.sh" ]]; then
  # shellcheck disable=SC1090
  source "${CONTAINER_HUB_CORE}/platform.sh"
fi

# Import ContainerHub verify utilities if available
if [[ -f "${CONTAINER_HUB_CORE}/verify.sh" ]]; then
  # shellcheck disable=SC1090
  source "${CONTAINER_HUB_CORE}/verify.sh"
fi

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

# Get script root directory
get_script_root() {
  cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd
}

# Get project root directory
get_project_root() {
  cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd
}