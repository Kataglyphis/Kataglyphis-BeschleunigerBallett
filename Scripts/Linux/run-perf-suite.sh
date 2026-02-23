#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-${BUILD_DIR:-build}}"
"${BUILD_DIR}/perfTestSuite"
