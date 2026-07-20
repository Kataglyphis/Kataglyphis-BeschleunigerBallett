# Runs both renderers headlessly and prints their per-pass GPU timings side by
# side - the first piece of the cross-renderer comparison harness.
#
# Sources:
#   C++/Vulkan: KATAGLYPHIS_GPU_TIMING_JSON export, produced by driving the
#               golden harness for a deterministic frame count.
#   Rust/WebGPU: the dump_gpu_timings example.
#
# Both write the same schema, so one parser reads both. Pass names only
# partially overlap (the pipelines genuinely differ - the C++ engine has
# Clouds/Sky, the Rust renderer has Ssao/Bloom/Histogram); the table aligns
# by name and leaves the other column empty rather than pretending the
# renderers are structurally identical.
#
# SAME scene, SAME resolution: the C++ golden harness renders the bundled
# Dinosaurs OBJ at 1200x768; this script converts that OBJ to glTF (the Rust
# renderer's format) via the obj2gltf example and times the Rust renderer on
# the converted scene at the same 1200x768. The conversion is data-exact -
# 166563 positions / 894174 indices on both sides, matching the C++ loader's
# own log line. Remaining honest differences: the pipelines are structurally
# different (Clouds/Sky vs Ssao/Bloom/Histogram), the camera framing differs,
# and the conversion currently carries no textures - so treat per-pass numbers
# as comparable workloads, not as a shader-for-shader benchmark.

[CmdletBinding()]
param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) 'kataglyphis-timings')
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $OutDir | Out-Null

$cppJson = Join-Path $OutDir 'gpu-timings-cpp.json'
$rustJson = Join-Path $OutDir 'gpu-timings-rust.json'

$suite = Join-Path $RepoRoot 'build-clangcl-debug\commitTestSuite.exe'
if (-not (Test-Path $suite)) {
    throw "commitTestSuite.exe not found at $suite - build clangcl-debug first."
}

Write-Host '== C++/Vulkan (golden harness, RendersNonBlankFrame) =='
$env:KATAGLYPHIS_GPU_TIMING_JSON = $cppJson
try {
    & $suite --gtest_filter=GoldenRender.RendersNonBlankFrame *> $null
} finally {
    $env:KATAGLYPHIS_GPU_TIMING_JSON = ''
}

# Convert the same OBJ the C++ harness renders into glTF for the Rust side.
# Cached: the conversion is deterministic, and reconverting 27 MB per run
# would dominate the harness's own runtime.
$dinoObj = Join-Path $RepoRoot 'Resources\Models\Dinosaurs\dinosaurs.obj'
$dinoGltf = Join-Path $OutDir 'dinosaurs.gltf'
Push-Location (Join-Path $RepoRoot 'ExternalLib\Kataglyphis-RustProjectTemplate')
try {
    if (-not (Test-Path $dinoGltf) -or
        (Get-Item $dinoObj).LastWriteTime -gt (Get-Item $dinoGltf).LastWriteTime) {
        Write-Host '== Converting Dinosaurs OBJ -> glTF =='
        cargo run -p kataglyphis_webgpu_renderer --example obj2gltf --quiet -- $dinoObj $dinoGltf 2>$null | Out-Null
    }

    Write-Host '== Rust/WebGPU (dump_gpu_timings, same scene, 1200x768) =='
    cargo run -p kataglyphis_webgpu_renderer --example dump_gpu_timings --quiet -- $rustJson $dinoGltf 1200 768 2>$null | Out-Null
} finally {
    Pop-Location
}

$cpp = Get-Content $cppJson -Raw | ConvertFrom-Json
$rust = Get-Content $rustJson -Raw | ConvertFrom-Json

$names = [System.Collections.Generic.SortedSet[string]]::new()
foreach ($p in $cpp.passes.PSObject.Properties.Name) { [void]$names.Add($p) }
foreach ($p in $rust.passes.PSObject.Properties.Name) { [void]$names.Add($p) }

Write-Host ''
Write-Host ("{0,-16} {1,14} {2,14}" -f 'Pass', 'C++/Vulkan ms', 'Rust/WebGPU ms')
Write-Host ('-' * 46)
foreach ($name in $names) {
    $c = $cpp.passes.PSObject.Properties[$name]
    $r = $rust.passes.PSObject.Properties[$name]
    $cv = if ($c) { '{0:N4}' -f $c.Value } else { '-' }
    $rv = if ($r) { '{0:N4}' -f $r.Value } else { '-' }
    Write-Host ("{0,-16} {1,14} {2,14}" -f $name, $cv, $rv)
}
Write-Host ''
Write-Host ("frames: C++ {0} ({1}), Rust {2} ({3})" -f `
    $cpp.frames_measured, ($cpp.timestamps_supported ? 'timestamps' : 'NO timestamps'), `
    $rust.frames_measured, ($rust.timestamps_supported ? 'timestamps' : 'NO timestamps'))
