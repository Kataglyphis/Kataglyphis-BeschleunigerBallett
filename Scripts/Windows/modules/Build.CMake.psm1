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
    [string]$CleanLabel = ''
  )

  if ($CleanBuildRoot) {
    $label = if ([string]::IsNullOrWhiteSpace($CleanLabel)) { $Preset } else { $CleanLabel }
    Remove-BuildRootSafe -Context $Context -Path $BuildPath -Label $label
  }

  $configureArgs = @('-B', $BuildPath, '--preset', $Preset)
  if ($ConfigureExtraArgs.Count -gt 0) {
    $configureArgs += $ConfigureExtraArgs
  }

  Invoke-BuildExternal -Context $Context -File 'cmake' -Parameters $configureArgs | Out-Null
  Invoke-BuildExternal -Context $Context -File 'cmake' -Parameters @('--build', $BuildPath, '--config', $Configuration) | Out-Null
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
