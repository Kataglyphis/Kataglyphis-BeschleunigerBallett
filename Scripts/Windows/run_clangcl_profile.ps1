<#
.SYNOPSIS
Starts the compiled executable inside the profile directory and sets necessary environment variables.
#>

#requires -Version 7.0

param (
    [string]$ExeName = "GraphicsEngine.exe",
    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$ExeArgs
)

$ErrorActionPreference = "Stop"

$ProjectRoot = (Resolve-Path "$PSScriptRoot\..\..").Path

. (Join-Path $PSScriptRoot 'Resolve-BuildModule.ps1')
Import-BuildModule @('WindowsTesting.Common')

# Locate the built executable in the build directory created by the CMake preset
$ProfileDir = Join-Path $ProjectRoot "build-clangcl-profile"
$ExePath = Resolve-AppExecutablePath -BuildRoot $ProfileDir -ExecutableName $ExeName -Configurations @('Profile', 'RelWithDebInfo')

if (-not $ExePath) {
    throw "Executable '$ExeName' not found inside $ProfileDir. Please build the profile preset first."
}

# Start the application.
# We use the project root as the working directory so it can discover `images/` and `Resources/`
$WorkDir = $ProjectRoot

Write-Host "Starting $ExePath..."
Write-Host "Working Directory: $WorkDir"

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
