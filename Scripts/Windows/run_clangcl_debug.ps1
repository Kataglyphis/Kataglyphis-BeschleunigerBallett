#requires -Version 7.0
<#
.SYNOPSIS
Runs the local clang-cl debug test flow, optional fuzz tests, and then launches the app.
#>

param (
    [string]$ExeName = "GraphicsEngine.exe",
    [switch]$SkipTests,
    [switch]$SkipFuzzTests,
    [switch]$RunVulkanIntegrationTest,
    [switch]$SkipAppLaunch,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExeArgs
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest


$ProjectRoot = (Resolve-Path "$PSScriptRoot\..\..").Path
$DebugDir = Join-Path $ProjectRoot 'build-clangcl-debug'
$FuzzDir = $DebugDir

. (Join-Path $PSScriptRoot 'Resolve-BuildModule.ps1')
Import-BuildModule @('WindowsBuild.Common', 'WindowsTesting.Common')

function Add-DirectoryToPath {
    param([string]$Directory)

    if ([string]::IsNullOrWhiteSpace($Directory) -or -not (Test-Path $Directory)) {
        return
    }

    $resolvedDirectory = (Resolve-Path $Directory).Path
    $currentEntries = @($env:PATH -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    $remainingEntries = @($currentEntries | Where-Object { $_ -ne $resolvedDirectory })
    $env:PATH = (@($resolvedDirectory) + $remainingEntries) -join ';'
}

function Add-DirectoriesToPath {
    param([string[]]$Directories)

    foreach ($directory in $Directories) {
        Add-DirectoryToPath $directory
    }
}

function Get-PreferredToolPath {
    param(
        [Parameter(Mandatory)]
        [string]$CommandName,
        [string[]]$CandidatePaths = @()
    )

    foreach ($candidate in $CandidatePaths) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path $candidate)) {
            return (Resolve-Path $candidate).Path
        }
    }

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    return $null
}

function Resolve-PreferredTool {
    param(
        [Parameter(Mandatory)]
        [string]$CommandName,
        [string[]]$CandidatePaths = @()
    )

    $toolPath = Get-PreferredToolPath -CommandName $CommandName -CandidatePaths $CandidatePaths
    if ($toolPath) {
        Add-DirectoryToPath (Split-Path $toolPath -Parent)
    }

    return $toolPath
}

