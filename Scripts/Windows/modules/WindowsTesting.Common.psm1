Set-StrictMode -Version Latest
#requires -Version 7.0


$script:MsvcAsanRuntimeDir = $null

function Add-AsanRuntimeDirIfPresent {
  param(
    [Parameter(Mandatory)]
    [object]$RuntimeDirs,
    [string]$CandidateDir
  )

  if ([string]::IsNullOrWhiteSpace($CandidateDir)) {
    return
  }

  $asanRuntime = Join-Path $CandidateDir 'clang_rt.asan_dynamic-x86_64.dll'
  if ((Test-Path $CandidateDir) -and (Test-Path $asanRuntime) -and -not $RuntimeDirs.Contains($CandidateDir)) {
    $RuntimeDirs.Add($CandidateDir)
  }
}

function Get-VisualStudioAsanRuntimeDirs {
  $runtimeDirs = [System.Collections.Generic.List[string]]::new()
  $visualStudioRoots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
    ForEach-Object { Join-Path $_ 'Microsoft Visual Studio' } |
    Where-Object { Test-Path $_ }

  foreach ($root in $visualStudioRoots) {
    try {
      Get-ChildItem -Path $root -Directory -ErrorAction Stop | ForEach-Object {
        Get-ChildItem -Path $_.FullName -Directory -ErrorAction SilentlyContinue | ForEach-Object {
          $msvcToolsRoot = Join-Path $_.FullName 'VC\Tools\MSVC'
          if (-not (Test-Path $msvcToolsRoot)) {
            return
          }

          Get-ChildItem -Path $msvcToolsRoot -Directory -ErrorAction SilentlyContinue | ForEach-Object {
            Add-AsanRuntimeDirIfPresent -RuntimeDirs $runtimeDirs -CandidateDir (Join-Path $_.FullName 'bin\Hostx64\x64')
          }
        }
      }
    } catch {
    }
  }

  return @($runtimeDirs)
}

function Get-LlvmAsanRuntimeDirs {
  $runtimeDirs = [System.Collections.Generic.List[string]]::new()
  $clangCommand = Get-Command 'clang-cl.exe' -ErrorAction SilentlyContinue

  if ($clangCommand) {
    $clangBinDir = Split-Path $clangCommand.Source -Parent
    $llvmRoot = Split-Path $clangBinDir -Parent
    $clangLibRoot = Join-Path $llvmRoot 'lib\clang'
    if (Test-Path $clangLibRoot) {
      Get-ChildItem -Path $clangLibRoot -Directory -ErrorAction SilentlyContinue | ForEach-Object {
        Add-AsanRuntimeDirIfPresent -RuntimeDirs $runtimeDirs -CandidateDir (Join-Path $_.FullName 'lib\windows')
      }
    }
  }

  try {
    $clangResourceDir = & 'clang-cl.exe' --print-resource-dir 2>$null
    if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($clangResourceDir)) {
      Add-AsanRuntimeDirIfPresent -RuntimeDirs $runtimeDirs -CandidateDir (Join-Path $clangResourceDir.Trim() 'lib\windows')
    }
  } catch {
  }

  return @($runtimeDirs)
}

function Resolve-TestExecutable {
  param(
    [Parameter(Mandatory)]
    [string]$BuildRoot,
    [Parameter(Mandatory)]
    [string]$ExecutableName
  )

  $preferredPaths = @(
    (Join-Path $BuildRoot $ExecutableName),
    (Join-Path $BuildRoot (Join-Path 'Debug' $ExecutableName)),
    (Join-Path $BuildRoot (Join-Path 'Release' $ExecutableName)),
    (Join-Path $BuildRoot (Join-Path 'RelWithDebInfo' $ExecutableName)),
    (Join-Path $BuildRoot (Join-Path 'Test\commit' $ExecutableName)),
    (Join-Path $BuildRoot (Join-Path 'Test\compile' $ExecutableName)),
    (Join-Path $BuildRoot (Join-Path 'Test\perf' $ExecutableName))
  )

  foreach ($candidate in $preferredPaths) {
    if (Test-Path $candidate) {
      return $candidate
    }
  }

  $found = Get-ChildItem -Path $BuildRoot -Filter $ExecutableName -File -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if ($found) {
    return $found.FullName
  }

  return $null
}

