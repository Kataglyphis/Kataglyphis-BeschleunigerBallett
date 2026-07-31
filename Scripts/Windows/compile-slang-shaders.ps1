#requires -Version 7.0
<#
.SYNOPSIS
  Compiles Slang shaders to SPIR-V (Vulkan/C++) and WGSL (Rust/WebGPU).

.DESCRIPTION
  Slang is the single source of truth for shaders shared between the C++
  Vulkan renderer and the Rust WebGPU renderer. This script compiles each
  Slang entry point to the targets its consumer needs:
    - spirv  -> .spv  (loaded by the C++ Vulkan renderer)
    - wgsl   -> .wgsl (consumed by the Rust WebGPU renderer)

  Math-only modules (e.g. common/aces.slang) have no entry point and are
  never emitted directly: they are `import`ed by entry-point shaders and
  linked by slangc. See docs/shader-sharing.md.

  Staleness: an output is reused only when it
  is newer than its source AND every .slang file under the Slang tree
  (conservative — an import edit rebuilds every dependent).

.NOTES
  slangc is resolved from VULKAN_SDK\Bin, then PATH. The Vulkan SDK ships
  slangc (verified: VulkanSDK 1.4.350.0). Container availability of slangc
  is an open item — see docs/shader-sharing.md.
#>

param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..') | Select-Object -ExpandProperty Path
$slangRoot = Join-Path $scriptRoot 'Resources\ShadersSlang'
$buildRoot = Join-Path $slangRoot 'build'

