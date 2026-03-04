#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

BUILD_DIR="build"
PRESET=""
SCAN_BUILD_OUT="scan-build-reports"
CLANG_TIDY_FIX="false"
RUN_CLANG_ANALYZE_HTML="false"

RUN_FORMAT_AND_TIDY="true"
RUN_SCAN_BUILD="true"

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

run_format_and_tidy() {
  ensure_cmake_format

  for tool in cmake-format clang-format clang-tidy; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      echo "Required tool not found: $tool" >&2
      exit 1
    fi
  done

  if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
    echo "Missing ${BUILD_DIR}/compile_commands.json" >&2
    echo "Run CMake configure first, e.g. Scripts/Linux/cmake-configure-build.sh --build-dir ${BUILD_DIR} --preset <preset>" >&2
    exit 1
  fi

  mapfile -t cmake_files < <(find . \
    -type f \( -name 'CMakeLists.txt' -o -name '*.cmake' \) \
    -not -path './build/*' \
    -not -path './build-release/*' \
    -not -path './ExternalLib/*')

  if [[ ${#cmake_files[@]} -gt 0 ]]; then
    cmake-format -c .cmake-format.yaml -i "${cmake_files[@]}"
  fi

  local cpp_search_dirs=()
  if [[ -d "Src" ]]; then
    cpp_search_dirs+=("Src")
  fi
  if [[ -d "Test" ]]; then
    cpp_search_dirs+=("Test")
  fi

  local cpp_files=()
  if [[ ${#cpp_search_dirs[@]} -gt 0 ]]; then
    mapfile -t cpp_files < <(find "${cpp_search_dirs[@]}" \
      -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' -o -name '*.ixx' -o -name '*.cppm' -o -name '*.ccm' -o -name '*.cxxm' -o -name '*.mpp' \))
  fi

  local clang_tidy_files=()
  if [[ ${#cpp_search_dirs[@]} -gt 0 ]]; then
    mapfile -t clang_tidy_files < <(find "${cpp_search_dirs[@]}" \
      -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \))
  fi

  if [[ ${#cpp_files[@]} -gt 0 ]]; then
    clang-format -i "${cpp_files[@]}"
  fi

  local clang_tidy_build_dir="${BUILD_DIR}"
  local compile_db_path="${BUILD_DIR}/compile_commands.json"
  local temp_db_dir=""

  # If the compile DB was generated in a container (/workspace), remap paths for local runs.
  if grep -q '/workspace' "${compile_db_path}"; then
    temp_db_dir="$(mktemp -d)"
    sed "s#\"/workspace#\"${ROOT_DIR}#g" "${compile_db_path}" > "${temp_db_dir}/compile_commands.json"

    # Drop container-only GCC toolchain flags if that toolchain path is unavailable locally.
    if [[ ! -d "/opt/gcc-15.2.0" ]]; then
      sed -E -i \
        -e 's#[[:space:]]--gcc-toolchain=/opt/gcc-[^[:space:]"]+##g' \
        -e 's#-Wl,-rpath,/opt/gcc-[^[:space:]"]+/lib64##g' \
        -e 's#[[:space:]]-L/opt/gcc-[^[:space:]"]+/lib64##g' \
        "${temp_db_dir}/compile_commands.json"
    fi

    clang_tidy_build_dir="${temp_db_dir}"
  fi

  local clang_tidy_args=( -p "${clang_tidy_build_dir}" )
  echo "Disabling for internal bug of clang-tidy..."
  clang_tidy_args+=( -checks=-modernize-use-scoped-lock )
  if [[ "${CLANG_TIDY_FIX}" == "true" ]]; then
    clang_tidy_args+=( -fix )
  fi

  if [[ ${#clang_tidy_files[@]} -gt 0 ]]; then
    clang-tidy "${clang_tidy_args[@]}" "${clang_tidy_files[@]}"
  fi

  if [[ -n "${temp_db_dir}" ]]; then
    rm -rf "${temp_db_dir}"
  fi
}

run_scan_build() {
  if ! command -v scan-build >/dev/null 2>&1; then
    echo "Required tool not found: scan-build" >&2
    exit 1
  fi

  mkdir -p "${SCAN_BUILD_OUT}"

  local scan_cmd=(scan-build -o "${SCAN_BUILD_OUT}" cmake --build "${BUILD_DIR}")
  if [[ -n "${PRESET}" ]]; then
    scan_cmd+=(--preset "${PRESET}")
  fi

  "${scan_cmd[@]}"
}

run_clang_analyze_html() {
  if ! command -v clang++ >/dev/null 2>&1; then
    echo "Required tool not found: clang++" >&2
    exit 1
  fi

  if [[ ! -d "Src" ]]; then
    echo "Skipping clang++ --analyze: Src directory not found."
    return
  fi

  mapfile -t src_cpp_files < <(find Src -type f \( -name '*.cpp' -o -name '*.cc' \))
  if [[ ${#src_cpp_files[@]} -eq 0 ]]; then
    echo "Skipping clang++ --analyze: no Src/*.cpp or Src/*.cc files found."
    return
  fi

  clang++ --analyze -DUSE_RUST=1 -Xanalyzer -analyzer-output=html "${src_cpp_files[@]}"
}

usage() {
  cat <<'EOF'
Usage: run_static_analysis.sh [options]

Single entrypoint for formatting and static analysis.

Runs by default:
  1) cmake-format + clang-format + clang-tidy
  2) scan-build

Options:
  --build-dir <path>           Build directory (default: build)
  --preset <name>              CMake preset name for scan-build
  --scan-build-out <path>      Output directory for scan-build reports (default: scan-build-reports)
  --fix                        Apply clang-tidy fixes (-fix)
  --with-clang-analyze-html    Also run clang++ --analyze (HTML output)

  --only-format                Run only format + clang-tidy step
  --only-scan-build            Run only scan-build step
  --only-clang-analyze-html    Run only clang++ --analyze HTML step

  -h, --help                   Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR="${2:-}"
      shift 2
      ;;
    --preset)
      PRESET="${2:-}"
      shift 2
      ;;
    --scan-build-out)
      SCAN_BUILD_OUT="${2:-}"
      shift 2
      ;;
    --fix)
      CLANG_TIDY_FIX="true"
      shift
      ;;
    --with-clang-analyze-html)
      RUN_CLANG_ANALYZE_HTML="true"
      shift
      ;;
    --only-format)
      RUN_FORMAT_AND_TIDY="true"
      RUN_SCAN_BUILD="false"
      shift
      ;;
    --only-scan-build)
      RUN_FORMAT_AND_TIDY="false"
      RUN_SCAN_BUILD="true"
      shift
      ;;
    --only-clang-analyze-html)
      RUN_FORMAT_AND_TIDY="false"
      RUN_SCAN_BUILD="false"
      RUN_CLANG_ANALYZE_HTML="true"
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

git config --global --add safe.directory /workspace || true

cd "${ROOT_DIR}"

total_steps=0
if [[ "${RUN_FORMAT_AND_TIDY}" == "true" ]]; then
  ((total_steps += 1))
fi
if [[ "${RUN_SCAN_BUILD}" == "true" ]]; then
  ((total_steps += 1))
fi
if [[ "${RUN_CLANG_ANALYZE_HTML}" == "true" ]]; then
  ((total_steps += 1))
fi

if [[ ${total_steps} -eq 0 ]]; then
  echo "Nothing to run. Use --help for available options."
  exit 1
fi

step=1

if [[ "${RUN_FORMAT_AND_TIDY}" == "true" ]]; then
  echo "[${step}/${total_steps}] Running format + clang-tidy..."
  run_format_and_tidy
  ((step += 1))
fi

if [[ "${RUN_SCAN_BUILD}" == "true" ]]; then
  echo "[${step}/${total_steps}] Running scan-build..."
  run_scan_build
  ((step += 1))
fi

if [[ "${RUN_CLANG_ANALYZE_HTML}" == "true" ]]; then
  echo "[${step}/${total_steps}] Running clang++ --analyze HTML report generation..."
  run_clang_analyze_html
fi

echo "Done: static analysis and formatting pipeline completed."