function Get-AsanRuntimeDirs {
  param(
    [Parameter(Mandatory)]
    [string]$BuildRoot,
    [ValidateSet('Auto', 'Msvc', 'Clang')]
    [string]$RuntimeFlavor = 'Auto'
  )

  $asanRuntimeDirs = [System.Collections.Generic.List[string]]::new()

  if ($RuntimeFlavor -eq 'Auto' -or $RuntimeFlavor -eq 'Msvc') {
    if ($script:MsvcAsanRuntimeDir) {
      Add-AsanRuntimeDirIfPresent -RuntimeDirs $asanRuntimeDirs -CandidateDir $script:MsvcAsanRuntimeDir
    } elseif ($env:VCToolsInstallDir) {
      $fromEnv = Join-Path $env:VCToolsInstallDir 'bin\Hostx64\x64'
      Add-AsanRuntimeDirIfPresent -RuntimeDirs $asanRuntimeDirs -CandidateDir $fromEnv
      if ($asanRuntimeDirs.Count -gt 0) {
        $script:MsvcAsanRuntimeDir = $fromEnv
      }
    }

    if ($asanRuntimeDirs.Count -eq 0) {
      foreach ($runtimeDir in Get-VisualStudioAsanRuntimeDirs) {
        Add-AsanRuntimeDirIfPresent -RuntimeDirs $asanRuntimeDirs -CandidateDir $runtimeDir
        if (-not $script:MsvcAsanRuntimeDir) {
          $script:MsvcAsanRuntimeDir = $runtimeDir
        }
      }
    }
  }

  if ($RuntimeFlavor -eq 'Auto' -or $RuntimeFlavor -eq 'Clang') {
    foreach ($runtimeDir in Get-LlvmAsanRuntimeDirs) {
      Add-AsanRuntimeDirIfPresent -RuntimeDirs $asanRuntimeDirs -CandidateDir $runtimeDir
    }
  }

  return @($asanRuntimeDirs)
}

function Invoke-WithRuntimePath {
  param(
    [string[]]$RuntimeDirs = @(),
    [Parameter(Mandatory)]
    [scriptblock]$Script
  )

  # Normalize to a clean string array, even when the caller provides $null or a scalar value.
  $normalizedRuntimeDirs = @($RuntimeDirs | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
  $oldPath = $env:PATH
  if ($normalizedRuntimeDirs.Length -gt 0) {
    $env:PATH = (($normalizedRuntimeDirs -join ';') + ';' + $oldPath)
  }

  $oldAsanOptions = $env:ASAN_OPTIONS
  $env:ASAN_OPTIONS = "log_path=logs/asan.log:report_globals=1:$oldAsanOptions"

  try {
    & $Script
  } finally {
    if ($normalizedRuntimeDirs.Length -gt 0) {
      $env:PATH = $oldPath
    }
    if ($null -ne $oldAsanOptions) {
      $env:ASAN_OPTIONS = $oldAsanOptions
    } else {
      Remove-Item Env:\ASAN_OPTIONS -ErrorAction SilentlyContinue
    }
  }
}

function Invoke-ManualTestExecutable {
  param(
    [Parameter(Mandatory)]
    [pscustomobject]$Context,
    [Parameter(Mandatory)]
    [string]$BuildRoot,
    [Parameter(Mandatory)]
    [string]$ExecutableName,
    [string[]]$Arguments = @(),
    [ValidateSet('Auto', 'Msvc', 'Clang')]
    [string]$RuntimeFlavor = 'Auto'
  )

  $testExecutable = Resolve-TestExecutable -BuildRoot $BuildRoot -ExecutableName $ExecutableName
  if (-not $testExecutable) {
    Write-BuildLogWarning -Context $Context -Message "Test executable '$ExecutableName' not found under '$BuildRoot'."
    return $false
  }

  $asanRuntimeDirs = Get-AsanRuntimeDirs -BuildRoot $BuildRoot -RuntimeFlavor $RuntimeFlavor

  Invoke-WithRuntimePath -RuntimeDirs $asanRuntimeDirs -Script {
    try {
      Invoke-BuildExternal -Context $Context -File $testExecutable -Parameters $Arguments | Out-Null
    } catch {
      $errorText = $_.Exception.Message
      if ($errorText -match 'exit code -1073741511|exit code -1073741515') {
        Write-BuildLogWarning -Context $Context -Message "Manual test execution failed to start '$ExecutableName' (Windows loader/runtime mismatch). Continuing pipeline."
        return $false
      }
      throw
    }
  }

  return $true
}

function Invoke-CtestDiscoveredTests {
  param(
    [Parameter(Mandatory)]
    [pscustomobject]$Context,
    [Parameter(Mandatory)]
    [string]$BuildRoot,
    [Parameter(Mandatory)]
    [string]$Configuration,
    [string[]]$ExcludeRegex = @(),
    [ValidateSet('Auto', 'Msvc', 'Clang')]
    [string]$RuntimeFlavor = 'Auto'
  )

  $ctestCommand = Get-Command 'ctest' -ErrorAction SilentlyContinue
  if (-not $ctestCommand) {
    throw 'ctest not found on PATH.'
  }

  $asanRuntimeDirs = Get-AsanRuntimeDirs -BuildRoot $BuildRoot -RuntimeFlavor $RuntimeFlavor

  $ctestParameters = @(
    '--test-dir', $BuildRoot,
    '--build-config', $Configuration,
    '--output-on-failure',
    '--timeout', '300'
  )

  foreach ($regex in @($ExcludeRegex | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })) {
    $ctestParameters += @('--exclude-regex', $regex)
  }

  Invoke-WithRuntimePath -RuntimeDirs $asanRuntimeDirs -Script {
    Invoke-BuildExternal -Context $Context -File $ctestCommand.Source -Parameters $ctestParameters | Out-Null
  }
}

Export-ModuleMember -Function Resolve-TestExecutable, Invoke-ManualTestExecutable, Invoke-CtestDiscoveredTests

