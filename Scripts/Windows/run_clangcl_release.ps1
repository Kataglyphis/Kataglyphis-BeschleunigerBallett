<#
.SYNOPSIS
Starts the compiled GraphicsEngine executable inside the release directory and sets necessary environment variables.
#>

$ErrorActionPreference = "Stop"

# 1. Setup Vulkan Environment (required to run correctly and find layers/dlls)
$VulkanVersion = "1.4.321.1" # Must match your system's version
$VulkanSdkRoot = "C:\VulkanSDK\$VulkanVersion"

if (Test-Path $VulkanSdkRoot) {
    Write-Host "Vulkan SDK found at $VulkanSdkRoot. Setting up environment..."
    $env:VULKAN_SDK = $VulkanSdkRoot
    
    $VulkanBin = Join-Path $VulkanSdkRoot "Bin"
    $VulkanLib = Join-Path $VulkanSdkRoot "Lib"
    
    # Add Vulkan binary directory so the loader and tools can be found
    if ($env:PATH -notmatch [regex]::Escape($VulkanBin)) {
        $env:PATH = "$VulkanBin;$VulkanLib;$env:PATH"
    }

    # Sometimes VK_LAYER_PATH is necessary even in release if debugging/validation is active via config
    $env:VK_LAYER_PATH = $VulkanBin
} else {
    # Write-Warning "Vulkan SDK not found at $VulkanSdkRoot. Execution might fail if dynamic libraries are missing."
}

# 2. Locate the built executable 
$ExeName = "GraphicsEngine.exe"

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
    throw "Executable '$ExeName' not found inside $ReleaseDir. Please run the 'build_clangcl_release.ps1' script first."
}

# 3. Start the application
# We use the project root as the working directory so it can discover `images/` and `Resources/` 
$WorkDir = $ProjectRoot

Write-Host "Starting $ExePath..."
Write-Host "Working Directory: $WorkDir"

# Use Start-Process so it executes properly without locking up the script but waits for completion
Start-Process -FilePath $ExePath -WorkingDirectory $WorkDir -Wait -NoNewWindow
