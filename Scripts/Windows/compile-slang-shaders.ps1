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

  The manifest (entry points, targets, the combined-WGSL map and the
  depth-texture patch table) is single-sourced from
  Resources/ShadersSlang/shader-manifest.json, shared with
  Scripts/Linux/compile-slang-shaders.sh. Schema notes live in that file's
  "_comment" fields.

  Math-only modules (e.g. common/aces.slang) have no entry point and are
  never emitted directly: they are `import`ed by entry-point shaders and
  linked by slangc. See docs/shader-sharing.md.

  Staleness: an output is reused only when it
  is newer than its source AND every .slang file under the Slang tree AND
  the manifest file itself (conservative — an import or manifest edit
  rebuilds every dependent).

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
$manifestPath = Join-Path $slangRoot 'shader-manifest.json'

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

if (-not (Test-Path $manifestPath)) {
    Write-Error "Shader manifest not found: $manifestPath"
    exit 2
}

$slangc = Resolve-Slangc
if (-not $slangc) {
    Write-Error 'slangc.exe not found in VULKAN_SDK or PATH. Install the Vulkan SDK (ships slangc) or add slangc to PATH.'
    exit 2
}
Write-Host "[INFO] Using slangc: $slangc"

$manifestData = Get-Content -Path $manifestPath -Raw | ConvertFrom-Json
# Rows flagged "disabled" are kept in the JSON as documentation only.
$Manifest = @($manifestData.manifest | Where-Object {
    -not ($_.PSObject.Properties['disabled'] -and $_.disabled)
})

# Every .slang file is a potential import dependency, and a manifest edit can
# retarget any output (conservative staleness).
$allSlangFiles = @(Get-ChildItem -Path $slangRoot -Recurse -File -Filter '*.slang' -ErrorAction SilentlyContinue)
$newestSource = (($allSlangFiles + @(Get-Item $manifestPath)) |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1).LastWriteTimeUtc

$failed = @()
$compiled = 0

foreach ($entry in $Manifest) {
    $srcPath = Join-Path $slangRoot $entry.file
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

    foreach ($target in $entry.targets) {
        $outExt = if ($target -eq 'spirv') { 'spv' } else { 'wgsl' }
        $outDir = Join-Path $buildRoot $target
        if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
        # Mirror the source subdirectory under build/<target>/ so distinct
        # shaders with the same entry-point name do not collide.
        $relDir = Split-Path $entry.file -Parent
        $targetOutDir = if ($relDir) { Join-Path $outDir $relDir } else { $outDir }
        if (-not (Test-Path $targetOutDir)) { New-Item -ItemType Directory -Force -Path $targetOutDir | Out-Null }
        $baseName = [IO.Path]::GetFileNameWithoutExtension($entry.file)
        $outFile = Join-Path $targetOutDir "$baseName.$($entry.entry).$outExt"

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

        Write-Host "[INFO] Compiling $($entry.file) ($($entry.entry) / $($entry.stage)) -> $target"
        $slangArgs = @("-target", $target, "-stage", $entry.stage, "-entry", $entry.entry) + $includeArgs + @('-o', $outFile, $srcPath)
        & $slangc $slangArgs
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "slangc failed: $($entry.file) $($entry.entry) -> $target"
            $failed += "$srcPath ($($entry.entry) -> $target)"
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
# Combined WGSL emit: compile each wgslMap source WITHOUT -entry/-stage to get
# all entry points in one WGSL file, then copy to the Rust crate's shader
# directory so include_str! picks up the Slang-emitted WGSL.
# ---------------------------------------------------------------------------
$wgslFailed = @()
$wgslEmitted = 0

foreach ($entry in $manifestData.wgslMap) {
    $srcPath = Join-Path $slangRoot $entry.src
    if (-not (Test-Path $srcPath)) { continue }

    $includeArgs = @('-I', $slangRoot, '-I', (Split-Path $srcPath -Parent))
    foreach ($d in (Get-ChildItem -Path $slangRoot -Directory -Recurse -ErrorAction SilentlyContinue)) {
        $includeArgs += '-I'; $includeArgs += $d.FullName
    }

    $tmpOut = Join-Path $buildRoot "combined_$($entry.out)"
    # No -entry/-stage: Slang emits ALL entry points in one WGSL file.
    & $slangc -target wgsl $includeArgs -o $tmpOut $srcPath
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Combined WGSL emit failed: $($entry.src)"
        $wgslFailed += $entry.src
        continue
    }

    # Depth-texture patch table: why each patch exists is documented in the
    # "_comment" fields next to the patterns in shader-manifest.json.
    $patchProp = $manifestData.depthTexturePatches.PSObject.Properties[$entry.out]
    if ($patchProp) {
        $wgslText = Get-Content -Path $tmpOut -Raw
        foreach ($p in @($patchProp.Value)) {
            $patched = $wgslText -replace $p.pattern, $p.replacement
            if ($patched -eq $wgslText) {
                Write-Warning "$($entry.out) depth-texture patch '$($p.pattern)' matched nothing - slangc output may have changed"
            }
            $wgslText = $patched
        }
        Set-Content -Path $tmpOut -Value $wgslText -NoNewline -Encoding utf8
    }

    # Copy to the Rust crate's shader directory (replaces hand-written WGSL).
    $dstDir = Join-Path $scriptRoot $entry.dst
    if (-not (Test-Path $dstDir)) { New-Item -ItemType Directory -Force -Path $dstDir | Out-Null }
    Copy-Item -Path $tmpOut -Destination (Join-Path $dstDir $entry.out) -Force
    $wgslEmitted++
}

if ($wgslFailed.Count -gt 0) {
  Write-Warning ("Combined WGSL emit failed for $($wgslFailed.Count) file(s):`n  " + ($wgslFailed -join "`n  "))
}

Write-Host "[INFO] Slang shader compilation finished ($compiled SPIR-V/WGSL artifact(s) + $wgslEmitted combined WGSL file(s) for Rust)"
