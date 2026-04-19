<#
.SYNOPSIS
Starts the compiled executable inside the debug directory and sets necessary environment variables.
#>

param (
    [string]$ExeName = "GraphicsEngine.exe",
    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$ExeArgs
)

$ErrorActionPreference = "Stop"

# Locate the built executable 

$ProjectRoot = (Resolve-Path "$PSScriptRoot\..\..").Path

# Setup Vulkan Environment
$VulkanSdkRoot = (Get-ChildItem -Path "C:\VulkanSDK" -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName

if ($null -ne $VulkanSdkRoot -and (Test-Path $VulkanSdkRoot)) {
    Write-Host "Vulkan SDK found at $VulkanSdkRoot. Setting up environment..."
    $env:VULKAN_SDK = $VulkanSdkRoot
    
    $VulkanBin = Join-Path $VulkanSdkRoot "Bin"
    $VulkanLib = Join-Path $VulkanSdkRoot "Lib"
    
    if ($env:PATH -notmatch [regex]::Escape($VulkanBin)) {
        $env:PATH = "$VulkanBin;$VulkanLib;$env:PATH"
    }
    $env:VK_LAYER_PATH = $VulkanBin
}

# Add project bin directories to PATH (for ASAN DLLs, etc.)
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
    Set-Location -Path $WorkDir

    # Run tests using CTest before starting the application
    Write-Host "Running tests via CTest in $DebugDir..."

    # Rewrite container absolute paths to host absolute paths for CTest
    $OriginalWorkspace = "C:/workspace"
    $CurrentWorkspace = $ProjectRoot.Replace('\', '/')
    $OriginalCMake = "C:/Program Files/CMake/share/cmake-4.3"
    $CurrentCMake = "C:/Program Files/CMake/share/cmake-4.2"

    if (($OriginalWorkspace -ne $CurrentWorkspace) -or ($OriginalCMake -ne $CurrentCMake)) {
        Get-ChildItem -Path $DebugDir -Include "CTestTestfile.cmake", "DartConfiguration.tcl", "*_include.cmake" -Recurse | ForEach-Object {
            $content = Get-Content $_.FullName -Raw
            $modified = $false
            if ($content -match [regex]::Escape($OriginalWorkspace)) {
                $content = $content.Replace($OriginalWorkspace, $CurrentWorkspace)
                $modified = $true
            }
            if ($content -match [regex]::Escape($OriginalCMake)) {
                $content = $content.Replace($OriginalCMake, $CurrentCMake)
                $modified = $true
            }
            if ($modified) {
                Set-Content -Path $_.FullName -Value $content -NoNewline
            }
        }
    }

    $OldAsanOptions = $env:ASAN_OPTIONS
    # Use minimal AddressSanitizer options to prevent interfering with AMD's internal allocations
    $env:ASAN_OPTIONS = "log_path=logs/asan.log:report_globals=0:windows_hook_rtl_allocators=false:$OldAsanOptions"
    
    try {
        Push-Location $DebugDir
        & ctest -C Debug --output-on-failure
        if ($LASTEXITCODE -ne 0) {
            throw "Tests failed with exit code $LASTEXITCODE. Aborting application launch."
        }
    } finally {
        Pop-Location
    }
    
    Write-Host "Starting $ExePath..."
    Write-Host "Working Directory: $WorkDir"

    # Force Vulkan to use the Proprietary AMD Driver instead of the buggy amdvlk open-source driver
    $ProprietaryDriver = "C:\WINDOWS\System32\DriverStore\FileRepository\u0198974.inf_amd64_dcac9659486b668a\B025819\amd-vulkan64.json"
    if (Test-Path $ProprietaryDriver) {
        $env:VK_ICD_FILENAMES = $ProprietaryDriver
    }
    
    # Also disable Vulkan validation layers if they cause intercepts
    $env:VK_LAYER_PATH = ""
    $env:VK_INSTANCE_LAYERS = ""

    # Use minimal AddressSanitizer options to prevent interfering with AMD's internal allocations
    $OldAsanOptions = $env:ASAN_OPTIONS
    $env:ASAN_OPTIONS = "log_path=logs/asan.log:report_globals=0:windows_hook_rtl_allocators=false:$OldAsanOptions"

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
    } finally {
    if ($null -ne $OldAsanOptions) {
        $env:ASAN_OPTIONS = $OldAsanOptions
    } else {
        Remove-Item Env:\ASAN_OPTIONS -ErrorAction SilentlyContinue
    }
}