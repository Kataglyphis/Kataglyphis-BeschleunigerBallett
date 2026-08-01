#requires -Version 7.0
<#
.SYNOPSIS
Runs the GPU golden/integration test suites with Vulkan synchronization
validation enabled and fails if the log contains a SYNC-HAZARD.

.DESCRIPTION
Turns the manual "hand-write a vk_layer_settings.txt, point VK_LAYER_PATH at
the SDK, run the suite, eyeball the log for SYNC-HAZARD" procedure described
in BACKLOG.md ("Recurring validation runs" -> "Synchronization validation")
into one command. That procedure found 10 real WRITE-AFTER-WRITE hazards in
July 2026; in practice nobody re-runs a manual procedure, so this exists to
make it something a "before touching render passes/barriers/frames-in-flight"
habit can actually be.

Deliberately NOT wired into CI: the GoldenRender/Integration suites need a
GPU and skip everywhere except a host with one (see docs/gpu-golden-testing.md),
so a CI gate here would be vacuous. Run it locally instead, the same way
Scripts/Compare-PerfBaseline.ps1 is a local-only tool.

.PARAMETER ExecutablePath
Path to the built commitTestSuite.exe. Defaults to the repo root, falling
back to build-clangcl-debug\ (where the container build script leaves it).

.PARAMETER GtestFilter
Passed through as --gtest_filter. Defaults to the two GPU suites.

.PARAMETER VulkanSdkBin
Directory containing the Khronos validation layer. Defaults to the SDK
version documented in AGENTS.md.

.PARAMETER LogDir
Where the tee'd run log is written, one timestamped file per run.

.PARAMETER LogFixturePath
Test-only escape hatch: skip running the executable entirely and just
evaluate an existing log file for SYNC-HAZARD lines, then exit accordingly.
Exists so the exit-code contract (non-zero iff a hazard is present) can be
Pester-tested without a GPU - see Scripts/Windows/tests/Run-SyncValidation.Tests.ps1.
#>

param(
    [string]$ExecutablePath,
    [string]$GtestFilter = 'GoldenRender.*:Integration.*',
    [string]$VulkanSdkBin = 'C:\VulkanSDK\1.4.350.0\Bin',
    [string]$LogDir = (Join-Path $PSScriptRoot '..\..\logs\sync-validation'),
    [string]$LogFixturePath
)

$ErrorActionPreference = 'Stop'

function Get-SyncHazardLines {
    param(
        [Parameter(Mandatory)]
        [string]$LogPath
    )

    if (-not (Test-Path $LogPath)) {
        throw "Log file not found at '$LogPath'."
    }

    return @(Select-String -Path $LogPath -Pattern 'SYNC-HAZARD' -SimpleMatch -ErrorAction SilentlyContinue)
}

function Write-HazardReport {
    param(
        [Parameter(Mandatory)]
        [array]$HazardLines,
        [Parameter(Mandatory)]
        [string]$LogPath
    )

    Write-Host ''
    Write-Host "=== SYNCHRONIZATION HAZARDS DETECTED ($($HazardLines.Count)) ===" -ForegroundColor Red
    foreach ($line in $HazardLines) {
        Write-Host "  $($line.Line.Trim())" -ForegroundColor Red
    }
    Write-Host "Full log: $LogPath" -ForegroundColor Red
}

# Test-only path: evaluate a canned log instead of running the GPU suite, so
# the pass/fail contract is verifiable on a machine (or in CI) with no GPU.
if ($LogFixturePath) {
    $hazards = Get-SyncHazardLines -LogPath $LogFixturePath
    if ($hazards.Count -gt 0) {
        Write-HazardReport -HazardLines $hazards -LogPath $LogFixturePath
        exit 1
    }
    Write-Host '=== NO SYNCHRONIZATION HAZARDS DETECTED ===' -ForegroundColor Green
    exit 0
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$settingsSource = (Resolve-Path (Join-Path $PSScriptRoot '..\vk_layer_settings.txt')).Path

if (-not $ExecutablePath) {
    $candidates = @(
        (Join-Path $repoRoot 'commitTestSuite.exe'),
        (Join-Path $repoRoot 'build-clangcl-debug\commitTestSuite.exe')
    )
    $ExecutablePath = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $ExecutablePath) {
        throw ("commitTestSuite.exe not found at the repo root or in build-clangcl-debug\. Build it with " +
            "'pwsh -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows-Container.ps1 " +
            "-Configurations clangcl-debug -SkipTests' and docker cp it out (see AGENTS.md), " +
            "or pass -ExecutablePath explicitly.")
    }
} elseif (-not (Test-Path $ExecutablePath)) {
    throw "Executable not found at '$ExecutablePath'."
}
$ExecutablePath = (Resolve-Path $ExecutablePath).Path

if (-not (Test-Path $VulkanSdkBin)) {
    throw "Vulkan SDK Bin directory not found at '$VulkanSdkBin'. Pass -VulkanSdkBin pointing at an installed SDK."
}

if (-not (Test-Path $LogDir)) {
    New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
}
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logPath = Join-Path $LogDir "$timestamp.log"

# The loader reads vk_layer_settings.txt from the CWD or from the directory
# containing the executable - copy next to the executable so this works
# regardless of where the caller runs the exe from.
$settingsDest = Join-Path (Split-Path $ExecutablePath -Parent) 'vk_layer_settings.txt'
Copy-Item -Path $settingsSource -Destination $settingsDest -Force

$originalLayerPath = $env:VK_LAYER_PATH
$exitCode = 0
try {
    $env:VK_LAYER_PATH = $VulkanSdkBin

    # Resources/ (shaders, models, textures) is loaded via CWD-relative
    # paths - must run from the repo root regardless of where the exe lives
    # (docs/gpu-golden-testing.md).
    Push-Location $repoRoot
    try {
        Write-Host "Running $ExecutablePath --gtest_filter='$GtestFilter' with khronos_validation.validate_sync=true" -ForegroundColor Cyan
        & $ExecutablePath "--gtest_filter=$GtestFilter" 2>&1 | Tee-Object -FilePath $logPath
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
} finally {
    if ($null -ne $originalLayerPath) {
        $env:VK_LAYER_PATH = $originalLayerPath
    } else {
        Remove-Item Env:\VK_LAYER_PATH -ErrorAction SilentlyContinue
    }
    # Never leave a copy lying around next to the binary - it would silently
    # enable (expensive) sync validation on every later, unrelated run.
    Remove-Item -Path $settingsDest -Force -ErrorAction SilentlyContinue
}

$hazards = Get-SyncHazardLines -LogPath $logPath
if ($hazards.Count -gt 0) {
    Write-HazardReport -HazardLines $hazards -LogPath $logPath
    exit 1
}

Write-Host ''
if ($exitCode -ne 0) {
    Write-Host "Test executable exited with code $exitCode (no SYNC-HAZARD lines found, but the run itself failed - check $logPath)." -ForegroundColor Yellow
    exit $exitCode
}

Write-Host '=== NO SYNCHRONIZATION HAZARDS DETECTED ===' -ForegroundColor Green
exit 0
