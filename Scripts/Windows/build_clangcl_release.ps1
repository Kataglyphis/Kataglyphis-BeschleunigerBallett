<#
.SYNOPSIS
Starts the Clang-CL Release build and sets all necessary environment variables.
#>

#requires -Version 7.0

$ErrorActionPreference = "Stop"

$VulkanVersion = "1.4.341.1" # Customize based on your installation
$VulkanSdkRoot = "C:\VulkanSDK\$VulkanVersion"

if (Test-Path $VulkanSdkRoot) {
    Write-Host "Vulkan SDK found at $VulkanSdkRoot."
    $env:VULKAN_SDK = $VulkanSdkRoot
    
    $VulkanBin = Join-Path $VulkanSdkRoot "Bin"
    $VulkanLib = Join-Path $VulkanSdkRoot "Lib"
    $VulkanInclude = Join-Path $VulkanSdkRoot "Include"
    
    if ($env:PATH -notmatch [regex]::Escape($VulkanBin)) {
        $env:PATH = "$VulkanBin;$VulkanLib;$VulkanInclude;$env:PATH"
    }
} else {
    Write-Warning "Vulkan SDK not found at $VulkanSdkRoot. Make sure it's installed."
}

# Extract environment variables set by MSVC's vcvars64.bat so clang-cl can use them
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vsPath) {
        $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path $vcvars) {
            Write-Host "Loading MSVC environment variables from: $vcvars"
            # Launch cmd to run vcvars, print environment variables, and import them into PowerShell
            cmd.exe /c "`"$vcvars`" > nul && set" | ForEach-Object {
                if ($_ -match "^(.*?)=(.*)$") {
                    $key = $matches[1]
                    $value = $matches[2]
                    Set-Item -Path "env:\$key" -Value $value -ErrorAction SilentlyContinue
                }
            }
        }
    }
} else {
    Write-Warning "vswhere.exe not found. Continuing with current PATH."
}

# Add LLVM/Clang tools to PATH if they are missing
$LLVMPath = "C:\Program Files\LLVM\bin"
if (Test-Path $LLVMPath) {
    if ($env:PATH -notmatch [regex]::Escape($LLVMPath)) {
        $env:PATH = "$LLVMPath;$env:PATH"
    }
}

# Verify sccache/ninja are accessible, which are used by the preset
$ScoopShims = "$env:USERPROFILE\scoop\shims"
if (Test-Path $ScoopShims) {
    if ($env:PATH -notmatch [regex]::Escape($ScoopShims)) {
        $env:PATH = "$ScoopShims;$env:PATH"
    }
}

Write-Host "`n=== Configuring CMake for x64-ClangCL-Windows-Release ==="
$ProjectRoot = (Resolve-Path "$PSScriptRoot\..\..").Path
Set-Location $ProjectRoot

cmake --preset x64-MSVC-Windows-Release -B build_release
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

Write-Host "`n=== Building x64-ClangCL-Windows-Release ==="
cmake --build build_release --config Release
if ($LASTEXITCODE -ne 0) { throw "CMake build failed." }

Write-Host "`nBuild completed successfully!"

