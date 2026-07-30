#!/usr/bin/env bash
# Compile Slang shaders to SPIR-V (Vulkan/C++) and WGSL (Rust/WebGPU).
# Linux equivalent of Scripts/Windows/compile-slang-shaders.ps1.
#
# Usage: bash compile-slang-shaders.sh
#
# slangc is resolved from VULKAN_SDK/bin/slangc, then PATH.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SLANG_ROOT="${REPO_ROOT}/Resources/ShadersSlang"
BUILD_ROOT="${SLANG_ROOT}/build"

# Resolve slangc
SLANGC=""
if [[ -n "${VULKAN_SDK:-}" && -f "${VULKAN_SDK}/bin/slangc" ]]; then
  SLANGC="${VULKAN_SDK}/bin/slangc"
elif command -v slangc &>/dev/null; then
  SLANGC="$(command -v slangc)"
else
  echo "[WARN] slangc not found in VULKAN_SDK or PATH — skipping Slang compilation"
  exit 0
fi

echo "[INFO] Using slangc: ${SLANGC}"

# Collect include dirs (all subdirectories under ShadersSlang)
INCLUDE_ARGS=()
while IFS= read -r -d '' dir; do
  INCLUDE_ARGS+=("-I" "$dir")
done < <(find "$SLANG_ROOT" -type d -print0)
INCLUDE_ARGS+=("-I" "$SLANG_ROOT")

# Manifest: file|entry|stage|targets
# targets: comma-separated list of 'spirv' and/or 'wgsl'
MANIFEST=(
  # C++/Vulkan post pass (SPIR-V only)
  "post/post.slang|vs_main|vertex|spirv"
  "post/post.slang|fs_main|fragment|spirv"
  # CI guards (both targets)
  "tests/brdf_test.slang|brdf_test_main|compute|spirv,wgsl"
  "tests/noise_test.slang|noise_test_main|compute|spirv,wgsl"
  # Vulkan ray tracing (SPIR-V only)
  "raytracing/raytrace.rmiss.slang|rmiss_main|miss|spirv"
  "raytracing/shadow.rmiss.slang|shadow_rmiss_main|miss|spirv"
  "raytracing/raytrace.rgen.slang|rgen_main|raygeneration|spirv"
  "raytracing/raytrace.rchit.slang|rchit_main|closesthit|spirv"
  # Path tracing (SPIR-V only)
  "path_tracing/path_tracing.slang|path_tracing_main|compute|spirv"
  # Skybox (SPIR-V only)
  "skybox/skybox.slang|vs_main|vertex|spirv"
  "skybox/skybox.slang|fs_main|fragment|spirv"
  # Compute (SPIR-V only)
  "compute/noise.slang|noise_main|compute|spirv"
  "compute/clouds.slang|clouds_main|compute|spirv"
  # Forward rasterizer (SPIR-V only)
  "rasterizer/rasterizer.slang|vs_main|vertex|spirv"
  "rasterizer/rasterizer.slang|fs_main|fragment|spirv"
  # Deferred (SPIR-V only)
  "deferred/deferred.slang|geometry_vs_main|vertex|spirv"
  "deferred/deferred.slang|geometry_fs_main|fragment|spirv"
  "deferred/deferred.slang|lighting_vs_main|vertex|spirv"
  "deferred/deferred.slang|lighting_fs_main|fragment|spirv"
  # Shadow map (SPIR-V only)
  "rasterizer/shadows/shadow_map.slang|shadow_vs_main|vertex|spirv"
  "rasterizer/shadows/shadow_map.slang|shadow_fs_main|fragment|spirv"
  # Rust/WebGPU forward PBR (WGSL only)
  "forward/forward.slang|vs_main|vertex|wgsl"
  "forward/forward.slang|fs_main|fragment|wgsl"
  "forward/forward.slang|vs_shadow|vertex|wgsl"
  "forward/forward.slang|vs_shadow_masked|vertex|wgsl"
  "forward/forward.slang|fs_shadow_masked|fragment|wgsl"
  # Rust/WebGPU sky (WGSL only)
  "sky/sky.slang|vs_main|vertex|wgsl"
  "sky/sky.slang|fs_main|fragment|wgsl"
  # Rust/WebGPU bloom (WGSL only)
  "bloom/bloom.slang|vs_main|vertex|wgsl"
  "bloom/bloom.slang|fs_brightpass|fragment|wgsl"
  "bloom/bloom.slang|fs_blur_h|fragment|wgsl"
  "bloom/bloom.slang|fs_blur_v|fragment|wgsl"
  # Rust/WebGPU SSAO (WGSL only)
  "ssao/ssao.slang|vs_main|vertex|wgsl"
  "ssao/ssao.slang|fs_ssao|fragment|wgsl"
  "ssao/ssao.slang|fs_blur|fragment|wgsl"
  # Rust/WebGPU IBL (WGSL only)
  "ibl/ibl.slang|vs_fullscreen|vertex|wgsl"
  "ibl/ibl.slang|fs_equirect_to_cube|fragment|wgsl"
  "ibl/ibl.slang|fs_irradiance|fragment|wgsl"
  "ibl/ibl.slang|fs_prefilter|fragment|wgsl"
  "ibl/ibl.slang|fs_brdf_lut|fragment|wgsl"
  # Rust/WebGPU GPU cull (WGSL only)
  "gpu_cull/gpu_cull.slang|cs_main|compute|wgsl"
  # Rust/WebGPU occlusion bbox (WGSL only)
  "occlusion_bbox/occlusion_bbox.slang|vs_main|vertex|wgsl"
  # Rust/WebGPU depth resolve (WGSL only)
  "depth_resolve/depth_resolve.slang|vs_main|vertex|wgsl"
  "depth_resolve/depth_resolve.slang|fs_main|fragment|wgsl"
  # Rust/WebGPU tonemap (WGSL only)
  "tonemap/tonemap.slang|vs_main|vertex|wgsl"
  "tonemap/tonemap.slang|fs_main|fragment|wgsl"
  # Rust/WebGPU GUI tex quad (WGSL only)
  "tex_quad/tex_quad.slang|vs_main|vertex|wgsl"
  "tex_quad/tex_quad.slang|fs_main|fragment|wgsl"
)

