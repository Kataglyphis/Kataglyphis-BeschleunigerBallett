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

# shader-manifest.json is read with python3, NOT jq: jq is absent from the
# ContainerHub Linux image (verified 2026-08-02 — a jq dependency here made
# shader precompilation fail, and cmake-configure-build.sh's `|| warn` hid it,
# leaving CI with no SPIR-V at all), while python3 ships in the image and is
# already a documented project dependency. One reader, no second parser to
# drift.
if ! command -v python3 &>/dev/null; then
  echo "[ERROR] python3 not found on PATH - required to read shader-manifest.json" >&2
  exit 2
fi

# manifest_query <python-expression-name> [args...]
# Emits pipe-delimited rows on stdout. Every query lives here so the JSON
# schema is touched in exactly one place.
manifest_query() {
  python3 - "$MANIFEST_FILE" "$@" <<'PY'
import json, sys

manifest_path, query = sys.argv[1], sys.argv[2]
args = sys.argv[3:]
with open(manifest_path, encoding="utf-8") as handle:
    doc = json.load(handle)

if query == "rows":
    for row in doc["manifest"]:
        if row.get("disabled") is True:
            continue
        print("|".join([row["file"], row["entry"], row["stage"], ",".join(row["targets"])]))
elif query == "patch_count":
    print(len(doc.get("depthTexturePatches", {}).get(args[0], [])))
elif query == "patch_field":
    print(doc["depthTexturePatches"][args[0]][int(args[1])][args[2]])
elif query == "wgsl_map":
    for row in doc["wgslMap"]:
        print("|".join([row["src"], row["out"], row["dst"]]))
elif query == "min_slangc_version":
    print(doc.get("minSlangcVersionForWgsl", ""))
else:
    sys.exit(f"unknown manifest query: {query}")
PY
}

# ---------------------------------------------------------------------------
# Combined-WGSL emit correctness guard.
#
# WGSL requires every non-builtin member of an inter-stage (varying) struct to
# carry @location(N). slangc 2026.1-52-gc8ddf20bb - the build in Vulkan SDK
# 1.4.341.1, i.e. the ContainerHub Linux image - drops that attribute in the
# COMBINED emit (no -entry/-stage) while emitting it correctly per entry point,
# so a regeneration on that toolchain silently produced WGSL naga rejects.
# 2026.8 is correct on both Windows and Linux. Two defences, both needed:
#   1. slangc_supports_wgsl_emit: below the manifest's floor we do not emit at
#      all, so the (correct) checked-in WGSL is never overwritten.
#   2. wgsl_varyings_are_located: at or above the floor we emit and then verify,
#      so ANY future emit regression fails the build instead of being copied.
# The same rule is pinned in Test/commit/VulkanEngine/buildIntegritySuite.cpp
# (BuildIntegrity.CheckedInWgslVaryingStructsCarryLocations) and reimplemented
# in Scripts/Windows/compile-slang-shaders.ps1 - keep the three in step.
# ---------------------------------------------------------------------------

# wgsl_varyings_are_located <file>
# A struct with at least one @builtin/@location member is an IO struct; every
# member of it must then carry one of those attributes. Prints offenders and
# returns non-zero when the file is invalid.
wgsl_varyings_are_located() {
  python3 - "$1" <<'PY'
import re, sys

path = sys.argv[1]
lines = open(path, encoding="utf-8").read().splitlines()
member = re.compile(r"^\s*((?:@\w+\([^)]*\)\s*)*)([A-Za-z_]\w*)\s*:\s*\S.*?,?\s*$")
offenders = []
index = 0
while index < len(lines):
    head = re.match(r"^struct\s+([A-Za-z_]\w*)", lines[index])
    index += 1
    if head is None:
        continue
    if index < len(lines) and lines[index].strip() == "{":
        index += 1
    members = []
    while index < len(lines) and not lines[index].lstrip().startswith("}"):
        hit = member.match(lines[index])
        if hit is not None:
            members.append((index + 1, hit.group(1), lines[index]))
        index += 1
    io = [m for m in members if "@builtin(" in m[1] or "@location(" in m[1]]
    if not io:
        continue
    for line_no, attrs, raw in members:
        if "@builtin(" not in attrs and "@location(" not in attrs:
            offenders.append(f"{line_no}: struct {head.group(1)}: {raw.strip()}")

for offender in offenders:
    print(offender)
sys.exit(1 if offenders else 0)
PY
}

# version_at_least <have> <want> - compares the leading MAJOR.MINOR only.
# slangc prints e.g. "2026.8" or "2026.1-52-gc8ddf20bb". An unparseable
# version is treated as new enough: the emit guard above is the backstop, and
# refusing to compile on an unrecognised version string would be worse.
version_at_least() {
  local have="$1" want="$2"
  local have_major have_minor want_major want_minor
  if [[ ! "$have" =~ ^([0-9]+)\.([0-9]+) ]]; then return 0; fi
  have_major="${BASH_REMATCH[1]}"; have_minor="${BASH_REMATCH[2]}"
  if [[ ! "$want" =~ ^([0-9]+)\.([0-9]+) ]]; then return 0; fi
  want_major="${BASH_REMATCH[1]}"; want_minor="${BASH_REMATCH[2]}"
  if ((have_major != want_major)); then ((have_major > want_major)); return; fi
  ((have_minor >= want_minor))
}

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
done < <(manifest_query rows | tr -d '\r')

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
WGSL_INVALID=()
WGSL_EMITTED=0
mkdir -p "$BUILD_ROOT"

