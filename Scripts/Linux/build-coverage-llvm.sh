#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR_ARG=""
COVERAGE_JSON_ARG=""

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
			echo "Unbekanntes Argument: $1" >&2
			exit 1
			;;
		*)
			break
			;;
	esac
done

BUILD_DIR="${BUILD_DIR_ARG:-${BUILD_DIR:-build}}"
COVERAGE_JSON="${COVERAGE_JSON_ARG:-${COVERAGE_JSON:-${BUILD_DIR}/coverage.json}}"

llvm-profdata merge -sparse "${BUILD_DIR}/Test/compile/default.profraw" -o "${BUILD_DIR}/compileTestSuite.profdata"
llvm-cov report "${BUILD_DIR}/compileTestSuite" -instr-profile="${BUILD_DIR}/compileTestSuite.profdata"
llvm-cov export "${BUILD_DIR}/compileTestSuite" -format=text -instr-profile="${BUILD_DIR}/compileTestSuite.profdata" > "${COVERAGE_JSON}"