# ---------------------------------------------------------------------------
# Manifest: one row per (entry point, target). Add rows as shaders migrate.
#   File   : path relative to $slangRoot
#   Entry  : entry-point function name
#   Stage  : vertex | fragment | compute | ...
#   Targets: 'spirv' and/or 'wgsl'
# ---------------------------------------------------------------------------
$Manifest = @(
    # Rust/WebGPU tonemap pass (WGSL emit only).
    @{ File = 'tonemap\tonemap.slang'; Entry = 'vs_main'; Stage = 'vertex';   Targets = @('wgsl') },
    @{ File = 'tonemap\tonemap.slang'; Entry = 'fs_main'; Stage = 'fragment'; Targets = @('wgsl') },
    # C++/Vulkan post pass (SPIR-V emit only).
    @{ File = 'post\post.slang';       Entry = 'vs_main'; Stage = 'vertex';   Targets = @('spirv') },
    @{ File = 'post\post.slang';       Entry = 'fs_main'; Stage = 'fragment'; Targets = @('spirv') },
    # CI guard: verify the shared BRDF math emits to BOTH targets.
    @{ File = 'tests\brdf_test.slang'; Entry = 'brdf_test_main'; Stage = 'compute'; Targets = @('spirv', 'wgsl') },
    # CI guard: verify the shared noise math emits to BOTH targets.
    @{ File = 'tests\noise_test.slang'; Entry = 'noise_test_main'; Stage = 'compute'; Targets = @('spirv', 'wgsl') },
    # Vulkan ray tracing shaders (SPIR-V only — WebGPU has no RT pipeline).
    @{ File = 'raytracing\raytrace.rmiss.slang'; Entry = 'rmiss_main'; Stage = 'miss'; Targets = @('spirv') },
    @{ File = 'raytracing\shadow.rmiss.slang';  Entry = 'shadow_rmiss_main'; Stage = 'miss'; Targets = @('spirv') },
    @{ File = 'raytracing\raytrace.rgen.slang'; Entry = 'rgen_main'; Stage = 'raygeneration'; Targets = @('spirv') },
    @{ File = 'raytracing\raytrace.rchit.slang'; Entry = 'rchit_main'; Stage = 'closesthit'; Targets = @('spirv') },
    # Path tracing compute kernel (ray query, SPIR-V only).
    @{ File = 'path_tracing\path_tracing.slang'; Entry = 'path_tracing_main'; Stage = 'compute'; Targets = @('spirv') },
    # Skybox (cubemap, SPIR-V only).
    @{ File = 'skybox\skybox.slang'; Entry = 'vs_main'; Stage = 'vertex'; Targets = @('spirv') },
    @{ File = 'skybox\skybox.slang'; Entry = 'fs_main'; Stage = 'fragment'; Targets = @('spirv') },
    # Compute noise volume (SPIR-V only).
    @{ File = 'compute\noise.slang'; Entry = 'noise_main'; Stage = 'compute'; Targets = @('spirv') },
    # Compute clouds (volumetric, SPIR-V only).
    @{ File = 'compute\clouds.slang'; Entry = 'clouds_main'; Stage = 'compute'; Targets = @('spirv') },
    # Forward rasterizer (SPIR-V only).
    @{ File = 'rasterizer\rasterizer.slang'; Entry = 'vs_main'; Stage = 'vertex'; Targets = @('spirv') },
    @{ File = 'rasterizer\rasterizer.slang'; Entry = 'fs_main'; Stage = 'fragment'; Targets = @('spirv') },
    # Deferred geometry + lighting (SPIR-V only).
    @{ File = 'deferred\deferred.slang'; Entry = 'geometry_vs_main'; Stage = 'vertex'; Targets = @('spirv') },
    @{ File = 'deferred\deferred.slang'; Entry = 'geometry_fs_main'; Stage = 'fragment'; Targets = @('spirv') },
    @{ File = 'deferred\deferred.slang'; Entry = 'lighting_vs_main'; Stage = 'vertex'; Targets = @('spirv') },
    @{ File = 'deferred\deferred.slang'; Entry = 'lighting_fs_main'; Stage = 'fragment'; Targets = @('spirv') },
    # Cascaded shadow map (SPIR-V only).
    @{ File = 'rasterizer\shadows\shadow_map.slang'; Entry = 'shadow_vs_main'; Stage = 'vertex'; Targets = @('spirv') },
    @{ File = 'rasterizer\shadows\shadow_map.slang'; Entry = 'shadow_fs_main'; Stage = 'fragment'; Targets = @('spirv') },
    # Rust/WebGPU forward PBR pass (WGSL emit).
    @{ File = 'forward\forward.slang'; Entry = 'vs_main'; Stage = 'vertex'; Targets = @('wgsl') },
    @{ File = 'forward\forward.slang'; Entry = 'fs_main'; Stage = 'fragment'; Targets = @('wgsl') },
    @{ File = 'forward\forward.slang'; Entry = 'vs_shadow'; Stage = 'vertex'; Targets = @('wgsl') },
    @{ File = 'forward\forward.slang'; Entry = 'vs_shadow_masked'; Stage = 'vertex'; Targets = @('wgsl') },
    @{ File = 'forward\forward.slang'; Entry = 'fs_shadow_masked'; Stage = 'fragment'; Targets = @('wgsl') },
    # Rust/WebGPU procedural sky (WGSL emit).
    @{ File = 'sky\sky.slang'; Entry = 'vs_main'; Stage = 'vertex'; Targets = @('wgsl') },
    @{ File = 'sky\sky.slang'; Entry = 'fs_main'; Stage = 'fragment'; Targets = @('wgsl') },
    # Rust/WebGPU bloom (WGSL emit).
    @{ File = 'bloom\bloom.slang'; Entry = 'vs_main'; Stage = 'vertex'; Targets = @('wgsl') },
    @{ File = 'bloom\bloom.slang'; Entry = 'fs_brightpass'; Stage = 'fragment'; Targets = @('wgsl') },
    @{ File = 'bloom\bloom.slang'; Entry = 'fs_blur_h'; Stage = 'fragment'; Targets = @('wgsl') },
    @{ File = 'bloom\bloom.slang'; Entry = 'fs_blur_v'; Stage = 'fragment'; Targets = @('wgsl') },
    # Rust/WebGPU SSAO (WGSL emit).
    @{ File = 'ssao\ssao.slang'; Entry = 'vs_main'; Stage = 'vertex'; Targets = @('wgsl') },
    @{ File = 'ssao\ssao.slang'; Entry = 'fs_ssao'; Stage = 'fragment'; Targets = @('wgsl') },
    @{ File = 'ssao\ssao.slang'; Entry = 'fs_blur'; Stage = 'fragment'; Targets = @('wgsl') },
    # Rust/WebGPU IBL precompute (WGSL emit).
    @{ File = 'ibl\ibl.slang'; Entry = 'vs_fullscreen'; Stage = 'vertex'; Targets = @('wgsl') },
    @{ File = 'ibl\ibl.slang'; Entry = 'fs_equirect_to_cube'; Stage = 'fragment'; Targets = @('wgsl') },
    @{ File = 'ibl\ibl.slang'; Entry = 'fs_irradiance'; Stage = 'fragment'; Targets = @('wgsl') },
    @{ File = 'ibl\ibl.slang'; Entry = 'fs_prefilter'; Stage = 'fragment'; Targets = @('wgsl') },
    @{ File = 'ibl\ibl.slang'; Entry = 'fs_brdf_lut'; Stage = 'fragment'; Targets = @('wgsl') },
    # Rust/WebGPU GPU occlusion culling (WGSL emit).
    @{ File = 'gpu_cull\gpu_cull.slang'; Entry = 'cs_main'; Stage = 'compute'; Targets = @('wgsl') },
    # Rust/WebGPU luminance histogram — WGSL fallback: Slang's InterlockedAdd on
# RWStructuredBuffer is not supported for the WGSL target (atomics require
# array<atomic<u32>> storage). This shader's WGSL stays hand-written; the
# Slang source is kept for documentation and future SPIR-V use.
    # @{ File = 'histogram\histogram.slang'; Entry = 'histogram_main'; Stage = 'compute'; Targets = @('wgsl') },
    # Rust/WebGPU occlusion bbox detection (WGSL emit).
    @{ File = 'occlusion_bbox\occlusion_bbox.slang'; Entry = 'vs_main'; Stage = 'vertex'; Targets = @('wgsl') },
    @{ File = 'occlusion_bbox\occlusion_bbox.slang'; Entry = 'fs_main'; Stage = 'fragment'; Targets = @('wgsl') },
    # Rust/WebGPU depth resolve (WGSL emit).
    @{ File = 'depth_resolve\depth_resolve.slang'; Entry = 'vs_main'; Stage = 'vertex'; Targets = @('wgsl') },
    @{ File = 'depth_resolve\depth_resolve.slang'; Entry = 'fs_main'; Stage = 'fragment'; Targets = @('wgsl') },
    # Rust/WebGPU GUI tex quad (WGSL emit).
    @{ File = 'tex_quad\tex_quad.slang'; Entry = 'vs_main'; Stage = 'vertex'; Targets = @('wgsl') },
    @{ File = 'tex_quad\tex_quad.slang'; Entry = 'fs_main'; Stage = 'fragment'; Targets = @('wgsl') }
)

