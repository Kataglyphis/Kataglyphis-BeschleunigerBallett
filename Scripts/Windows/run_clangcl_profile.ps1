<#
.SYNOPSIS
Starts the compiled executable inside the profile directory and sets necessary environment variables.
#>

param (
    [string]$ExeName = "GraphicsEngine.exe",
    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$ExeArgs
)

$ErrorActionPreference = "Stop"

# 2. Locate the built executable 

$ProjectRoot = (Resolve-Path "$PSScriptRoot\..\..").Path

# Search in known build directories created by CMake presets
$ProfileDir = Join-Path $ProjectRoot "build-clangcl-profile"

# Attempt to find the exe path (fallback for single or multi-config generators)
$ExePath = Join-Path $ProfileDir $ExeName

if (-not (Test-Path $ExePath)) {
    $ExePath = Join-Path $ProfileDir "bin\$ExeName"
}
if (-not (Test-Path $ExePath)) {
    $ExePath = Join-Path $ProfileDir "bin\Profile\$ExeName"
}
if (-not (Test-Path $ExePath)) {
    $ExePath = Join-Path $ProfileDir "Profile\$ExeName"
}
if (-not (Test-Path $ExePath)) {
    $ExePath = Join-Path $ProfileDir "bin\RelWithDebInfo\$ExeName"
}
if (-not (Test-Path $ExePath)) {
    $ExePath = Join-Path $ProfileDir "RelWithDebInfo\$ExeName"
}

if (-not (Test-Path $ExePath)) {
    throw "Executable '$ExeName' not found inside $ProfileDir. Please build the profile preset first."
}

# 3. Start the application
# We use the project root as the working directory so it can discover `images/` and `Resources/` 
$WorkDir = $ProjectRoot

Write-Host "Starting $ExePath..."
Write-Host "Working Directory: $WorkDir"

# Use Start-Process so it executes properly without locking up the script but waits for completion

try {
    # Force Vulkan to use the Proprietary AMD Driver to prevent AMD open-source driver access violations
    $ProprietaryDriver = "C:\WINDOWS\System32\DriverStore\FileRepository\u0198974.inf_amd64_dcac9659486b668a\B025819\amd-vulkan64.json"
    if (Test-Path $ProprietaryDriver) {
        $env:VK_ICD_FILENAMES = $ProprietaryDriver
    }

    Set-Location -Path $WorkDir
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
