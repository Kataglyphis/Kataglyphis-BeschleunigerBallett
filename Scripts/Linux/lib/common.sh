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

# Import the ContainerHub Vulkan env resolver via source_module pattern.
# It carries the union of the search strategies this file used to implement
# inline (explicit $VULKAN_SETUP_SCRIPT, $VULKAN_VERSION under /opt/vulkan and
# ~/vulkan, the arch-subdirectory glob, $VULKAN_SDK/setup-env.sh, the plain
# /opt/vulkan/*/setup-env.sh sweep and the glslc-on-PATH short circuit) plus the
# prefix handling from 02-toolchain/vulkan.sh, and it is dependency-free: it
# does NOT drag in downloads.sh or a file-scope `set -euo pipefail`.
source_module vulkan-env.sh 2>/dev/null || true

# Standard Vulkan environment sourcing (shared across all scripts).
# Non-strict on purpose (strict=0): a dev box without an installed SDK must warn
# and still proceed, unlike the image-side source_vulkan_sdk_env which returns 1.
source_vulkan_env() {
  if declare -F vulkan_env_source >/dev/null 2>&1; then
    vulkan_env_source "" "keep-libs" 0
    return 0
  fi

  warn "vulkan-env.sh module not found – continuing without explicit sourcing"
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
  # common.sh lives in Scripts/Linux/lib; go three levels up to reach repo root
  cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd
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
# Make the wasm32-unknown-unknown target usable, without assuming rustup.
#
# The CI image installs Rust WITHOUT rustup: cargo and rustc are the distro
# toolchain (/bin/cargo, sysroot /usr/lib/rust-1.x) and /usr/local/cargo/bin
# only symlinks them, so `rustup target add` exits 127 - "command not found".
# Under `set -e` that killed the wasm size-budget step before it measured a
# single byte (2026-08-06), and it silently short-circuited the docs demo
# rebuild's `&&` chain for good measure. The image used to have rustup - the
# demo was rebuilt by CI on 2026-07-23 - so this is an image regression to fix
# in ContainerHub's install-rust.sh; until then neither caller should die of it.
#
# Returns 0 when a wasm32 build can proceed, 1 when the toolchain simply cannot
# target wasm. Callers decide what that means for them.
ensure_wasm32_target() {
  if has_tool rustup; then
    rustup target add wasm32-unknown-unknown
    return $?
  fi

  # No rustup: ask rustc directly whether std for the target is installed.
  # --print target-libdir names the directory even when it does not exist, so
  # the existence check is the actual probe.
  local libdir
  if libdir="$(rustc --print target-libdir --target wasm32-unknown-unknown 2>/dev/null)" \
     && [ -d "${libdir}" ]; then
    info "rustup not present; wasm32-unknown-unknown std already installed (${libdir})"
    return 0
  fi

  warn "rustup is not installed AND this toolchain has no wasm32-unknown-unknown std"
  warn "(rustc sysroot: $(rustc --print sysroot 2>/dev/null || echo unknown)) - cannot build wasm here"
  return 1
}