function Resolve-Slangc {
    if ($env:VULKAN_SDK) {
        $candidate = Join-Path $env:VULKAN_SDK 'Bin\slangc.exe'
        if (Test-Path $candidate) { return $candidate }
    }
    $cmd = Get-Command slangc.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

if (-not (Test-Path $slangRoot)) {
    Write-Host "[WARN] Slang shader directory not found: $slangRoot - skipping"
    exit 0
}

$slangc = Resolve-Slangc
if (-not $slangc) {
    Write-Error 'slangc.exe not found in VULKAN_SDK or PATH. Install the Vulkan SDK (ships slangc) or add slangc to PATH.'
    exit 2
}
Write-Host "[INFO] Using slangc: $slangc"

# Every .slang file is a potential import dependency (conservative staleness).
$allSlangFiles = @(Get-ChildItem -Path $slangRoot -Recurse -File -Filter '*.slang' -ErrorAction SilentlyContinue)
$newestSource = ($allSlangFiles | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1).LastWriteTimeUtc

$failed = @()
$compiled = 0

foreach ($entry in $Manifest) {
    $srcPath = Join-Path $slangRoot $entry.File
    if (-not (Test-Path $srcPath)) {
        Write-Warning "Manifest references missing file: $srcPath"
        $failed += $srcPath
        continue
    }

    # slangc resolves `import <name>` to <name>.slang on the -I paths. Add
    # the Slang root and every subdirectory so `import aces` finds
    # common/aces.slang regardless of where the importing shader lives.
    $includeArgs = @('-I', $slangRoot, '-I', (Split-Path $srcPath -Parent))
    foreach ($d in (Get-ChildItem -Path $slangRoot -Directory -Recurse -ErrorAction SilentlyContinue)) {
        $includeArgs += '-I'; $includeArgs += $d.FullName
    }

    foreach ($target in $entry.Targets) {
        $outExt = if ($target -eq 'spirv') { 'spv' } else { 'wgsl' }
        $outDir = Join-Path $buildRoot $target
        if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
        # Mirror the source subdirectory under build/<target>/ so distinct
        # shaders with the same entry-point name do not collide.
        $relDir = Split-Path $entry.File -Parent
        $targetOutDir = if ($relDir) { Join-Path $outDir $relDir } else { $outDir }
        if (-not (Test-Path $targetOutDir)) { New-Item -ItemType Directory -Force -Path $targetOutDir | Out-Null }
        $baseName = [IO.Path]::GetFileNameWithoutExtension($entry.File)
        $outFile = Join-Path $targetOutDir "$baseName.$($entry.Entry).$outExt"

        $needsCompile = $true
        if (Test-Path $outFile) {
            $outStamp = (Get-Item $outFile).LastWriteTimeUtc
            if ($outStamp -ge $newestSource) {
                $needsCompile = $false
                Write-Host "[INFO] Up to date: $outFile"
            } else {
                Write-Host "[INFO] Stale, recompiling: $outFile"
            }
        }
        if (-not $needsCompile) { continue }

        Write-Host "[INFO] Compiling $($entry.File) ($($entry.Entry) / $($entry.Stage)) -> $target"
        $slangArgs = @("-target", $target, "-stage", $entry.Stage, "-entry", $entry.Entry) + $includeArgs + @('-o', $outFile, $srcPath)
        & $slangc $slangArgs
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "slangc failed: $($entry.File) $($entry.Entry) -> $target"
            $failed += "$srcPath ($($entry.Entry) -> $target)"
        } else {
            $compiled++
        }
    }
}

