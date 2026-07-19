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
$includeArgs = @(
  '-I', (Join-Path $scriptRoot 'Src\GraphicsEngineVulkan'),
  '-I', (Join-Path $scriptRoot 'Src\GraphicsEngineVulkan\renderer'),
  '-I', (Join-Path $scriptRoot 'Src\shared')
)
$includeDirs = Get-ChildItem -Path $shadersRoot -Directory -Recurse -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
foreach ($d in $includeDirs) { $includeArgs += '-I'; $includeArgs += $d }

 # Only compile shader entry points. Many .glsl files are shared headers/includes
 # and should not be compiled directly to SPIR-V. Remove '*.glsl' from the
 # patterns so we only compile explicit shader stage files.
 $patterns = @('*.vert','*.frag','*.comp','*.rgen','*.rchit','*.rmiss','*.geom','*.tesc','*.tese')
$files = Get-ChildItem -Path $shadersRoot -Recurse -File -Include $patterns -ErrorAction SilentlyContinue

# Shared headers/includes: any of these being newer than a .spv makes it stale.
# Conservative (rebuilds more than strictly needed) but cheap and never wrong.
$includeFiles = @(Get-ChildItem -Path $shadersRoot -Recurse -File -Include '*.glsl', '*.hpp', '*.h' -ErrorAction SilentlyContinue)

foreach ($file in $files) {
  $outDir = Join-Path $file.Directory.FullName 'spv'
  if (-not (Test-Path $outDir)) { New-Item -Path $outDir -ItemType Directory | Out-Null }
  $outFile = Join-Path $outDir ($file.Name + '.spv')

  # Recompile when the .spv is MISSING or OLDER than any input. An
  # existence-only check (what this used to do) meant every shader edit after
  # the first build was silently ignored - the GPU kept running stale SPIR-V
  # and shader changes appeared to have no effect. Includes are considered
  # too, so editing a shared .glsl rebuilds its dependents.
  $needsCompile = $true
  if (Test-Path $outFile) {
    $outStamp = (Get-Item $outFile).LastWriteTimeUtc
    $newestInput = (Get-Item $file.FullName).LastWriteTimeUtc
    foreach ($inc in $includeFiles) {
      if ($inc.LastWriteTimeUtc -gt $newestInput) { $newestInput = $inc.LastWriteTimeUtc }
    }
    if ($outStamp -ge $newestInput) {
      $needsCompile = $false
      Write-Host "[INFO] Up to date: $outFile"
    } else {
      Write-Host "[INFO] Stale, recompiling: $outFile"
    }
  }
  if (-not $needsCompile) { continue }

  Write-Host "[INFO] Compiling $($file.FullName) -> $outFile"

  # Build glslc arguments. Define VULKAN for shader branches that need it when
  # compiling for a Vulkan target. Include search paths afterwards.
  $args = @("--target-env=$TargetEnv")
  # Use a project-specific define to avoid colliding with built-in or external
  # definitions named 'VULKAN'. Some toolchains predefine 'VULKAN' which can
  # cause macro redefinition errors. We define `KAT_VULKAN` instead.
  if ($TargetEnv -match '^vulkan') { $args += '-DKAT_VULKAN' }
  $args += $file.FullName
  $args += '-o'
  $args += $outFile
  $args += $includeArgs

  & $glslcPath $args
  if ($LASTEXITCODE -ne 0) {
    Write-Warning "glslc failed for $($file.FullName)"
  }
}

Write-Host "[INFO] Shader precompilation finished"
