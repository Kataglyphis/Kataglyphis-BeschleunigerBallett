#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

DEFAULT_BUILD_DIR="build"
DEFAULT_EXE_NAME="GraphicsEngine"
DEFAULT_BUILD_TYPE="RelWithDebInfo"

EXE_NAME="${DEFAULT_EXE_NAME}"
BUILD_DIR="${DEFAULT_BUILD_DIR}"
BUILD_TYPE="${DEFAULT_BUILD_TYPE}"
APP_ARGS=()

usage() {
  cat <<EOF
Usage: $(basename "$0") [--exe-name NAME] [--build-dir DIR] [--build-type TYPE] [--] [app args...]

Starts the built application from the profile build directory. Defaults:
  --exe-name ${DEFAULT_EXE_NAME}
  --build-dir ${DEFAULT_BUILD_DIR}
  --build-type ${DEFAULT_BUILD_TYPE}
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --exe-name)
      EXE_NAME="${2:-}"
      shift 2
      ;;
    --build-dir)
      BUILD_DIR="${2:-}"
      shift 2
      ;;
    --build-type)
      BUILD_TYPE="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      APP_ARGS+=("$@")
      break
      ;;
    -* )
      err "Unknown option: $1"
      ;;
    *)
      APP_ARGS+=("$1")
      shift
      ;;
  esac
done

PROJECT_ROOT="$(get_project_root)"

# Resolve build dir absolute path (accept absolute or repo-relative)
if [[ "${BUILD_DIR}" = /* ]]; then
  ABS_BUILD_DIR="${BUILD_DIR}"
else
  ABS_BUILD_DIR="${PROJECT_ROOT}/${BUILD_DIR}"
fi

source_vulkan_env

# Candidate locations to look for the executable
CANDIDATES=(
  "${ABS_BUILD_DIR}/${EXE_NAME}"
  "${ABS_BUILD_DIR}/bin/${EXE_NAME}"
  "${ABS_BUILD_DIR}/${BUILD_TYPE}/${EXE_NAME}"
  "${ABS_BUILD_DIR}/bin/${BUILD_TYPE}/${EXE_NAME}"
)

EXE_PATH=""
for c in "${CANDIDATES[@]}"; do
  if [[ -x "${c}" ]]; then
    EXE_PATH="${c}"
    break
  fi
done

if [[ -z "${EXE_PATH}" && -d "${ABS_BUILD_DIR}" ]]; then
  EXE_PATH=$(find "${ABS_BUILD_DIR}" -maxdepth 3 -type f -executable -name "${EXE_NAME}" -print -quit || true)
fi

if [[ -z "${EXE_PATH}" ]]; then
  err "Executable '${EXE_NAME}' not found in '${ABS_BUILD_DIR}'. Please build the project first."
fi

# Ensure runtime loader finds built shared libraries
export LD_LIBRARY_PATH="${ABS_BUILD_DIR}:${ABS_BUILD_DIR}/bin:${LD_LIBRARY_PATH:-}"

WORK_DIR="${PROJECT_ROOT}"
info "Starting (profile): ${EXE_PATH}"
info "Working directory: ${WORK_DIR}"
info "LD_LIBRARY_PATH=${LD_LIBRARY_PATH}"

cd "${WORK_DIR}"

if [[ ${#APP_ARGS[@]} -gt 0 ]]; then
  exec "${EXE_PATH}" "${APP_ARGS[@]}"
else
  exec "${EXE_PATH}"
fi