if ($failed.Count -gt 0) {
    Write-Error ("Slang compilation failed for $($failed.Count) entry point(s):`n  " + ($failed -join "`n  "))
    exit 1
}

# ---------------------------------------------------------------------------
# Combined WGSL emit: compile each shader file WITHOUT -entry/-stage to get
# all entry points in one WGSL file, then copy to the Rust crate's shader
# directory so include_str! picks up the Slang-emitted WGSL.
# ---------------------------------------------------------------------------
$RustShaderDir = Join-Path $scriptRoot 'ExternalLib\Kataglyphis-RustProjectTemplate\crates\webgpu_renderer\src\shaders'
$RustGuiShaderDir = Join-Path $scriptRoot 'ExternalLib\Kataglyphis-RustProjectTemplate\crates\gui\src\shaders'

# Map: Slang source file -> destination WGSL file in the Rust crate.
$WgslMap = @(
    @{ Src = 'forward\forward.slang';       Dst = $RustShaderDir;       Out = 'forward.wgsl' },
    @{ Src = 'sky\sky.slang';               Dst = $RustShaderDir;       Out = 'sky.wgsl' },
    @{ Src = 'bloom\bloom.slang';           Dst = $RustShaderDir;       Out = 'bloom.wgsl' },
    @{ Src = 'ssao\ssao.slang';             Dst = $RustShaderDir;       Out = 'ssao.wgsl' },
    @{ Src = 'ibl\ibl.slang';               Dst = $RustShaderDir;       Out = 'ibl.wgsl' },
    @{ Src = 'gpu_cull\gpu_cull.slang';     Dst = $RustShaderDir;       Out = 'gpu_cull.wgsl' },
    @{ Src = 'tonemap\tonemap.slang';       Dst = $RustShaderDir;       Out = 'tonemap.wgsl' },
    @{ Src = 'depth_resolve\depth_resolve.slang'; Dst = $RustShaderDir; Out = 'depth_resolve.wgsl' },
    @{ Src = 'occlusion_bbox\occlusion_bbox.slang'; Dst = $RustShaderDir; Out = 'occlusion_bbox.wgsl' },
    @{ Src = 'tex_quad\tex_quad.slang';     Dst = $RustGuiShaderDir;    Out = 'tex_quad.wgsl' }
)

