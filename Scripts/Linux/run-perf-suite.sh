#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

BUILD_DIR="${1:-${BUILD_DIR:-build}}"

if [[ ! -f "${BUILD_DIR}/perfTestSuite" ]]; then
  err "Performance test suite not found at ${BUILD_DIR}/perfTestSuite. Build the project first."
fi

info "Running performance test suite from ${BUILD_DIR}"
"${BUILD_DIR}/perfTestSuite"