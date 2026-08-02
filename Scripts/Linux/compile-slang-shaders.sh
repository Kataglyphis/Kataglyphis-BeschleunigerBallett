#!/usr/bin/env bash
# Compile Slang shaders to SPIR-V (Vulkan/C++) and WGSL (Rust/WebGPU).
# Linux equivalent of Scripts/Windows/compile-slang-shaders.ps1.
#
# The manifest (entry points, targets, the combined-WGSL map and the
# depth-texture patch table) is single-sourced from
# Resources/ShadersSlang/shader-manifest.json, shared with the Windows
# script. Schema notes live in that file's "_comment" fields. Reading it
# requires jq.
#
# Usage: bash compile-slang-shaders.sh
#
# slangc is resolved from VULKAN_SDK/bin/slangc, then PATH.
#
# Staleness: an output is reused only when it is newer than its source AND
# every .slang file under the Slang tree AND the manifest file itself
# (conservative — an import or manifest edit rebuilds every dependent).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SLANG_ROOT="${REPO_ROOT}/Resources/ShadersSlang"
BUILD_ROOT="${SLANG_ROOT}/build"
MANIFEST_FILE="${SLANG_ROOT}/shader-manifest.json"

if ! command -v jq &>/dev/null; then
  echo "[ERROR] jq not found on PATH - required to read shader-manifest.json" >&2
  exit 2
fi

if [[ ! -d "$SLANG_ROOT" ]]; then
  echo "[WARN] Slang shader directory not found: $SLANG_ROOT - skipping"
  exit 0
fi

if [[ ! -f "$MANIFEST_FILE" ]]; then
  echo "[ERROR] Shader manifest not found: $MANIFEST_FILE" >&2
  exit 2
fi

# Resolve slangc
SLANGC=""
if [[ -n "${VULKAN_SDK:-}" && -f "${VULKAN_SDK}/bin/slangc" ]]; then
  SLANGC="${VULKAN_SDK}/bin/slangc"
elif command -v slangc &>/dev/null; then
  SLANGC="$(command -v slangc)"
else
  echo "[ERROR] slangc not found in VULKAN_SDK or PATH. Install the Vulkan SDK (ships slangc) or add slangc to PATH." >&2
  exit 2
fi

echo "[INFO] Using slangc: ${SLANGC}"

# Every .slang file is a potential import dependency, and a manifest edit can
# retarget any output (conservative staleness). Newest mtime wins.
NEWEST_SOURCE="$( { find "$SLANG_ROOT" -type f -name '*.slang' -printf '%T@\n'; find "$MANIFEST_FILE" -printf '%T@\n'; } | sort -g | tail -n 1 )"

# Subdirectories under the Slang tree, reused for every -I expansion.
SUBDIRS=()
while IFS= read -r -d '' dir; do
  SUBDIRS+=("$dir")
done < <(find "$SLANG_ROOT" -mindepth 1 -type d -print0)

# slangc resolves `import <name>` to <name>.slang on the -I paths. Add the
# Slang root, the source's own directory and every subdirectory so
# `import aces` finds common/aces.slang regardless of where the importing
# shader lives.
build_include_args() {
  local src_parent="$1"
  INCLUDE_ARGS=("-I" "$SLANG_ROOT" "-I" "$src_parent")
  local d
  for d in "${SUBDIRS[@]}"; do
    INCLUDE_ARGS+=("-I" "$d")
  done
}

FAILED_ENTRIES=()
COMPILED=0

while IFS='|' read -r file entry_name stage targets; do
  src_path="${SLANG_ROOT}/${file}"
  if [[ ! -f "$src_path" ]]; then
    echo "[WARN] Manifest references missing file: $src_path"
    FAILED_ENTRIES+=("$src_path")
    continue
  fi

  build_include_args "$(dirname "$src_path")"

  IFS=',' read -ra target_list <<< "$targets"
  for target in "${target_list[@]}"; do
    if [[ "$target" == "spirv" ]]; then
      out_ext="spv"
    else
      out_ext="wgsl"
    fi
    # Mirror the source subdirectory under build/<target>/ so distinct
    # shaders with the same entry-point name do not collide.
    rel_dir="$(dirname "$file")"
    base_name="$(basename "$file" .slang)"
    out_dir="${BUILD_ROOT}/${target}/${rel_dir}"
    mkdir -p "$out_dir"
    out_file="${out_dir}/${base_name}.${entry_name}.${out_ext}"

    needs_compile=1
    if [[ -f "$out_file" ]]; then
      out_stamp="$(find "$out_file" -printf '%T@')"
      if awk -v o="$out_stamp" -v n="$NEWEST_SOURCE" 'BEGIN { exit !(o >= n) }'; then
        needs_compile=0
        echo "[INFO] Up to date: $out_file"
      else
        echo "[INFO] Stale, recompiling: $out_file"
      fi
    fi
    if [[ $needs_compile -eq 0 ]]; then continue; fi

    echo "[INFO] Compiling ${file} (${entry_name} / ${stage}) -> ${target}"
    if ! "$SLANGC" -target "$target" -stage "$stage" -entry "$entry_name" \
         "${INCLUDE_ARGS[@]}" -o "$out_file" "$src_path"; then
      echo "[WARN] slangc failed: ${file} ${entry_name} -> ${target}"
      FAILED_ENTRIES+=("${src_path} (${entry_name} -> ${target})")
    else
      COMPILED=$((COMPILED + 1))
    fi
  done
