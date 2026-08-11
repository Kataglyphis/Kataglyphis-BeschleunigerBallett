#!/usr/bin/env bash
# build-coverage-gcovr.sh - project wrapper around ContainerHub's generic
# coverage driver (linux/scripts/lib/coverage.sh). Only this project's report
# root and exclusion filters live here.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

COVERAGE_LIB="${SCRIPT_DIR}/../../ExternalLib/Kataglyphis-ContainerHub/linux/scripts/lib/coverage.sh"
if [[ ! -f "${COVERAGE_LIB}" ]]; then
  err "Shared coverage library not found at '${COVERAGE_LIB}'. Initialize the Kataglyphis-ContainerHub submodule first."
fi
# shellcheck source=../../ExternalLib/Kataglyphis-ContainerHub/linux/scripts/lib/coverage.sh
source "${COVERAGE_LIB}"

# gcovr walks the compile directory for .gcda/.gcno, which for this project is
# the repo root the container builds from.
GCOVR_ROOT="${GCOVR_ROOT:-.}"

# No exclusion filters applied today; add regexes here (e.g. 'ExternalLib/.*')
# rather than in the shared library.
COVERAGE_GCOVR_EXCLUDES=()

coverage_run_gcovr "${GCOVR_ROOT}"
