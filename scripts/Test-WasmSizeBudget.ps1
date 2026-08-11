#requires -Version 7.0
# Test-WasmSizeBudget.ps1
#
# Builds kataglyphis_webgpu_renderer for wasm32-unknown-unknown, optimises with
# wasm-opt -Oz, and fails if the resulting .wasm exceeds the budget.
#
# Run from the repo root:
#   pwsh -ExecutionPolicy Bypass -File .\scripts\Test-WasmSizeBudget.ps1
#
# Prerequisites:
#   - Rust toolchain with wasm32-unknown-unknown target
#   - binaryen (wasm-opt) - bootstrapped automatically from the pin in
#     ContainerHub's versions.env when it is not already on PATH
#
# Install wasm target:
#   rustup target add wasm32-unknown-unknown
#
# Install binaryen (wasm-opt) yourself if you prefer a system copy:
#   winget install binaryen          # Windows
#   brew install binaryen            # macOS
#   sudo apt install binaryen        # Linux

[CmdletBinding()]
param(

    # Wasm size budget in bytes (default 12.0 MiB - matches
    # scripts/linux/wasm-size-budget.sh, the CI-enforced equivalent. Measured
    # 2026-07-31 post -Oz: ~8.3 MiB; the previous 4 MiB default predated any
    # real measurement and would have failed immediately).
    [int]$BudgetBytes = 12582912,
    # Path to the Rust project template (kataglyphis_webgpu_renderer lives here).
    [string]$RustProjectDir = (Join-Path $PSScriptRoot '..\ExternalLib\Kataglyphis-RustProjectTemplate'),
    # Skip the cargo build step (useful for re-checking an existing build).
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$exitCode = 0

# Resolve paths
$RustProjectDir = (Resolve-Path $RustProjectDir).Path
$wasmFile = Join-Path $RustProjectDir 'target\wasm32-unknown-unknown\release\kataglyphis_webgpu_renderer.wasm'

Write-Host '=== Wasm Size Budget Test ===' -ForegroundColor Cyan
Write-Host "Budget: $BudgetBytes bytes ($([math]::Round($BudgetBytes / 1MB, 2)) MiB)" -ForegroundColor Cyan
Write-Host "Rust project: $RustProjectDir" -ForegroundColor Cyan
Write-Host ''

# ---- Prerequisites ----
$hasWasmTarget = $false
try {
    $installed = rustup target list --installed 2>$null
    $hasWasmTarget = $installed -match 'wasm32-unknown-unknown'
} catch { }

if (-not $hasWasmTarget) {
    Write-Host 'Wasm target wasm32-unknown-unknown not installed.' -ForegroundColor Yellow
    Write-Host 'Install with: rustup target add wasm32-unknown-unknown' -ForegroundColor Yellow
    exit 1
}

# Bootstrap a pinned, SHA-verified binaryen instead of failing when wasm-opt is
# missing - parity with scripts/linux/wasm-size-budget.sh, which fetches the
# same release from the same pin. Both read version + checksums from
# ContainerHub's linux/scripts/01-core/versions.env.
. (Join-Path $PSScriptRoot 'Windows\Resolve-BuildModule.ps1')
Import-BuildModule @('WindowsWasmOpt.Common')
$null = Install-WasmOpt

# ---- Build ----
if (-not $SkipBuild) {
    Write-Host '== Building for wasm32-unknown-unknown (release) ==' -ForegroundColor Cyan
    Push-Location $RustProjectDir
    try {
        cargo build --target wasm32-unknown-unknown --release -p kataglyphis_webgpu_renderer
        if ($LASTEXITCODE -ne 0) {
            throw "cargo build failed (exit code $LASTEXITCODE)."
        }
    } finally {
        Pop-Location
    }
} else {
    Write-Host '== Skipping build (-SkipBuild) ==' -ForegroundColor Yellow
}

# ---- Check file exists ----
if (-not (Test-Path $wasmFile)) {
    Write-Host "FAIL: Wasm file not found at $wasmFile" -ForegroundColor Red
    Write-Host 'Build the crate first, or omit -SkipBuild.' -ForegroundColor Yellow
    exit 1
}

# ---- Measure pre-opt size ----
$preSize = (Get-Item $wasmFile).Length
Write-Host "Pre-opt size: $preSize bytes ($([math]::Round($preSize / 1KB, 1)) KiB)" -ForegroundColor Cyan

# ---- Optimise with wasm-opt ----
Write-Host '== Running wasm-opt -Oz ==' -ForegroundColor Cyan
$optFile = $wasmFile -replace '\.wasm$', '.opt.wasm'
# Invoke-WasmOpt supplies the required WebAssembly feature flags (wgpu/naga
# emits bulk-memory, nontrapping-float-to-int, sign-extension and simd
# instructions) and the --all-features retry.
Invoke-WasmOpt -InputPath $wasmFile -OutputPath $optFile -OptimizationLevel '-Oz'

$optSize = (Get-Item $optFile).Length
Write-Host "Post-opt size: $optSize bytes ($([math]::Round($optSize / 1KB, 1)) KiB)" -ForegroundColor Cyan
Write-Host "Saved: $($preSize - $optSize) bytes ($([math]::Round(($preSize - $optSize) / 1KB, 1)) KiB)" -ForegroundColor Cyan

# Replace original with optimised
Move-Item -Force $optFile $wasmFile

# ---- Check against budget ----
$finalSize = (Get-Item $wasmFile).Length
$ratio = [math]::Round($finalSize * 100.0 / $BudgetBytes, 1)

Write-Host ''
Write-Host "Final size: $finalSize bytes ($([math]::Round($finalSize / 1KB, 1)) KiB)" -ForegroundColor Cyan
Write-Host "Budget:     $BudgetBytes bytes ($([math]::Round($BudgetBytes / 1KB, 1)) KiB)" -ForegroundColor Cyan
Write-Host "Ratio:      $ratio%" -ForegroundColor Cyan

if ($finalSize -gt $BudgetBytes) {
    $over = $finalSize - $BudgetBytes
    Write-Host "FAIL: Wasm size $finalSize bytes exceeds budget of $BudgetBytes bytes ($over bytes over)!" -ForegroundColor Red
    Write-Host 'Consider: feature flags, LTO, dead-code elimination, or raising the budget.' -ForegroundColor Yellow
    $exitCode = 1
} else {
    $under = $BudgetBytes - $finalSize
    Write-Host "PASS: Wasm size $finalSize bytes is within budget ($under bytes to spare)." -ForegroundColor Green
}

Write-Host ''
if ($exitCode -eq 0) {
    Write-Host '=== WASM SIZE BUDGET TEST PASSED ===' -ForegroundColor Green
} else {
    Write-Host '=== WASM SIZE BUDGET TEST FAILED ===' -ForegroundColor Red
}
exit $exitCode

