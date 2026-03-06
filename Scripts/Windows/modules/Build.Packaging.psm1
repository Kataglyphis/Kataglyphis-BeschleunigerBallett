Set-StrictMode -Version Latest

function ConvertTo-XmlEscapedText {
  param([AllowNull()][string]$Value)

  if ($null -eq $Value) {
    return ''
  }

  return [System.Security.SecurityElement]::Escape($Value)
}

function Resolve-WindowsSdkToolPath {
  param(
    [Parameter(Mandatory)]
    [string]$ToolName,
    [AllowNull()]
    [string]$OverridePath
  )

  if (-not [string]::IsNullOrWhiteSpace($OverridePath)) {
    if (Test-Path $OverridePath) {
      return (Resolve-Path $OverridePath).Path
    }

    throw "Configured SDK tool path does not exist for '$ToolName': $OverridePath"
  }

  $onPath = Get-Command $ToolName -ErrorAction SilentlyContinue
  if ($onPath) {
    return $onPath.Source
  }

  $candidateDirs = @()

  foreach ($envVar in @('WindowsSdkVerBinPath', 'WindowsSdkBinPath')) {
    $entry = Get-Item -Path "Env:$envVar" -ErrorAction SilentlyContinue
    if ($entry -and -not [string]::IsNullOrWhiteSpace($entry.Value)) {
      $candidateDirs += $entry.Value
      $candidateDirs += (Join-Path $entry.Value 'x64')
    }
  }

  $kitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
  if (Test-Path $kitsRoot) {
    $sdkVersion = $null
    $versionEntry = Get-Item -Path 'Env:WindowsSDKVersion' -ErrorAction SilentlyContinue
    if ($versionEntry -and -not [string]::IsNullOrWhiteSpace($versionEntry.Value)) {
      $sdkVersion = $versionEntry.Value.TrimEnd('\\')
    }

    if (-not [string]::IsNullOrWhiteSpace($sdkVersion)) {
      $candidateDirs += (Join-Path $kitsRoot $sdkVersion)
      $candidateDirs += (Join-Path (Join-Path $kitsRoot $sdkVersion) 'x64')
    }

    $versionDirs = Get-ChildItem -Path $kitsRoot -Directory -ErrorAction SilentlyContinue |
      Sort-Object Name -Descending
    foreach ($versionDir in $versionDirs) {
      $candidateDirs += $versionDir.FullName
      $candidateDirs += (Join-Path $versionDir.FullName 'x64')
    }
  }

  foreach ($dir in ($candidateDirs | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)) {
    $candidate = Join-Path $dir $ToolName
    if (Test-Path $candidate) {
      return $candidate
    }
  }

  return $null
}

function Expand-XmlTemplateTokens {
  param(
    [Parameter(Mandatory)]
    [string]$Template,
    [Parameter(Mandatory)]
    [hashtable]$TokenMap
  )

  $expanded = $Template
  foreach ($token in $TokenMap.Keys) {
    $expanded = $expanded -replace $token, (ConvertTo-XmlEscapedText ([string]$TokenMap[$token]))
  }

  return $expanded
}

function New-TransparentPng {
  param(
    [Parameter(Mandatory)]
    [string]$Path,
    [Parameter(Mandatory)]
    [int]$Width,
    [Parameter(Mandatory)]
    [int]$Height
  )

  Add-Type -AssemblyName System.Drawing
  $bmp = New-Object System.Drawing.Bitmap($Width, $Height)
  $gfx = [System.Drawing.Graphics]::FromImage($bmp)
  try {
    $gfx.Clear([System.Drawing.Color]::Transparent)
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
  } finally {
    $gfx.Dispose()
    $bmp.Dispose()
  }
}

Export-ModuleMember -Function Resolve-WindowsSdkToolPath, Expand-XmlTemplateTokens, New-TransparentPng
