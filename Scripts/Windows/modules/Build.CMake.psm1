Set-StrictMode -Version Latest

function Remove-BuildRootSafe {
  param(
    [Parameter(Mandatory)]
    [pscustomobject]$Context,
    [Parameter(Mandatory)]
    [string]$Path,
    [Parameter(Mandatory)]
    [string]$Label
  )

  if (Remove-BuildRoot -Context $Context -Path $Path) {
    return
  }

  Write-BuildLogWarning -Context $Context -Message "Could not remove build directory ($Label): $Path. Continuing with in-place configure/build."
}

function Invoke-CmakeConfigureAndBuild {
  param(
    [Parameter(Mandatory)]
    [pscustomobject]$Context,
    [Parameter(Mandatory)]
    [string]$BuildPath,
    [Parameter(Mandatory)]
    [string]$Preset,
    [Parameter(Mandatory)]
    [string]$Configuration,
    [string[]]$ConfigureExtraArgs = @(),
    [switch]$CleanBuildRoot,
    [string]$CleanLabel = '',
    [int]$ParallelJobs = 0,
    [switch]$VerboseOutput,
    [switch]$DisableSccache
  )

  # Debug: Log system info
  $cpuCount = [Environment]::ProcessorCount
  $logicalProcessors = (Get-CimInstance -ClassName Win32_ComputerSystem).NumberOfLogicalProcessors
  Write-BuildLog -Context $Context -Message "DEBUG: ========== SYSTEM INFO =========="
  Write-BuildLog -Context $Context -Message "DEBUG: System CPU count (Environment): $cpuCount"
  Write-BuildLog -Context $Context -Message "DEBUG: System logical processors (WMI): $logicalProcessors"
  Write-BuildLog -Context $Context -Message "DEBUG: ParallelJobs parameter: $ParallelJobs"
  Write-BuildLog -Context $Context -Message "DEBUG: VerboseOutput mode: $VerboseOutput"
  Write-BuildLog -Context $Context -Message "DEBUG: DisableSccache: $DisableSccache"
  Write-BuildLog -Context $Context -Message "DEBUG: BuildPath: $BuildPath"
  Write-BuildLog -Context $Context -Message "DEBUG: Preset: $Preset"
  Write-BuildLog -Context $Context -Message "DEBUG: Configuration: $Configuration"

  # Set SCCACHE_MAX_JOBS if not already set - this is critical for parallelism!
  if (-not $env:SCCACHE_MAX_JOBS) {
    $env:SCCACHE_MAX_JOBS = $cpuCount.ToString()
    Write-BuildLog -Context $Context -Message "DEBUG: AUTO-SET SCCACHE_MAX_JOBS=$cpuCount (was not set, defaulting to CPU count)"
  } else {
    Write-BuildLog -Context $Context -Message "DEBUG: SCCACHE_MAX_JOBS already set to: $env:SCCACHE_MAX_JOBS"
  }

  # Disable sccache if requested
  if ($DisableSccache) {
    Write-BuildLog -Context $Context -Message "DEBUG: ========== DISABLING SCCACHE =========="
    $env:CMAKE_C_COMPILER_LAUNCHER = ""
    $env:CMAKE_CXX_COMPILER_LAUNCHER = ""
    $env:RUSTC_WRAPPER = ""
    $env:CC_WRAPPER = ""
    $env:CXX_WRAPPER = ""
    Write-BuildLog -Context $Context -Message "DEBUG: Cleared all compiler wrapper environment variables"
    
    # Try to stop sccache server
    $sccachePath = Get-Command 'sccache' -ErrorAction SilentlyContinue
    if ($sccachePath) {
      try {
        & sccache --stop-server 2>&1 | Out-Null
        Write-BuildLog -Context $Context -Message "DEBUG: Stopped sccache server"
      } catch {
        Write-BuildLog -Context $Context -Message "DEBUG: Could not stop sccache server (may not be running)"
      }
    }
  }

  if ($CleanBuildRoot) {
    $label = if ([string]::IsNullOrWhiteSpace($CleanLabel)) { $Preset } else { $CleanLabel }
    Remove-BuildRootSafe -Context $Context -Path $BuildPath -Label $label
  }

  $configureArgs = @('-B', $BuildPath, '--preset', $Preset)
  if ($ConfigureExtraArgs.Count -gt 0) {
    $configureArgs += $ConfigureExtraArgs
  }

  $buildArgs = @('--build', $BuildPath, '--config', $Configuration)
  if ($ParallelJobs -gt 0) {
    $buildArgs += @('--parallel', $ParallelJobs.ToString())
    Write-BuildLog -Context $Context -Message "DEBUG: Using --parallel $ParallelJobs (limited)"
  } else {
    $buildArgs += @('--parallel')
    Write-BuildLog -Context $Context -Message "DEBUG: Using --parallel (unlimited, will use all $cpuCount cores)"
  }

  # Add verbose flag to see actual commands being executed
  if ($VerboseOutput) {
    $buildArgs += @('--verbose')
    Write-BuildLog -Context $Context -Message "DEBUG: VerboseOutput mode enabled (--verbose)"
  }

  # Debug: Log full commands
  $configureCmd = "cmake $($configureArgs -join ' ')"
  $buildCmd = "cmake $($buildArgs -join ' ')"
  Write-BuildLog -Context $Context -Message "DEBUG: Configure command: $configureCmd"
  Write-BuildLog -Context $Context -Message "DEBUG: Build command: $buildCmd"

  # Check for ninja and log its version
  Write-BuildLog -Context $Context -Message "DEBUG: ========== NINJA INFO =========="
  $ninjaPath = Get-Command 'ninja' -ErrorAction SilentlyContinue
  if ($ninjaPath) {
    $ninjaVersion = & ninja --version 2>&1
    Write-BuildLog -Context $Context -Message "DEBUG: Ninja version: $ninjaVersion"
    Write-BuildLog -Context $Context -Message "DEBUG: Ninja path: $($ninjaPath.Source)"
    Write-BuildLog -Context $Context -Message "DEBUG: Ninja will use $cpuCount jobs by default (based on CPU count)"
  } else {
    Write-BuildLog -Context $Context -Message "DEBUG: Ninja not found on PATH"
  }

  # Check for sccache and log comprehensive diagnostics
  Write-BuildLog -Context $Context -Message "DEBUG: ========== SCCACHE DIAGNOSTICS (BEFORE BUILD) =========="
  $sccachePath = Get-Command 'sccache' -ErrorAction SilentlyContinue
  if ($sccachePath) {
    Write-BuildLog -Context $Context -Message "DEBUG: sccache found at: $($sccachePath.Source)"
    
    # Log all sccache-related environment variables
    Write-BuildLog -Context $Context -Message "DEBUG: --- sccache environment variables ---"
    $sccacheEnvVars = @(
      'SCCACHE_DIR', 'SCCACHE_CACHE_SIZE', 'SCCACHE_MAX_JOBS', 
      'SCCACHE_IDLE_TIMEOUT', 'SCCACHE_LOG', 'SCCACHE_ERROR_LOG',
      'SCCACHE_RECACHE', 'SCCACHE_SERVER_PORT', 'SCCACHE_NO_DAEMON',
      'SCCACHE_DIRECT', 'SCCACHE_BUCKET', 'SCCACHE_REDIS',
      'CMAKE_C_COMPILER_LAUNCHER', 'CMAKE_CXX_COMPILER_LAUNCHER',
      'RUSTC_WRAPPER', 'CC_WRAPPER', 'CXX_WRAPPER'
    )
    foreach ($var in $sccacheEnvVars) {
      $value = [Environment]::GetEnvironmentVariable($var)
      if ($value) {
        Write-BuildLog -Context $Context -Message "DEBUG:   $var = $value"
      } else {
        Write-BuildLog -Context $Context -Message "DEBUG:   $var = (not set)"
      }
    }
    
    # Try to start sccache server and get stats
    Write-BuildLog -Context $Context -Message "DEBUG: --- sccache server status ---"
    try {
      $sccacheStartResult = & sccache --start-server 2>&1
      Write-BuildLog -Context $Context -Message "DEBUG: sccache --start-server: $sccacheStartResult"
    } catch {
      Write-BuildLog -Context $Context -Message "DEBUG: sccache server already running or failed to start"
    }
    
    # Get full sccache stats
    Write-BuildLog -Context $Context -Message "DEBUG: --- sccache stats (full) ---"
    try {
      $sccacheStats = & sccache --show-stats 2>&1
      foreach ($line in $sccacheStats) {
        Write-BuildLog -Context $Context -Message "DEBUG:   $line"
      }
    } catch {
      Write-BuildLog -Context $Context -Message "DEBUG: Could not get sccache stats: $($_.Exception.Message)"
    }
    
    # Try to get advanced stats if available
    Write-BuildLog -Context $Context -Message "DEBUG: --- sccache internal stats ---"
    try {
      $sccacheInternalStats = & sccache --show-adv-stats 2>&1
      foreach ($line in $sccacheInternalStats) {
        Write-BuildLog -Context $Context -Message "DEBUG:   $line"
      }
    } catch {
      Write-BuildLog -Context $Context -Message "DEBUG: Advanced stats not available"
    }
  } else {
    Write-BuildLog -Context $Context -Message "DEBUG: sccache not found on PATH"
  }

  # Check other environment variables that might affect parallelism
  Write-BuildLog -Context $Context -Message "DEBUG: ========== OTHER BUILD ENVIRONMENT =========="
  $otherEnvVars = @(
    'CMAKE_BUILD_PARALLEL_LEVEL', 'NINJA_STATUS', 'NINJA_FORCE_COLOR',
    'CL', '_CL_', 'LINK', '_LINK_', 'LIB', 'LIBPATH', 'INCLUDE',
    'MAKEFLAGS', 'NUMBER_OF_PROCESSORS'
  )
  foreach ($var in $otherEnvVars) {
    $value = [Environment]::GetEnvironmentVariable($var)
    if ($value) {
      Write-BuildLog -Context $Context -Message "DEBUG:   $var = $value"
    }
  }

  Write-BuildLog -Context $Context -Message "DEBUG: ========== STARTING BUILD =========="
  Write-BuildLog -Context $Context -Message "DEBUG: Starting configure step..."
  Invoke-BuildExternal -Context $Context -File 'cmake' -Parameters $configureArgs | Out-Null
  Write-BuildLog -Context $Context -Message "DEBUG: Configure completed. Starting build step..."
  
  $buildStartTime = Get-Date
  Write-BuildLog -Context $Context -Message "DEBUG: Build started at $($buildStartTime.ToString('HH:mm:ss'))"
  
  # Use direct invocation with real-time streaming to avoid output capture issues
  # The Invoke-BuildExternal approach can lose output in Docker containers
  $buildCmdLine = "cmake $($buildArgs -join ' ')"
  Write-BuildLog -Context $Context -Message "CMD: $buildCmdLine"
  
  $global:LASTEXITCODE = 0
  try {
    # Stream output line by line for real-time logging and visibility
    & cmake @buildArgs 2>&1 | ForEach-Object {
      $line = $_.ToString()
      if (-not [String]::IsNullOrWhiteSpace($line)) {
        Write-BuildLog -Context $Context -Message $line
      }
    }
    $buildExitCode = $global:LASTEXITCODE
  } catch {
    Write-BuildLog -Context $Context -Message "ERROR: Build exception: $_"
    $buildExitCode = 1
  }
  
  $buildEndTime = Get-Date
  $buildDuration = $buildEndTime - $buildStartTime
  Write-BuildLog -Context $Context -Message "DEBUG: Build completed at $($buildEndTime.ToString('HH:mm:ss'))"
  Write-BuildLog -Context $Context -Message "DEBUG: Build duration: $($buildDuration.TotalSeconds.ToString('F2')) seconds ($($buildDuration.TotalMinutes.ToString('F2')) minutes)"
  Write-BuildLog -Context $Context -Message "DEBUG: Build exit code: $buildExitCode"
  
  if ($buildExitCode -ne 0) {
    throw "CMake build failed with exit code $buildExitCode"
  }

  # Check if this is a clang-cl debug build and copy the sanitizer dlls to the bin directory
  if ($Configuration -eq 'Debug' -and $Preset -match 'clang') {
    Write-BuildLog -Context $Context -Message "DEBUG: Post-build step: Copying ASan DLLs for clang-cl debug build..."
    $binDir = Join-Path $BuildPath "bin\Debug"
    if (-not (Test-Path $binDir)) {
        $binDir = Join-Path $BuildPath "bin"
    }

    if (Test-Path $binDir) {
        $ClangRootPaths = @()

        $ClangCmd = Get-Command clang-cl.exe -ErrorAction SilentlyContinue
        if (-not $ClangCmd) {
            $ClangCmd = Get-Command clang.exe -ErrorAction SilentlyContinue
        }

        if ($ClangCmd) {
            $ClangRootPaths += Split-Path (Split-Path $ClangCmd.Path)
        }

        $ClangRootPaths += "C:\Program Files\LLVM"
        $ClangRootPaths += "C:\Program Files (x86)\LLVM"

        $SanitizerDllsCopied = $false

        foreach ($ClangRoot in $ClangRootPaths) {
            if (-not (Test-Path $ClangRoot)) { continue }

            $SanitizerDlls = Get-ChildItem -Path "$ClangRoot\lib\clang\*\lib\windows\clang_rt.*san_dynamic-*.dll" -ErrorAction SilentlyContinue
            if (-not $SanitizerDlls) {
                $SanitizerDlls = Get-ChildItem -Path "$ClangRoot\lib\windows\clang_rt.*san_dynamic-*.dll" -ErrorAction SilentlyContinue
            }

            if ($SanitizerDlls) {
                foreach ($dll in $SanitizerDlls) {
                    Copy-Item -Path $dll.FullName -Destination $binDir -Force
                    Write-BuildLog -Context $Context -Message "DEBUG: Copied $($dll.Name) to $binDir"
                }
                $SanitizerDllsCopied = $true
                break
            }
        }
        
        if (-not $SanitizerDllsCopied) {
            Write-BuildLogWarning -Context $Context -Message "Could not find clang_rt ASan/UBSan dynamic DLLs to copy."
        }
    } else {
        Write-BuildLogWarning -Context $Context -Message "Bin directory $binDir not found. Cannot copy ASan DLLs."
    }
  }

  # Log sccache stats after build
  Write-BuildLog -Context $Context -Message "DEBUG: ========== SCCACHE DIAGNOSTICS (AFTER BUILD) =========="
  if ($sccachePath) {
    try {
      $sccacheStatsAfter = & sccache --show-stats 2>&1
      foreach ($line in $sccacheStatsAfter) {
        Write-BuildLog -Context $Context -Message "DEBUG:   $line"
      }
    } catch {
      Write-BuildLog -Context $Context -Message "DEBUG: Could not get sccache stats after build"
    }
  }
}

function Test-ClangClThreadSanitizerSupport {
  param(
    [Parameter(Mandatory)]
    [string]$ClangClPath
  )

  try {
    $versionOutput = & $ClangClPath '--version' 2>&1
    $versionText = ($versionOutput | Out-String)
    if ($versionText -match 'Target:\s*x86_64-.*windows-msvc') {
      return $false
    }
  } catch {
    return $false
  }

  return $true
}

Export-ModuleMember -Function Remove-BuildRootSafe, Invoke-CmakeConfigureAndBuild, Test-ClangClThreadSanitizerSupport
