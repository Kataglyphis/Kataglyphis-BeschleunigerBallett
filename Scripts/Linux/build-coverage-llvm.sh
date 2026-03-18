#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR_ARG="${2:-}"
      shift 2
      ;;
    --coverage-json)
      COVERAGE_JSON_ARG="${2:-}"
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

BUILD_DIR="${BUILD_DIR_ARG:-${BUILD_DIR:-build}}"
COVERAGE_JSON="${COVERAGE_JSON_ARG:-${COVERAGE_JSON:-${BUILD_DIR}/coverage.json}}"

require_tools llvm-profdata llvm-cov

PROFRAW="${BUILD_DIR}/Test/compile/default.profraw"
if [[ ! -f "${PROFRAW}" ]]; then
  err "Profile data not found at ${PROFRAW}. Run tests first to generate coverage data."
fi

TEST_SUITE="${BUILD_DIR}/compileTestSuite"
if [[ ! -f "${TEST_SUITE}" ]]; then
  err "Test executable not found at ${TEST_SUITE}. Build the project first."
fi

info "Merging profile data from ${PROFRAW}"
llvm-profdata merge -sparse "${PROFRAW}" -o "${BUILD_DIR}/compileTestSuite.profdata"

info "Generating coverage report"
llvm-cov report "${TEST_SUITE}" -instr-profile="${BUILD_DIR}/compileTestSuite.profdata"

info "Exporting coverage to JSON: ${COVERAGE_JSON}"
llvm-cov export "${TEST_SUITE}" -format=text -instr-profile="${BUILD_DIR}/compileTestSuite.profdata" > "${COVERAGE_JSON}"