function Get-CMakeShareDir {
    param([string]$CMakeExePath)

    if ([string]::IsNullOrWhiteSpace($CMakeExePath)) {
        return $null
    }

    $cmakeBinDir = Split-Path $CMakeExePath -Parent
    $cmakeRoot = Split-Path $cmakeBinDir -Parent
    $shareRoot = Join-Path $cmakeRoot 'share'
    if (-not (Test-Path $shareRoot)) {
        return $null
    }

    $shareDir = Get-ChildItem -Path $shareRoot -Directory -Filter 'cmake-*' -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        Select-Object -First 1

    if (-not $shareDir) {
        return $null
    }

    return $shareDir.FullName.Replace('\', '/')
}

function Update-CTestMetadataPaths {
    param(
        [Parameter(Mandatory)]
        [string]$BuildRoot,
        [Parameter(Mandatory)]
        [string]$WorkspaceRoot,
        [string]$CMakeShareDir
    )

    if (-not (Test-Path $BuildRoot)) {
        return
    }

    $workspacePathUnix = $WorkspaceRoot.Replace('\', '/')
    $files = Get-ChildItem -Path $BuildRoot -Recurse -File -Include 'CTestTestfile.cmake', 'DartConfiguration.tcl', '*_include.cmake', '*_discovery.cmake', '*_tests.cmake' -ErrorAction SilentlyContinue
    $asciiEncoding = [System.Text.Encoding]::ASCII

    foreach ($file in $files) {
        $originalContent = [System.IO.File]::ReadAllText($file.FullName)
        $updatedContent = $originalContent.Replace('C:/workspace', $workspacePathUnix)

        if (-not [string]::IsNullOrWhiteSpace($CMakeShareDir)) {
            $updatedContent = [regex]::Replace($updatedContent, 'C:/Program Files/CMake/share/cmake-[0-9.]+', $CMakeShareDir)
            $updatedContent = [regex]::Replace($updatedContent, 'C:/Strawberry/c/share/cmake-[0-9.]+', $CMakeShareDir)
        }

        if ($updatedContent -cne $originalContent) {
            [System.IO.File]::WriteAllText($file.FullName, $updatedContent, $asciiEncoding)
        }
    }
}

function Resolve-AppExecutablePath {
    param(
        [Parameter(Mandatory)]
        [string]$BuildRoot,
        [Parameter(Mandatory)]
        [string]$ExecutableName
    )

    $candidateRelativePaths = @(
        $ExecutableName,
        (Join-Path 'bin' $ExecutableName),
        (Join-Path 'bin' (Join-Path 'Debug' $ExecutableName)),
        (Join-Path 'Debug' $ExecutableName)
    )

    foreach ($relativePath in $candidateRelativePaths) {
        $candidate = Join-Path $BuildRoot $relativePath
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    $foundExecutable = Get-ChildItem -Path $BuildRoot -Filter $ExecutableName -File -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($foundExecutable) {
        return $foundExecutable.FullName
    }

    return $null
}

$cmakeExePath = Resolve-PreferredTool -CommandName 'cmake.exe' -CandidatePaths @(
    'C:\Program Files\CMake\bin\cmake.exe'
)
$ctestExePath = Resolve-PreferredTool -CommandName 'ctest.exe' -CandidatePaths @(
    'C:\Program Files\CMake\bin\ctest.exe'
)
$clangClExePath = Resolve-PreferredTool -CommandName 'clang-cl.exe' -CandidatePaths @(
    'C:\Program Files\LLVM\bin\clang-cl.exe',
    (Join-Path $env:USERPROFILE 'scoop\apps\llvm\current\bin\clang-cl.exe')
)

Add-DirectoriesToPath @(
    $DebugDir,
    (Join-Path $DebugDir 'bin'),
    $FuzzDir,
    (Join-Path $FuzzDir 'bin'),
    (Join-Path $ProjectRoot 'bin')
)

$context = New-BuildContext -Workspace $ProjectRoot -LogDir 'logs\windows' -StopOnError
$currentCMakeShareDir = Get-CMakeShareDir -CMakeExePath $cmakeExePath

if (-not $ctestExePath) {
    throw "ctest.exe not found. Install CMake or add it to PATH before running this script."
}

if (-not $clangClExePath) {
    Write-BuildLogWarning -Context $context -Message 'clang-cl.exe was not found on PATH or in the default LLVM install locations. Clang ASan runtime discovery may fail for fuzz tests.'
}

$WorkDir = $ProjectRoot

Open-BuildLog -Context $context

try {
    Set-Location -Path $WorkDir

    if (-not $SkipTests) {
        Write-BuildLog -Context $context -Message "Running tests via CTest in $DebugDir..."
        Update-CTestMetadataPaths -BuildRoot $DebugDir -WorkspaceRoot $ProjectRoot -CMakeShareDir $currentCMakeShareDir
        $excludeRegex = @()
        if (-not $RunVulkanIntegrationTest) {
            $excludeRegex += '^Integration\.VulkanEngine$'
            Write-BuildLogWarning -Context $context -Message 'Skipping Integration.VulkanEngine in the local host runner. Use -RunVulkanIntegrationTest to include it explicitly.'
        }

        Invoke-CtestDiscoveredTests -Context $context -BuildRoot $DebugDir -Configuration 'Debug' -ExcludeRegex $excludeRegex -RuntimeFlavor 'Clang'

        if (-not $SkipFuzzTests) {
            if (Test-Path $FuzzDir) {
                Write-BuildLog -Context $context -Message "Running local fuzz executables in $FuzzDir..."

                foreach ($fuzzExecutable in @('first_fuzz_test.exe', 'example_fuzz_test.exe')) {
                    $resolvedFuzzExecutable = Resolve-TestExecutable -BuildRoot $FuzzDir -ExecutableName $fuzzExecutable
                    if (-not $resolvedFuzzExecutable) {
                        throw "Expected fuzz executable '$fuzzExecutable' was not found inside $FuzzDir."
                    }

                    $ranFuzzExecutable = Invoke-ManualTestExecutable -Context $context -BuildRoot $FuzzDir -ExecutableName $fuzzExecutable -RuntimeFlavor 'Clang'
                    if (-not $ranFuzzExecutable) {
                        throw "Fuzz executable '$fuzzExecutable' did not start successfully. Check the Clang ASan runtime setup."
                    }
                }
            } else {
                Write-BuildLogWarning -Context $context -Message "Fuzz build directory '$FuzzDir' does not exist. Skipping fuzz executable run."
            }
        }
    }

    if ($SkipAppLaunch) {
        Write-BuildLog -Context $context -Message 'Skipping application launch because -SkipAppLaunch was requested.'
        return
    }

    $ExePath = Resolve-AppExecutablePath -BuildRoot $DebugDir -ExecutableName $ExeName
    if (-not $ExePath) {
        throw "Executable '$ExeName' not found inside $DebugDir. Please run the 'build_clangcl_debug.ps1' script first."
    }

    Write-BuildLog -Context $context -Message "Starting $ExePath..."
    Write-BuildLog -Context $context -Message "Working Directory: $WorkDir"

    # Force Vulkan to use the proprietary AMD driver instead of the AMDVLK open-source driver.
    $proprietaryDriver = 'C:\WINDOWS\System32\DriverStore\FileRepository\u0198974.inf_amd64_dcac9659486b668a\B025819\amd-vulkan64.json'
    if (Test-Path $proprietaryDriver) {
        $env:VK_ICD_FILENAMES = $proprietaryDriver
    }

    $env:VK_LAYER_PATH = ''
    $env:VK_INSTANCE_LAYERS = ''

    # App-run ASAN options differ from the test-run defaults on purpose:
    # report_globals=0 + windows_hook_rtl_allocators=false silence GUI/driver
    # global-init and RTL-allocator noise the test binaries do not have.
    Invoke-WithAsanOptions -Options 'log_path=logs/asan.log:report_globals=0:windows_hook_rtl_allocators=false' -Script {
        if ($ExeArgs) {
            & $ExePath $ExeArgs
        } else {
            & $ExePath
        }

        $exitCode = $LASTEXITCODE
        if ($exitCode -eq -1073740791) {
            Write-Error "Vulkan validation layers are missing or VulkanSDK is not installed. Install it with 'winget install VulkanSDK'."

            $vulkanSdkRoot = $null
            if (Test-Path 'C:\VulkanSDK') {
                $vulkanSdkRoot = (Get-ChildItem -Path 'C:\VulkanSDK' -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
            }

            if ($null -ne $vulkanSdkRoot -and (Test-Path $vulkanSdkRoot)) {
                $env:VULKAN_SDK = $vulkanSdkRoot
                $vulkanBin = Join-Path $vulkanSdkRoot 'Bin'
                $vulkanLib = Join-Path $vulkanSdkRoot 'Lib'
                Add-DirectoryToPath $vulkanLib
                Add-DirectoryToPath $vulkanBin
                $env:VK_LAYER_PATH = $vulkanBin
                Write-BuildLog -Context $context -Message 'VulkanSDK environment variables were updated. Run the script again.'
            } else {
                Write-BuildLogWarning -Context $context -Message 'VulkanSDK could not be detected automatically. Install it and run the script again.'
            }
        } elseif ($exitCode -ne 0) {
            Write-BuildLogWarning -Context $context -Message "Process failed with exit code $exitCode"
        }
    }
} finally {
    Close-BuildLog -Context $context
}

