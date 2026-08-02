<#
.SYNOPSIS
Starts the compiled executable inside the release directory and sets necessary environment variables.
#>

#requires -Version 7.0

param (
    [string]$ExeName = "GraphicsEngine.exe",
    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$ExeArgs
)

$ErrorActionPreference = "Stop"

# 2. Locate the built executable 

$ProjectRoot = (Resolve-Path "$PSScriptRoot\..\..").Path

# Search in known build directories created by CMake presets
$ReleaseDir = Join-Path $ProjectRoot "build-clangcl-release"

# Attempt to find the exe path (fallback for single or multi-config generators)
$ExePath = Join-Path $ReleaseDir $ExeName

if (-not (Test-Path $ExePath)) {
    $ExePath = Join-Path $ReleaseDir "bin\$ExeName"
}
if (-not (Test-Path $ExePath)) {
    $ExePath = Join-Path $ReleaseDir "bin\Release\$ExeName"
}
if (-not (Test-Path $ExePath)) {
    $ExePath = Join-Path $ReleaseDir "Release\$ExeName"
}

if (-not (Test-Path $ExePath)) {
    throw "Executable '$ExeName' not found inside $ReleaseDir. Build it first: Build-Windows.ps1 -Configurations clangcl-release (or Build-Windows-Container.ps1)."
}

# 3. Start the application
# We use the project root as the working directory so it can discover `images/` and `Resources/` 
$WorkDir = $ProjectRoot

Write-Host "Starting $ExePath..."
Write-Host "Working Directory: $WorkDir"

# Use Start-Process so it executes properly without locking up the script but waits for completion
Set-Location -Path $WorkDir
try {
    if ($ExeArgs) {
        & $ExePath $ExeArgs
    } else {
        & $ExePath
    }
    
    $ExitCode = $LASTEXITCODE
    if ($ExitCode -ne 0) { 
        Write-Warning "Process failed with exit code $ExitCode" 
    }
    exit $ExitCode
} catch {
    Write-Warning "Failed to start $ExePath : $_"
    exit 1
}

