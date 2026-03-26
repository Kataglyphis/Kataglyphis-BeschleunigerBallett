#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SHADERS_ROOT="${PROJECT_ROOT}/Resources/Shaders"

TARGET_ENV=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --target-env)
      TARGET_ENV="$2"
      shift 2
      ;;
    *)
      shift
      ;;
  esac
done

if [[ -z "${TARGET_ENV}" ]]; then
  if [[ -n "${VULKAN_VERSION:-}" && "${VULKAN_VERSION}" =~ ^([0-9]+)\.([0-9]+) ]]; then
    TARGET_ENV="vulkan${BASH_REMATCH[1]}.${BASH_REMATCH[2]}"
  else
    TARGET_ENV="vulkan1.4"
  fi
fi

if [[ ! -d "${SHADERS_ROOT}" ]]; then
  echo "[WARN] No shader directory found at ${SHADERS_ROOT}, skipping shader compilation"
  exit 0
fi

if ! command -v glslc >/dev/null 2>&1; then
  echo "[ERROR] glslc not found in PATH. Install Vulkan SDK or ensure glslc is available." >&2
  exit 2
fi

echo "[INFO] Precompiling shaders under ${SHADERS_ROOT} -> target-env=${TARGET_ENV}"

# Build include flags from all shader subdirectories
INCLUDES=()
while IFS= read -r -d $'\0' d; do
  INCLUDES+=("-I" "${d}")
done < <(find "${SHADERS_ROOT}" -type d -print0)

# Find shader source files and compile to spv (skip existing .spv files)
while IFS= read -r -d $'\0' shader; do
  # Skip already compiled spv files
  case "${shader}" in
    *.spv) continue ;;
  esac

  outdir="$(dirname "${shader}")/spv"
  mkdir -p "${outdir}"
  outfile="${outdir}/$(basename "${shader}").spv"

  echo "[INFO] Compiling ${shader} -> ${outfile}"
  glslc --target-env="${TARGET_ENV}" "${shader}" -o "${outfile}" "${INCLUDES[@]}"
done < <(find "${SHADERS_ROOT}" -type f -regextype posix-extended -regex '.*\.(vert|frag|comp|rgen|rchit|rmiss|geom|tesc|tese|glsl)$' -print0)

echo "[INFO] Shader precompilation finished"