done < <(jq -r '.manifest[] | select(.disabled != true) | [.file, .entry, .stage, (.targets | join(","))] | join("|")' "$MANIFEST_FILE" | tr -d '\r')

if [[ ${#FAILED_ENTRIES[@]} -gt 0 ]]; then
  {
    echo "[ERROR] Slang compilation failed for ${#FAILED_ENTRIES[@]} entry point(s):"
    printf '  %s\n' "${FAILED_ENTRIES[@]}"
  } >&2
  exit 1
fi

# ---------------------------------------------------------------------------
# Combined WGSL emit: compile each wgslMap source WITHOUT -entry/-stage to get
# all entry points in one WGSL file, then copy to the Rust crate's shader
# directory so include_str! picks up the Slang-emitted WGSL.
# ---------------------------------------------------------------------------
WGSL_FAILED=()
WGSL_EMITTED=0
mkdir -p "$BUILD_ROOT"

while IFS='|' read -r src_file out_name dst_rel; do
  src_path="${SLANG_ROOT}/${src_file}"
  if [[ ! -f "$src_path" ]]; then continue; fi

  build_include_args "$(dirname "$src_path")"

  tmp_out="${BUILD_ROOT}/combined_${out_name}"
  # No -entry/-stage: Slang emits ALL entry points in one WGSL file.
  if ! "$SLANGC" -target wgsl "${INCLUDE_ARGS[@]}" -o "$tmp_out" "$src_path"; then
    echo "[WARN] Combined WGSL emit failed: ${src_file}"
    WGSL_FAILED+=("$src_file")
    continue
  fi

  # Depth-texture patch table: why each patch exists is documented in the
  # "_comment" fields next to the patterns in shader-manifest.json.
  # tr strips the CR that jq emits on Windows hosts (harmless on Linux).
  patch_count="$(jq -r --arg f "$out_name" '(.depthTexturePatches[$f] // []) | length' "$MANIFEST_FILE" | tr -d '\r')"
  for ((i = 0; i < patch_count; i++)); do
    pattern="$(jq -r --arg f "$out_name" --argjson i "$i" '.depthTexturePatches[$f][$i].pattern' "$MANIFEST_FILE" | tr -d '\r')"
    replacement="$(jq -r --arg f "$out_name" --argjson i "$i" '.depthTexturePatches[$f][$i].replacement' "$MANIFEST_FILE" | tr -d '\r')"
    # Rewrite ${N} group references to sed's \N form.
    sed_repl="$(printf '%s' "$replacement" | sed -E 's/\$\{([0-9]+)\}/\\\1/g')"
    before_sum="$(cksum < "$tmp_out")"
    sed -i -E "s|${pattern}|${sed_repl}|g" "$tmp_out"
    after_sum="$(cksum < "$tmp_out")"
    if [[ "$before_sum" == "$after_sum" ]]; then
      echo "[WARN] ${out_name} depth-texture patch '${pattern}' matched nothing - slangc output may have changed"
    fi
  done

  # Copy to the Rust crate's shader directory (replaces hand-written WGSL).
  dst_dir="${REPO_ROOT}/${dst_rel}"
  mkdir -p "$dst_dir"
  cp "$tmp_out" "${dst_dir}/${out_name}"
  WGSL_EMITTED=$((WGSL_EMITTED + 1))
done < <(jq -r '.wgslMap[] | [.src, .out, .dst] | join("|")' "$MANIFEST_FILE" | tr -d '\r')

if [[ ${#WGSL_FAILED[@]} -gt 0 ]]; then
  echo "[WARN] Combined WGSL emit failed for ${#WGSL_FAILED[@]} file(s):"
  printf '  %s\n' "${WGSL_FAILED[@]}"
fi

echo "[INFO] Slang shader compilation finished (${COMPILED} SPIR-V/WGSL artifact(s) + ${WGSL_EMITTED} combined WGSL file(s) for Rust)"
