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
# The numbers are NOT a benchmark of each other: different scenes, different
# resolutions, different work per pass. What they share is the schema and the
# units, which is what the comparison harness needs to grow from.

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

Write-Host '== Rust/WebGPU (dump_gpu_timings example) =='
Push-Location (Join-Path $RepoRoot 'ExternalLib\Kataglyphis-RustProjectTemplate')
try {
    cargo run -p kataglyphis_webgpu_renderer --example dump_gpu_timings --quiet -- $rustJson 2>$null | Out-Null
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
