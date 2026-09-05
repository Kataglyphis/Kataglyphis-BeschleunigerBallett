#!/usr/bin/env bash
# run-ctest.sh - project wrapper around ContainerHub's generic ctest runner.
# Everything reusable (arg parsing, git safe.directory, Vulkan env, the ctest
# verbosity/-T test flag set and the --ctest-exclude plumbing) lives in
# third_party/ContainerHub/linux/scripts/lib/ctest-run.sh; only this
# project's defaults live here.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

CTEST_RUN_LIB="${SCRIPT_DIR}/../../third_party/ContainerHub/linux/scripts/lib/ctest-run.sh"
if [[ ! -f "${CTEST_RUN_LIB}" ]]; then
  err "Shared ctest-run library not found at '${CTEST_RUN_LIB}'. Initialize the Kataglyphis-ContainerHub submodule first."
fi
# shellcheck source=../../third_party/ContainerHub/linux/scripts/lib/ctest-run.sh
source "${CTEST_RUN_LIB}"

CTEST_RUN_DEFAULT_BUILD_DIR="build"
CTEST_RUN_DEFAULT_BUILD_TYPE="Debug"
CTEST_RUN_USAGE_INTRO="Runs the BeschleunigerBallett test suite inside the Linux container image."

# No default --ctest-exclude: which suites are GPU/device-dependent differs per
# lane (Linux.yml excludes Integration/GoldenRender and the shader-freshness
# check on the headless runners), so the exclusion stays an explicit CI
# argument rather than a silent default here.

ctest_run_main "$@"
