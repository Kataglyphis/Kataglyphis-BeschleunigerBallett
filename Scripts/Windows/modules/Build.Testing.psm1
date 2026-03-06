Set-StrictMode -Version Latest

$script:MsvcAsanRuntimeDir = $null

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

  $asanRuntimeDirs = @()

  if ($RuntimeFlavor -eq 'Auto' -or $RuntimeFlavor -eq 'Msvc') {
    if ($script:MsvcAsanRuntimeDir) {
      $asanRuntimeDirs += $script:MsvcAsanRuntimeDir
    } elseif ($env:VCToolsInstallDir) {
      $fromEnv = Join-Path $env:VCToolsInstallDir 'bin\Hostx64\x64'
      if (Test-Path (Join-Path $fromEnv 'clang_rt.asan_dynamic-x86_64.dll')) {
        $script:MsvcAsanRuntimeDir = $fromEnv
        $asanRuntimeDirs += $fromEnv
      }
    }
  }

  if ($RuntimeFlavor -eq 'Auto' -or $RuntimeFlavor -eq 'Clang') {
    try {
      $clangResourceDir = & 'clang-cl.exe' --print-resource-dir 2>$null
      if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($clangResourceDir)) {
        $clangRuntimeDir = Join-Path $clangResourceDir.Trim() 'lib\windows'
        if (Test-Path (Join-Path $clangRuntimeDir 'clang_rt.asan_dynamic-x86_64.dll')) {
          $asanRuntimeDirs += $clangRuntimeDir
        }
      }
    } catch {
    }
  }

  return @($asanRuntimeDirs | Select-Object -Unique)
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

  try {
    & $Script
  } finally {
    if ($normalizedRuntimeDirs.Length -gt 0) {
      $env:PATH = $oldPath
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
