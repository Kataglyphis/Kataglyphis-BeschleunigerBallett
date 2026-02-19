#!/usr/bin/env bash
set -euo pipefail

clang++ --analyze -DUSE_RUST=1 -Xanalyzer -analyzer-output=html $(find Src -name "*.cpp" -o -name "*.cc")
