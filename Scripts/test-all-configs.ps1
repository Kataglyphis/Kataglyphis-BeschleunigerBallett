# Test all four clang-cl configurations in one session, plus (when available)
# the Linux TSan build.
#
# One-shot correctness gate: runs the three standard Windows container builds
# (debug+ASAN, profile, release) and, on hosts with Rancher Desktop / Docker
# Linux container support, the Linux ThreadSanitizer build.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\Scripts\test-all-configs.ps1
#
# Returns the aggregate exit code (non-zero if ANY build failed).

[CmdletBinding()]
param(
#requires -Version 7.0

    # Comma-separated list of Windows configurations to build.
    [string]$WindowsConfigurations = 'clangcl-debug,clangcl-profile,clangcl-release',
    # Linux CMake preset.
    [string]$LinuxPreset = 'linux-debug-tsan-clang',
    # Passed through to Build-Windows-Container.ps1.
    [switch]$FreshContainer,
    # Also run the test suite after each build (Windows only).
    [switch]$RunTests,
    # Skip the Linux TSan build entirely.
    [switch]$SkipLinux
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$exitCode = 0

Write-Host '=== Windows container builds ===' -ForegroundColor Cyan
Write-Host "Configurations: $WindowsConfigurations" -ForegroundColor Cyan
Write-Host ''

$winArgs = @(
    '-Configurations', $WindowsConfigurations,
    '-SkipPerfTests'
)
if (-not $RunTests) { $winArgs += '-SkipTests' }
if ($FreshContainer) { $winArgs += '-FreshContainer' }

$winScript = Join-Path $PSScriptRoot 'Windows\Build-Windows-Container.ps1'
if (-not (Test-Path $winScript)) {
    Write-Host "WARNING: Windows build script not found at $winScript - skipping." -ForegroundColor Yellow
} else {
    try {
        & $winScript @winArgs
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Windows builds FAILED (exit code $LASTEXITCODE)." -ForegroundColor Red
            $exitCode = $LASTEXITCODE
        } else {
            Write-Host "Windows builds PASSED." -ForegroundColor Green
        }
    } catch {
        Write-Host "Windows builds threw: $_" -ForegroundColor Red
        $exitCode = 1
    }
}

Write-Host ''
Write-Host '=== Linux TSan build ===' -ForegroundColor Cyan

if ($SkipLinux) {
    Write-Host 'Skipped ( -SkipLinux ).' -ForegroundColor Yellow
} else {
    # Detect Docker Linux container support by trying to run a trivial Linux
    # container. This works with Rancher Desktop, Docker Desktop (Linux mode),
    # or any Docker installation that can run Linux containers.
    $linuxAvailable = $false
    try {
        $testId = docker run --rm --platform linux/amd64 alpine echo 'linux-ok' 2>$null
        if ($testId -eq 'linux-ok') { $linuxAvailable = $true }
    } catch {
        # Linux containers not available on this host.
    }

    if (-not $linuxAvailable) {
        Write-Host 'Linux containers not available on this host - skipping.' -ForegroundColor Yellow
        Write-Host 'To test Linux builds, install Rancher Desktop or Docker Desktop with Linux container support.' -ForegroundColor Yellow
    } else {
        $linuxScript = Join-Path $PSScriptRoot 'Linux\cmake-configure-build.sh'
        if (-not (Test-Path $linuxScript)) {
            Write-Host "WARNING: Linux build script not found at $linuxScript - skipping." -ForegroundColor Yellow
        } else {
            Write-Host "Running: $linuxScript --preset $LinuxPreset" -ForegroundColor Cyan
            try {
                # The Linux script expects to run inside a container or on a
                # Linux host; we invoke it through Docker with a bind mount.
                $buildDir = "build-linux-tsan"
                docker run --rm `
                    --platform linux/amd64 `
                    -v "${repoRoot}:/workspace" `
                    -w /workspace `
                    ghcr.io/kataglyphis/kataglyphis_beschleuniger:latest-cross `
                    bash -c "
                        set -e
                        bash /workspace/Scripts/Linux/cmake-configure-build.sh \
                            --preset $LinuxPreset \
                            --build-dir $buildDir \
                            --skip-configure false
                    "
                if ($LASTEXITCODE -ne 0) {
                    Write-Host "Linux TSan build FAILED (exit code $LASTEXITCODE)." -ForegroundColor Red
                    $exitCode = $LASTEXITCODE
                } else {
                    Write-Host "Linux TSan build PASSED." -ForegroundColor Green
                }
            } catch {
                Write-Host "Linux TSan build threw: $_" -ForegroundColor Red
                $exitCode = 1
            }
        }
    }
}

Write-Host ''
if ($exitCode -eq 0) {
    Write-Host '=== ALL BUILDS PASSED ===' -ForegroundColor Green
} else {
    Write-Host "=== SOME BUILDS FAILED (aggregate exit code $exitCode) ===" -ForegroundColor Red
}

exit $exitCode