# Per-output-file regex patches for the depth-texture backend limitation
# described above (Src -> WgslMap; see the patch loop for how these apply).
$DepthTexturePatches = @{
    'forward.wgsl'       = @(
        @{ Pattern = '(var shadowMap_\w+\s*:\s*)texture_2d_array<f32>'; Replacement = '${1}texture_depth_2d_array' }
    )
    'depth_resolve.wgsl' = @(
        @{ Pattern = '(var msaaDepth_\w+\s*:\s*)texture_multisampled_2d<f32>'; Replacement = '${1}texture_depth_multisampled_2d' }
        # textureLoad on texture_multisampled_2d<f32> returns vec4<f32> (hence
        # Slang's trailing .x); textureLoad on texture_depth_multisampled_2d
        # returns f32 directly, so the accessor becomes invalid once the type
        # above is patched. Drop it from this specific call.
        @{ Pattern = '(textureLoad\(\(msaaDepth_\w+\), \(\w+\), \(i32\(\w+\)\)\))\.x'; Replacement = '$1' }
    )
    'ssao.wgsl'          = @(
        @{ Pattern = '(var depthTex_\w+\s*:\s*)texture_2d<f32>'; Replacement = '${1}texture_depth_2d' }
        # See the depth_resolve.wgsl comment above: textureLoad on
        # texture_depth_2d returns f32 directly, so the trailing .x Slang
        # emitted for the texture_2d<f32> case becomes invalid.
        @{ Pattern = '(textureLoad\(\(depthTex_\w+\), \(\(\w+\)\)\.xy, \(\(\w+\)\)\.z\))\.x'; Replacement = '$1' }
    )
    'gpu_cull.wgsl'      = @(
        @{ Pattern = '(var depthTex_\w+\s*:\s*)texture_2d<f32>'; Replacement = '${1}texture_depth_2d' }
        @{ Pattern = '(textureLoad\(\(depthTex_\w+\), \(\(\w+\)\)\.xy, \(\(\w+\)\)\.z\))\.x'; Replacement = '$1' }
    )
}

$wgslFailed = @()
$wgslEmitted = 0

foreach ($entry in $WgslMap) {
    $srcPath = Join-Path $slangRoot $entry.Src
    if (-not (Test-Path $srcPath)) { continue }

    $includeArgs = @('-I', $slangRoot, '-I', (Split-Path $srcPath -Parent))
    foreach ($d in (Get-ChildItem -Path $slangRoot -Directory -Recurse -ErrorAction SilentlyContinue)) {
        $includeArgs += '-I'; $includeArgs += $d.FullName
    }

    $tmpOut = Join-Path $buildRoot "combined_$($entry.Out)"
    # No -entry/-stage: Slang emits ALL entry points in one WGSL file.
    & $slangc -target wgsl $includeArgs -o $tmpOut $srcPath
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Combined WGSL emit failed: $($entry.Src)"
        $wgslFailed += $entry.Src
        continue
    }

    # Slang's WGSL backend has no depth-texture resource type in HLSL syntax
    # (a Texture2DArray/Texture2DMS<float> sampled via SampleCmp/Load from a
    # depth-format image), so it always emits the sampled-float WGSL type even
    # where the Rust bind group layout declares TextureSampleType::Depth.
    # Patch the emitted declaration(s) until slangc gains a depth-texture
    # control (checked 2026-07-31: no such control exists yet).
    if ($DepthTexturePatches.ContainsKey($entry.Out)) {
        $wgslText = Get-Content -Path $tmpOut -Raw
        foreach ($p in $DepthTexturePatches[$entry.Out]) {
            $patched = $wgslText -replace $p.Pattern, $p.Replacement
            if ($patched -eq $wgslText) {
                Write-Warning "$($entry.Out) depth-texture patch '$($p.Pattern)' matched nothing - slangc output may have changed"
            }
            $wgslText = $patched
        }
        Set-Content -Path $tmpOut -Value $wgslText -NoNewline -Encoding utf8
    }

    # Copy to the Rust crate's shader directory (replaces hand-written WGSL).
    if (-not (Test-Path $entry.Dst)) { New-Item -ItemType Directory -Force -Path $entry.Dst | Out-Null }
    Copy-Item -Path $tmpOut -Destination (Join-Path $entry.Dst $entry.Out) -Force
    $wgslEmitted++
}

if ($wgslFailed.Count -gt 0) {
  Write-Warning ("Combined WGSL emit failed for $($wgslFailed.Count) file(s):`n  " + ($wgslFailed -join "`n  "))
}

Write-Host "[INFO] Slang shader compilation finished ($compiled SPIR-V/WGSL artifact(s) + $wgslEmitted combined WGSL file(s) for Rust)"