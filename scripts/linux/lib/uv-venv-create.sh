#!/usr/bin/env bash
set -euo pipefail

# Thin wrapper: the uv venv logic lives upstream in ContainerHub's shared
# python_uv.sh so every project uses one implementation. Creates ./.venv in
# the caller's working directory, like the plain `uv venv` this replaced.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
PYTHON_UV_LIB="${REPO_ROOT}/ExternalLib/Kataglyphis-ContainerHub/linux/scripts/01-core/python_uv.sh"

if [[ ! -f "${PYTHON_UV_LIB}" ]]; then
  echo "ERROR: shared uv helpers not found: ${PYTHON_UV_LIB}" >&2
  echo "       Run: git submodule update --init ExternalLib/Kataglyphis-ContainerHub" >&2
  exit 1
fi

# shellcheck source=../../../ExternalLib/Kataglyphis-ContainerHub/linux/scripts/01-core/python_uv.sh
source "${PYTHON_UV_LIB}"

# Empty python version on purpose: let uv resolve the interpreter (honouring
# UV_PYTHON exported by the CI containers) exactly like the previous plain
# `uv venv` call did, instead of pinning python_uv.sh's default version.
uv_venv_create "${1:-.venv}" ""
