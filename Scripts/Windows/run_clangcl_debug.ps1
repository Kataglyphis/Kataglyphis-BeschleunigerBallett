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
        $CTestPath = Get-Command ctest -ErrorAction SilentlyContinue
        if ($null -eq $CTestPath) {
            # Versuche, CTest aus dem Standard-CMake-Installationsverzeichnis zu finden
            $cmakeBin = "C:\Program Files\CMake\bin"
            if (Test-Path "$cmakeBin\ctest.exe") {
                $env:PATH = "$cmakeBin;$env:PATH"
                $CTestPath = Get-Command ctest -ErrorAction SilentlyContinue
            }
        }
        if ($null -eq $CTestPath) {
            Write-Warning "CTest wurde nicht gefunden. Bitte installiere CMake mit 'winget install cmake'. Die Anwendung wird trotzdem gestartet."
        } else {
            $ctestCmd = Get-Command ctest -ErrorAction SilentlyContinue
            if ($null -eq $ctestCmd) {
                Write-Warning "ctest wurde nicht gefunden (unerwartet)."
                $ctestOutput = ""
            } else {
                $outFile = Join-Path $env:TEMP 'ctest_out.txt'
                $errFile = Join-Path $env:TEMP 'ctest_err.txt'
                Remove-Item $outFile,$errFile -ErrorAction SilentlyContinue
                $proc = Start-Process -FilePath $ctestCmd.Source -ArgumentList @('-C','Debug','--output-on-failure') -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile
                $ctestOutput = ''
                if (Test-Path $outFile) { $ctestOutput += Get-Content $outFile -Raw }
                if (Test-Path $errFile) { $ctestOutput += "`n" + (Get-Content $errFile -Raw) }
                Write-Host $ctestOutput
            }
            if ($ctestOutput -match "No tests were found") {
                Write-Warning "CTests reported no registered tests. Suche nach Test-Executables als Fallback..."
                $testExes = Get-ChildItem -Path $DebugDir -Recurse -File | Where-Object { ($_.Extension -ieq '.exe') -and ($_.Name -match '(?i)test') }
                if ($testExes.Count -eq 0) {
                    Write-Warning "Keine Test-Executables gefunden."
                } else {
                    foreach ($exe in $testExes) {
                        Write-Host "Starte Test-Executable: $($exe.FullName)"
                        Push-Location $exe.DirectoryName
                        & $exe.FullName
                        $code = $LASTEXITCODE
                        if ($code -ne 0) {
                            Write-Warning "Test $($exe.Name) endete mit Exit-Code $code"
                        }
                        Pop-Location
                    }
                }
            } else {
                if ($LASTEXITCODE -ne 0) {
                    throw "Tests failed with exit code $LASTEXITCODE. Aborting application launch."
                }
            }
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
        if ($ExitCode -eq -1073740791) {
            Write-Error "Vulkan-Validation-Layer-Fehler: Die Anwendung konnte nicht korrekt gestartet werden, weil Vulkan Validation Layers fehlen oder das VulkanSDK nicht installiert ist.\nBitte installiere das VulkanSDK mit 'winget install VulkanSDK'.\nDas Skript versucht, die Umgebungsvariablen automatisch zu setzen."
            # Versuche, VulkanSDK zu finden und Umgebungsvariablen zu setzen
            $VulkanSdkRoot = $null
            if (Test-Path "C:\VulkanSDK") {
                $VulkanSdkRoot = (Get-ChildItem -Path "C:\VulkanSDK" -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
            }
            if ($null -ne $VulkanSdkRoot -and (Test-Path $VulkanSdkRoot)) {
                $env:VULKAN_SDK = $VulkanSdkRoot
                $VulkanBin = Join-Path $VulkanSdkRoot "Bin"
                $VulkanLib = Join-Path $VulkanSdkRoot "Lib"
                if ($env:PATH -notmatch [regex]::Escape($VulkanBin)) {
                    $env:PATH = "$VulkanBin;$VulkanLib;$env:PATH"
                }
                $env:VK_LAYER_PATH = $VulkanBin
                Write-Host "VulkanSDK-Umgebungsvariablen wurden gesetzt. Bitte führe das Skript erneut aus."
            } else {
                Write-Warning "VulkanSDK konnte nicht automatisch gefunden werden. Bitte installiere es manuell und starte das Skript erneut."
            }
        } elseif ($ExitCode -ne 0) {
            Write-Warning "Process failed with exit code $ExitCode"
        }
    } finally {
        if ($null -ne $OldAsanOptions) {
            $env:ASAN_OPTIONS = $OldAsanOptions
        } else {
            Remove-Item Env:\ASAN_OPTIONS -ErrorAction SilentlyContinue
    }
}