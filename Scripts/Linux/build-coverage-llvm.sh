#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
COVERAGE_JSON="${COVERAGE_JSON:-${BUILD_DIR}/coverage.json}"

llvm-profdata merge -sparse "${BUILD_DIR}/Test/compile/default.profraw" -o "${BUILD_DIR}/compileTestSuite.profdata"
llvm-cov report "${BUILD_DIR}/compileTestSuite" -instr-profile="${BUILD_DIR}/compileTestSuite.profdata"
llvm-cov export "${BUILD_DIR}/compileTestSuite" -format=text -instr-profile="${BUILD_DIR}/compileTestSuite.profdata" > "${COVERAGE_JSON}"
