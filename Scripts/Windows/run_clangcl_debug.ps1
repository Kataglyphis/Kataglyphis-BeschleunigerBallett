<#
.SYNOPSIS
Starts the compiled GraphicsEngine executable inside the debug directory and sets necessary environment variables.
#>

$ErrorActionPreference = "Stop"

# Locate the built executable 
$ExeName = "GraphicsEngine.exe"

$ProjectRoot = (Resolve-Path "$PSScriptRoot\..\..").Path

# Add project bin directories to PATH (for ASAN DLLs, Vulkan DLLs, etc.)
$env:PATH = "$(Join-Path $ProjectRoot 'build-clangcl-debug\bin');$(Join-Path $ProjectRoot 'bin');$env:PATH"

# Search in known build directories created by CMake presets
$DebugDir = Join-Path $ProjectRoot "build-clangcl-debug"

# Attempt to find the exe path (fallback for single or multi-config generators)
$ExePath = Join-Path $DebugDir $ExeName

if (-not (Test-Path $ExePath)) {
    $ExePath = Join-Path $DebugDir "bin\$ExeName"
}
if (-not (Test-Path $ExePath)) {
    $ExePath = Join-Path $DebugDir "bin\Debug\$ExeName"
}
if (-not (Test-Path $ExePath)) {
    $ExePath = Join-Path $DebugDir "Debug\$ExeName"
}

if (-not (Test-Path $ExePath)) {
    throw "Executable '$ExeName' not found inside $DebugDir. Please run the 'build_clangcl_debug.ps1' script first."
}

$ExeDir = Split-Path $ExePath

# 3. Start the application
# We use the project root as the working directory so it can discover `images/` and `Resources/` 
$WorkDir = $ProjectRoot

Write-Host "Starting $ExePath..."
Write-Host "Working Directory: $WorkDir"

# Use Start-Process so it executes properly without locking up the script but waits for completion

# Enable AddressSanitizer logging for the run script
$OldAsanOptions = $env:ASAN_OPTIONS
$env:ASAN_OPTIONS = "log_path=asan.log:report_globals=1:$OldAsanOptions"

try {
    Start-Process -FilePath $ExePath -WorkingDirectory $WorkDir -Wait -NoNewWindow
} finally {
    if ($null -ne $OldAsanOptions) {
        $env:ASAN_OPTIONS = $OldAsanOptions
    } else {
        Remove-Item Env:\ASAN_OPTIONS -ErrorAction SilentlyContinue
    }
}