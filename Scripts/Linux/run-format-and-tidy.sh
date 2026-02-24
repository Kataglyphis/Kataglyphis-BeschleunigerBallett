#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

BUILD_DIR="build"
CLANG_TIDY_FIX="false"

usage() {
  cat <<'EOF'
Usage: run-format-and-tidy.sh [--build-dir <path>] [--fix]

Runs:
  1) cmake-format
  2) clang-format
  3) clang-tidy

Options:
  --build-dir <path>   Build directory with compile_commands.json (default: build)
  --fix                Apply clang-tidy fixes (-fix)
  -h, --help           Show this help
EOF
}

ensure_cmake_format() {
  if command -v cmake-format >/dev/null 2>&1; then
    return
  fi

  if ! command -v uv >/dev/null 2>&1; then
    echo "Required tool not found: uv (needed to manage .venv and install requirements)" >&2
    exit 1
  fi

  echo "cmake-format not found. Preparing Python environment..."

  if [[ -d "${ROOT_DIR}/.venv" ]]; then
    echo "Found .venv - activating and installing requirements..."
  else
    echo "No .venv found - creating one with uv..."
    uv venv "${ROOT_DIR}/.venv"
  fi

  # shellcheck disable=SC1091
  source "${ROOT_DIR}/.venv/bin/activate"
  uv pip install -r "${ROOT_DIR}/requirements.txt"

  if ! command -v cmake-format >/dev/null 2>&1; then
    echo "cmake-format is still not available after installing requirements." >&2
    exit 1
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR="${2:-}"
      shift 2
      ;;
    --fix)
      CLANG_TIDY_FIX="true"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

ensure_cmake_format

for tool in cmake-format clang-format clang-tidy; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Required tool not found: $tool" >&2
    exit 1
  fi
done

cd "${ROOT_DIR}"

if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
  echo "Missing ${BUILD_DIR}/compile_commands.json" >&2
  echo "Run CMake configure first, e.g. Scripts/Linux/cmake-configure-build.sh --build-dir ${BUILD_DIR} --preset <preset>" >&2
  exit 1
fi

echo "[1/3] Running cmake-format..."
mapfile -t cmake_files < <(find . \
  -type f \( -name 'CMakeLists.txt' -o -name '*.cmake' \) \
  -not -path './build/*' \
  -not -path './build-release/*' \
  -not -path './ExternalLib/*')

if [[ ${#cmake_files[@]} -gt 0 ]]; then
  cmake-format -c .cmake-format.yaml -i "${cmake_files[@]}"
fi

echo "[2/3] Running clang-format..."
mapfile -t cpp_files < <(find Src Test \
  -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' -o -name '*.ixx' -o -name '*.cppm' -o -name '*.ccm' -o -name '*.cxxm' -o -name '*.mpp' \))

if [[ ${#cpp_files[@]} -gt 0 ]]; then
  clang-format -i "${cpp_files[@]}"
fi

echo "[3/3] Running clang-tidy..."
clang_tidy_args=( -p "${BUILD_DIR}" )
if [[ "${CLANG_TIDY_FIX}" == "true" ]]; then
  clang_tidy_args+=( -fix )
fi

if [[ ${#cpp_files[@]} -gt 0 ]]; then
  clang-tidy "${clang_tidy_args[@]}" "${cpp_files[@]}"
fi

echo "Done: cmake-format, clang-format and clang-tidy completed."
