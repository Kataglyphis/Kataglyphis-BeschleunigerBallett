Set-StrictMode -Version Latest

function Get-OrDefault([string]$Value, [string]$DefaultValue) {
  if ([string]::IsNullOrWhiteSpace($Value)) { return $DefaultValue }
  return $Value
}

function Get-ConfigValue {
  param(
    [Parameter(Mandatory)]
    $Config,
    [Parameter(Mandatory)]
    [string]$Path
  )

  $cursor = $Config
  foreach ($segment in ($Path -split '\.')) {
    if ($null -eq $cursor) { return $null }
    try {
      $cursor = $cursor[$segment]
    } catch {
      return $null
    }
  }

  return $cursor
}

function Get-SelectedConfigurations {
  param(
    [string[]]$Configurations,
    [string[]]$AvailableConfigurations
  )

  $selectedConfigurations = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
  if ($null -eq $Configurations -or $Configurations.Count -eq 0 -or ($Configurations -contains 'all')) {
    foreach ($configurationName in $AvailableConfigurations) {
      $selectedConfigurations.Add($configurationName) | Out-Null
    }
  } else {
    foreach ($item in $Configurations) {
      if ([string]::IsNullOrWhiteSpace($item)) { continue }
      foreach ($configurationName in $item -split ',') {
        if ([string]::IsNullOrWhiteSpace($configurationName)) { continue }
        $normalized = $configurationName.Trim().ToLowerInvariant()
        if (-not ($AvailableConfigurations -contains $normalized)) {
          throw "Unknown configuration '$configurationName'. Supported values: all, $($AvailableConfigurations -join ', ')"
        }
        $selectedConfigurations.Add($normalized) | Out-Null
      }
    }
  }

  return $selectedConfigurations
}

function Test-ConfigurationSelected {
  param([Parameter(Mandatory)][string]$Name, [Parameter(Mandatory)][System.Collections.Generic.HashSet[string]]$SelectedConfigurations)
  return $SelectedConfigurations.Contains($Name)
}

Export-ModuleMember -Function @(
  'Get-OrDefault',
  'Get-ConfigValue',
  'Get-SelectedConfigurations',
  'Test-ConfigurationSelected'
)