# Toolchain floor: below it slangc's combined emit is known to drop varying
# @location attributes, so skip the emit entirely rather than overwrite the
# checked-in WGSL with output naga rejects. Regenerating after a .slang edit
# then needs a newer slangc - BuildIntegrity.CheckedInWgslIsNotOlderThanItsSlangSource
# fails if that regeneration is skipped and forgotten.
MIN_SLANGC_VERSION="$(manifest_query min_slangc_version | tr -d '\r')"
SLANGC_VERSION="$("$SLANGC" -version 2>&1 | head -n 1 | tr -d '\r')"
WGSL_EMIT_ENABLED=1
if [[ -n "$MIN_SLANGC_VERSION" ]] && ! version_at_least "$SLANGC_VERSION" "$MIN_SLANGC_VERSION"; then
  WGSL_EMIT_ENABLED=0
  echo "[WARN] slangc ${SLANGC_VERSION} is older than ${MIN_SLANGC_VERSION}, whose combined (whole-module)" >&2
  echo "[WARN] WGSL emit is the first known-correct one: older builds drop @location(N) from varying" >&2
  echo "[WARN] structs and produce WGSL that wgpu/naga rejects. SKIPPING the combined WGSL emit - the" >&2
  echo "[WARN] checked-in Rust-crate WGSL is left untouched. See docs/shader-build-pipeline.md." >&2
fi

while [[ $WGSL_EMIT_ENABLED -eq 1 ]] && IFS='|' read -r src_file out_name dst_rel; do
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
  # tr strips the CR a Windows host may add (harmless on Linux).
  patch_count="$(manifest_query patch_count "$out_name" | tr -d '\r')"
  for ((i = 0; i < patch_count; i++)); do
    pattern="$(manifest_query patch_field "$out_name" "$i" pattern | tr -d '\r')"
    replacement="$(manifest_query patch_field "$out_name" "$i" replacement | tr -d '\r')"
    # Rewrite ${N} group references to sed's \N form.
    sed_repl="$(printf '%s' "$replacement" | sed -E 's/\$\{([0-9]+)\}/\\\1/g')"
    before_sum="$(cksum < "$tmp_out")"
    sed -i -E "s|${pattern}|${sed_repl}|g" "$tmp_out"
    after_sum="$(cksum < "$tmp_out")"
    if [[ "$before_sum" == "$after_sum" ]]; then
      echo "[WARN] ${out_name} depth-texture patch '${pattern}' matched nothing - slangc output may have changed"
    fi
  done

  # Reject a structurally invalid emit BEFORE it can overwrite the checked-in
  # file, so a broken regeneration can never be committed silently.
  if ! offenders="$(wgsl_varyings_are_located "$tmp_out")"; then
    {
      echo "[ERROR] ${out_name}: slangc ${SLANGC_VERSION} emitted varying struct member(s) with neither"
      echo "[ERROR]   @builtin nor @location - that is not valid WGSL and wgpu/naga will reject it."
      echo "[ERROR]   Emit kept at ${tmp_out}; ${dst_rel}/${out_name} NOT overwritten."
      while IFS= read -r offender; do echo "[ERROR]   ${offender}"; done <<< "$offenders"
    } >&2
    WGSL_INVALID+=("${out_name}")
    continue
  fi

  # Copy to the Rust crate's shader directory (replaces hand-written WGSL).
  dst_dir="${REPO_ROOT}/${dst_rel}"
  mkdir -p "$dst_dir"
  cp "$tmp_out" "${dst_dir}/${out_name}"
  WGSL_EMITTED=$((WGSL_EMITTED + 1))
done < <(manifest_query wgsl_map | tr -d '\r')

if [[ ${#WGSL_FAILED[@]} -gt 0 ]]; then
  echo "[WARN] Combined WGSL emit failed for ${#WGSL_FAILED[@]} file(s):"
  printf '  %s\n' "${WGSL_FAILED[@]}"
fi

echo "[INFO] Slang shader compilation finished (${COMPILED} SPIR-V/WGSL artifact(s) + ${WGSL_EMITTED} combined WGSL file(s) for Rust)"

# Fatal, and last so the SPIR-V summary above is still reported: an emit that
# violates WGSL's varying rules is a toolchain regression, not a warning.
if [[ ${#WGSL_INVALID[@]} -gt 0 ]]; then
  {
    echo "[ERROR] ${#WGSL_INVALID[@]} combined WGSL emit(s) had varying struct members without @builtin/@location:"
    printf '  %s\n' "${WGSL_INVALID[@]}"
    echo "[ERROR] None of them were copied into the Rust crates. Fix the toolchain (slangc >= ${MIN_SLANGC_VERSION}"
    echo "[ERROR] is known good; this run used ${SLANGC_VERSION}) - do not hand-patch the generated WGSL."
  } >&2
  exit 1
fi
