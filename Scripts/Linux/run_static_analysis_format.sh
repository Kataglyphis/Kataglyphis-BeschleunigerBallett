#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

BUILD_DIR="${BUILD_DIR:-build}"
PRESET="${PRESET:-}"
SCAN_BUILD_OUT="${SCAN_BUILD_OUT:-scan-build-reports}"
CLANG_TIDY_FIX="${CLANG_TIDY_FIX:-false}"
RUN_CLANG_ANALYZE_HTML="${RUN_CLANG_ANALYZE_HTML:-false}"

RUN_FORMAT_AND_TIDY="${RUN_FORMAT_AND_TIDY:-true}"
RUN_SCAN_BUILD="${RUN_SCAN_BUILD:-true}"

ensure_cmake_format() {
  if has_tool cmake-format; then
    return
  fi

  if ! has_tool uv; then
    err "Required tool not found: uv (needed to manage .venv and install requirements)"
  fi

  info "cmake-format not found. Preparing Python environment..."

  if [[ -d "${ROOT_DIR}/.venv" ]]; then
    info "Found .venv - activating and installing requirements..."
  else
    info "No .venv found - creating one with uv..."
    uv venv "${ROOT_DIR}/.venv"
  fi

  # shellcheck disable=SC1091
  source "${ROOT_DIR}/.venv/bin/activate"
  uv pip install -r "${ROOT_DIR}/requirements.txt"

  if ! has_tool cmake-format; then
    err "cmake-format is still not available after installing requirements."
  fi
}

run_format_and_tidy() {
  ensure_cmake_format

  require_tools cmake-format clang-format clang-tidy

  if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
    err "Missing ${BUILD_DIR}/compile_commands.json. Run CMake configure first, e.g. Scripts/Linux/cmake-configure-build.sh --build-dir ${BUILD_DIR} --preset <preset>"
  fi

  info "Formatting CMake files..."
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
    info "Finding C/C++ source files..."
    mapfile -t cpp_files < <(find "${cpp_search_dirs[@]}" \
      -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' -o -name '*.ixx' -o -name '*.cppm' -o -name '*.ccm' -o -name '*.cxxm' -o -name '*.mpp' \))
  fi

  local clang_tidy_files=()
  if [[ ${#cpp_search_dirs[@]} -gt 0 ]]; then
    mapfile -t clang_tidy_files < <(find "${cpp_search_dirs[@]}" \
      -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \))
  fi

  if [[ ${#cpp_files[@]} -gt 0 ]]; then
    info "Running clang-format on ${#cpp_files[@]} files..."
    clang-format -i "${cpp_files[@]}"
  fi

  local clang_tidy_build_dir="${BUILD_DIR}"
  local compile_db_path="${BUILD_DIR}/compile_commands.json"
  local temp_db_dir=""

  # If the compile DB was generated in a container (/workspace), remap paths for local runs.
  if grep -q '/workspace' "${compile_db_path}"; then
    temp_db_dir="$(mktemp -d)"
    info "Remapping container paths in compile_commands.json..."
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
  warn "Disabling for internal bug of clang-tidy..."
  clang_tidy_args+=( -checks=-modernize-use-scoped-lock )
  if [[ "${CLANG_TIDY_FIX}" == "true" ]]; then
    clang_tidy_args+=( -fix )
  fi

  if [[ ${#clang_tidy_files[@]} -gt 0 ]]; then
    info "Running clang-tidy on ${#clang_tidy_files[@]} files..."
    clang-tidy "${clang_tidy_args[@]}" "${clang_tidy_files[@]}"
  fi

  if [[ -n "${temp_db_dir}" ]]; then
    rm -rf "${temp_db_dir}"
  fi
}

run_scan_build() {
  require_tools scan-build

  mkdir -p "${SCAN_BUILD_OUT}"

  local scan_cmd=(scan-build -o "${SCAN_BUILD_OUT}" cmake --build "${BUILD_DIR}")
  if [[ -n "${PRESET}" ]]; then
    scan_cmd+=(--preset "${PRESET}")
  fi

  info "Running scan-build..."
  "${scan_cmd[@]}"
}

run_clang_analyze_html() {
  require_tools clang++

  if [[ ! -d "Src" ]]; then
    warn "Skipping clang++ --analyze: Src directory not found."
    return
  fi

  mapfile -t src_cpp_files < <(find Src -type f \( -name '*.cpp' -o -name '*.cc' \))
  if [[ ${#src_cpp_files[@]} -eq 0 ]]; then
    warn "Skipping clang++ --analyze: no Src/*.cpp or Src/*.cc files found."
    return
  fi

  info "Running clang++ --analyze (HTML output)..."
  clang++ --analyze -DUSE_RUST=1 -Xanalyzer -analyzer-output=html "${src_cpp_files[@]}"
}

usage() {
  cat <<'EOF'
Usage: run_static_analysis_format.sh [options]

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
      err "Unknown argument: $1"
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
  err "Nothing to run. Use --help for available options."
fi

step=1

if [[ "${RUN_FORMAT_AND_TIDY}" == "true" ]]; then
  info "[${step}/${total_steps}] Running format + clang-tidy..."
  run_format_and_tidy
  ((step += 1))
fi

if [[ "${RUN_SCAN_BUILD}" == "true" ]]; then
  info "[${step}/${total_steps}] Running scan-build..."
  run_scan_build
  ((step += 1))
fi

if [[ "${RUN_CLANG_ANALYZE_HTML}" == "true" ]]; then
  info "[${step}/${total_steps}] Running clang++ --analyze HTML report generation..."
  run_clang_analyze_html
fi

info "Static analysis and formatting pipeline completed successfully."