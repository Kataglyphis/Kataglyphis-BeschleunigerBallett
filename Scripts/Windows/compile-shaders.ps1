param(
  [string]$TargetEnv = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..') | Select-Object -ExpandProperty Path
$shadersRoot = Join-Path $scriptRoot 'Resources\Shaders'

if ([string]::IsNullOrWhiteSpace($TargetEnv)) {
  if ($env:VULKAN_VERSION) {
    if ($env:VULKAN_VERSION -match '^([0-9]+)\.([0-9]+)') { $TargetEnv = "vulkan$($matches[1]).$($matches[2])" }
  }
  if ([string]::IsNullOrWhiteSpace($TargetEnv)) { $TargetEnv = 'vulkan1.4' }
}

if (-not (Test-Path $shadersRoot)) {
  Write-Host "[WARN] Shader directory not found: $shadersRoot - skipping shader compilation"
  exit 0
}

function Resolve-Glslc {
  $glslc = Get-Command 'glslc.exe' -ErrorAction SilentlyContinue
  if ($glslc) { return $glslc.Source }

  if ($env:VULKAN_SDK) {
    $candidate = Join-Path $env:VULKAN_SDK 'Bin\glslc.exe'
    if (Test-Path $candidate) { return $candidate }
  }

  return $null
}

$glslcPath = Resolve-Glslc
if (-not $glslcPath) {
  Write-Error 'glslc.exe not found in PATH or VULKAN_SDK. Install Vulkan SDK or add glslc to PATH.'
  exit 2
}

Write-Host "[INFO] Using glslc: $glslcPath"
Write-Host "[INFO] Precompiling shaders under $shadersRoot -> target-env=$TargetEnv"

# collect include dirs
$includeDirs = Get-ChildItem -Path $shadersRoot -Directory -Recurse -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
$includeArgs = @()
foreach ($d in $includeDirs) { $includeArgs += '-I'; $includeArgs += $d }

$patterns = @('*.vert','*.frag','*.comp','*.rgen','*.rchit','*.rmiss','*.geom','*.tesc','*.tese','*.glsl')
$files = Get-ChildItem -Path $shadersRoot -Recurse -File -Include $patterns -ErrorAction SilentlyContinue

foreach ($file in $files) {
  $outDir = Join-Path $file.Directory.FullName 'spv'
  if (-not (Test-Path $outDir)) { New-Item -Path $outDir -ItemType Directory | Out-Null }
  $outFile = Join-Path $outDir ($file.Name + '.spv')

  if (Test-Path $outFile) {
    Write-Host "[INFO] Skipping existing: $outFile"
    continue
  }

  Write-Host "[INFO] Compiling $($file.FullName) -> $outFile"
  & $glslcPath "--target-env=$TargetEnv" $file.FullName '-o' $outFile $includeArgs
  if ($LASTEXITCODE -ne 0) {
    Write-Warning "glslc failed for $($file.FullName)"
  }
}

Write-Host "[INFO] Shader precompilation finished"
