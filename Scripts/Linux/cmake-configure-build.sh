#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

git config --global --add safe.directory /workspace || true

# The :latest-cross image sets CCACHE_SECONDARY_STORAGE=true, but that variable
# is ccache's remote_storage and must be a URL - ccache parses "true" as one and
# dies with "URL scheme must not be empty: true" on EVERY compile. sccache
# (clang presets) ignores it, so only the gcc presets are hit, which is why the
# gcc lanes - benchmarks (gcc) included - were red for months. Neutralize any
# CCACHE_SECONDARY_STORAGE that is not an actual URL so the deployed image works
# without a rebuild (the env is also removed at source in ContainerHub's
# Dockerfile.package). A real remote URL, if ever set, is left intact.
if [[ -n "${CCACHE_SECONDARY_STORAGE:-}" && "${CCACHE_SECONDARY_STORAGE}" != *"://"* ]]; then
  echo "Ignoring invalid CCACHE_SECONDARY_STORAGE='${CCACHE_SECONDARY_STORAGE}' (not a URL)"
  unset CCACHE_SECONDARY_STORAGE
fi

DEFAULT_PRESET="linux-debug-clang"
DEFAULT_BUILD_DIR="build"
DEFAULT_CLEAN_BUILD_DIR="false"
DEFAULT_SKIP_CONFIGURE="false"
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
      err "--use-thread-sanitizer does nothing (legacy plumbing) — use --preset linux-debug-tsan-clang instead"
      ;;
    --cargo-cache-dir)
      CARGO_CACHE_DIR="${2:-}"
      shift 2
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

# The :latest-cross image runs as uid 1001 with CARGO_HOME=/usr/local/cargo
# owned by root, so the Corrosion/cargo half of the configure dies with
# "failed to create directory /usr/local/cargo/registry" - which took the
# whole Linux lane down when combined with the tee exit-code masking in
# Linux.yml (fixed there with shell: bash / pipefail). Redirect cargo to a
# writable home rather than requiring the image to hand us its own.
#
# Checked in order:
#   1. --cargo-cache-dir  (CLI arg, survives container restarts when backed
#      by a docker named volume or host bind mount)
#   2. $CARGO_CACHE_DIR   (environment variable override)
#   3. $CARGO_HOME        (image default; /usr/local/cargo, usually read-only)
#   4. $TMPDIR/cargo-home (fallback, lost when container exits)
if [[ ! -w "${CARGO_HOME:-/usr/local/cargo}" ]]; then
  if [[ -n "${CARGO_CACHE_DIR:-}" ]]; then
    export CARGO_HOME="${CARGO_CACHE_DIR}"
    # Also redirect the cargo target directory to the same volume (under
    # a subdirectory) so compiled artifacts bypass the 9p host mount which
    # has known permission issues with cargo's temp-file rename operations.
    export CARGO_TARGET_DIR="${CARGO_CACHE_DIR}/target"
    mkdir -p "${CARGO_TARGET_DIR}"
  else
    export CARGO_HOME="${TMPDIR:-/tmp}/cargo-home"
    export CARGO_TARGET_DIR="${CARGO_HOME}/target"
  fi
  mkdir -p "${CARGO_HOME}"
  echo "CARGO_HOME not writable in this image; using ${CARGO_HOME}"
fi

PRESET="${PRESET_ARG:-${PRESET:-${1:-${DEFAULT_PRESET}}}}"
BUILD_DIR="${BUILD_DIR_ARG:-${BUILD_DIR:-${DEFAULT_BUILD_DIR}}}"
CLEAN_BUILD_DIR="${CLEAN_BUILD_DIR_ARG:-${CLEAN_BUILD_DIR:-${DEFAULT_CLEAN_BUILD_DIR}}}"
SKIP_CONFIGURE="${SKIP_CONFIGURE_ARG:-${SKIP_CONFIGURE:-${DEFAULT_SKIP_CONFIGURE}}}"
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

# Compile Slang shaders to SPIR-V (C++) and WGSL (Rust). Runs for ALL builds
# (not just Release) because the C++ code loads Slang-emitted SPIR-V at
# runtime via File I/O, not embedded — the .spv files must exist on disk.
# See docs/shader-sharing.md.
info "Precompiling Slang shaders"
bash "${SCRIPT_DIR}/compile-slang-shaders.sh" || warn "Slang shader precompilation failed"
"${build_cmd[@]}"