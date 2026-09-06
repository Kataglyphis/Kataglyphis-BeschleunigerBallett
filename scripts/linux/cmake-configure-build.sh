#!/usr/bin/env bash
# cmake-configure-build.sh - project wrapper around ContainerHub's generic
# CMake build driver. Everything reusable (arg parsing, cargo/ccache/sccache
# writability fallbacks, Vulkan env, parallelism, configure+build) lives in
# third_party/ContainerHub/linux/scripts/lib/cmake-build.sh; only
# this project's defaults and its Slang pre-build step live here.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

CMAKE_BUILD_LIB="${SCRIPT_DIR}/../../third_party/ContainerHub/linux/scripts/lib/cmake-build.sh"
if [[ ! -f "${CMAKE_BUILD_LIB}" ]]; then
  err "Shared cmake-build library not found at '${CMAKE_BUILD_LIB}'. Initialize the ContainerHub submodule first."
fi
# shellcheck source=../../third_party/ContainerHub/linux/scripts/lib/cmake-build.sh
source "${CMAKE_BUILD_LIB}"

CMAKE_BUILD_DEFAULT_PRESET="linux-debug-clang"
CMAKE_BUILD_DEFAULT_BUILD_DIR="build"
CMAKE_BUILD_DEFAULT_VULKAN_SETUP_SCRIPT="/opt/vulkan/1.4.341.1/setup-env.sh"
CMAKE_BUILD_DEFAULT_MB_PER_JOB="4000"  # 4GB RAM per parallel job
CMAKE_BUILD_PREBUILD_LABEL="Slang shader precompilation"
CMAKE_BUILD_USAGE_INTRO="Configures and builds BeschleunigerBallett inside the Linux container image."

# Compile Slang shaders to SPIR-V (C++) and WGSL (Rust). Runs for ALL builds
# (not just Release) because the C++ code loads Slang-emitted SPIR-V at
# runtime via File I/O, not embedded — the .spv files must exist on disk.
# See docs/shader-sharing.md.
#
# A failure here is fatal (cmake-build.sh default). This call used to end in
# `|| warn "Slang shader precompilation failed"`, which hid a real failure and
# left CI with a green build and no SPIR-V at all. Use
# --allow-prebuild-failure if a build genuinely has to continue without shaders.
cmake_build_prebuild_hook() {
  bash "${SCRIPT_DIR}/compile-slang-shaders.sh"
}

cmake_build_main "$@"