FAILED=0
COMPILED=0

for entry in "${MANIFEST[@]}"; do
  IFS='|' read -r file entry_name stage targets <<< "$entry"
  src_path="${SLANG_ROOT}/${file}"
  if [[ ! -f "$src_path" ]]; then
    echo "[WARN] Missing: $src_path"
    continue
  fi

  IFS=',' read -ra target_list <<< "$targets"
  for target in "${target_list[@]}"; do
    if [[ "$target" == "spirv" ]]; then
      out_ext="spv"
    else
      out_ext="wgsl"
    fi
    rel_dir="$(dirname "$file")"
    base_name="$(basename "$file" .slang)"
    out_dir="${BUILD_ROOT}/${target}/${rel_dir}"
    mkdir -p "$out_dir"
    out_file="${out_dir}/${base_name}.${entry_name}.${out_ext}"

    echo "[INFO] Compiling ${file} (${entry_name} / ${stage}) -> ${target}"
    if ! "$SLANGC" -target "$target" -stage "$stage" -entry "$entry_name" \
         "${INCLUDE_ARGS[@]}" -o "$out_file" "$src_path"; then
      echo "[WARN] slangc failed: ${file} ${entry_name} -> ${target}"
      FAILED=$((FAILED + 1))
    else
      COMPILED=$((COMPILED + 1))
    fi
  done
done

# Combined WGSL emit for the Rust crate (all entry points in one file)
RUST_SHADER_DIR="${REPO_ROOT}/ExternalLib/Kataglyphis-RustProjectTemplate/crates/webgpu_renderer/src/shaders"
RUST_GUI_SHADER_DIR="${REPO_ROOT}/ExternalLib/Kataglyphis-RustProjectTemplate/crates/gui/src/shaders"

WGSL_MAP=(
  "forward/forward.slang|forward.wgsl|${RUST_SHADER_DIR}"
  "sky/sky.slang|sky.wgsl|${RUST_SHADER_DIR}"
  "bloom/bloom.slang|bloom.wgsl|${RUST_SHADER_DIR}"
  "ssao/ssao.slang|ssao.wgsl|${RUST_SHADER_DIR}"
  "ibl/ibl.slang|ibl.wgsl|${RUST_SHADER_DIR}"
  "gpu_cull/gpu_cull.slang|gpu_cull.wgsl|${RUST_SHADER_DIR}"
  "tonemap/tonemap.slang|tonemap.wgsl|${RUST_SHADER_DIR}"
  "depth_resolve/depth_resolve.slang|depth_resolve.wgsl|${RUST_SHADER_DIR}"
  "occlusion_bbox/occlusion_bbox.slang|occlusion_bbox.wgsl|${RUST_SHADER_DIR}"
  "tex_quad/tex_quad.slang|tex_quad.wgsl|${RUST_GUI_SHADER_DIR}"
)

WGSL_EMITTED=0
for entry in "${WGSL_MAP[@]}"; do
  IFS='|' read -r src_file out_name dst_dir <<< "$entry"
  src_path="${SLANG_ROOT}/${src_file}"
  if [[ ! -f "$src_path" ]]; then continue; fi

  tmp_out="${BUILD_ROOT}/combined_${out_name}"
  if ! "$SLANGC" -target wgsl "${INCLUDE_ARGS[@]}" -o "$tmp_out" "$src_path"; then
    echo "[WARN] Combined WGSL emit failed: ${src_file}"
    continue
  fi
  mkdir -p "$dst_dir"
  cp "$tmp_out" "${dst_dir}/${out_name}"
  WGSL_EMITTED=$((WGSL_EMITTED + 1))
done

if [[ $FAILED -gt 0 ]]; then
  echo "[ERROR] Slang compilation failed for ${FAILED} entry point(s)"
  exit 1
fi

echo "[INFO] Slang shader compilation finished (${COMPILED} artifacts + ${WGSL_EMITTED} combined WGSL files)"