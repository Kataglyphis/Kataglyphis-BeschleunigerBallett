#!/usr/bin/env bash
set -euo pipefail

# Thin wrapper: the requirements install lives upstream in ContainerHub's
# shared python_uv.sh. uv_pip_install_requirements carries the load-bearing
# `--python .venv/bin/python` pin (uv honours UV_PYTHON over the activated
# venv - see the explanatory comment upstream), so no activation is needed.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
PYTHON_UV_LIB="${REPO_ROOT}/third_party/ContainerHub/linux/scripts/01-core/python_uv.sh"

if [[ ! -f "${PYTHON_UV_LIB}" ]]; then
  echo "ERROR: shared uv helpers not found: ${PYTHON_UV_LIB}" >&2
  echo "       Run: git submodule update --init third_party/ContainerHub" >&2
  exit 1
fi

# shellcheck source=../../../third_party/ContainerHub/linux/scripts/01-core/python_uv.sh
source "${PYTHON_UV_LIB}"

uv_pip_install_requirements "${1:-.venv}" "${2:-requirements.txt}"
