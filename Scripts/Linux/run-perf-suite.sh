#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
"${BUILD_DIR}/perfTestSuite"
