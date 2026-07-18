# Build the project inside the ContainerHub Windows developer image using
# Stevedore's docker.exe (see ExternalLib/Kataglyphis-ContainerHub/docs/windows-builds.md
# for why nerdctl is not an option on Windows).
#
# Two transport modes, chosen automatically:
#   1. Bind mount (fast path): the repo is mounted read/write into the container
#      and build directories land directly in the working tree — same flow as CI
#      (.github/workflows/Windows.yml).
#   2. Tar pipe (fallback): when the bind mount cannot attach (e.g. the repo
#      lives on a Dev Drive whose filters are not allow-listed — error
#      "Der Dateisystem-Minifilter kann nicht an das Entwicklervolume angefügt
#      werden"), sources are streamed into a fresh container-local directory,
#      built there, and the build trees + logs are streamed back out.
#      To enable the fast path on a Dev Drive host instead, run once (elevated):
#        fsutil devdrv setfiltersallowed bindFlt, wcifs
#      then remount the volume (or reboot).
#
# The mount target is a FRESH path (C:\ws-mnt) on purpose: mounting over a
# directory baked into the image (C:\workspace) fails at CreateComputeSystem on
# hosts whose OS build differs from the image base build.

param(
  # Comma-separated Build-Windows.ps1 configurations to build.
  [string]$Configurations = 'clangcl-debug,clangcl-tsan,clangcl-profile,clangcl-release',
  [string]$Image = 'ghcr.io/kataglyphis/kataglyphis_beschleuniger:winamd64',
  # Explicit docker.exe path; falls back to $env:DOCKER_EXE, the Stevedore
  # install locations, then 'docker' on PATH.
  [string]$DockerExe,
  # Process isolation exposes all host CPUs; Hyper-V isolation defaults to 2.
  [ValidateSet('process', 'hyperv')]
  [string]$Isolation = 'process',
  # Only applied under Hyper-V isolation (process isolation shares the host).
  [int]$CpuCount = 0,
  [int]$MemoryGb = 16,
  [switch]$RunTests,
  [int]$ParallelJobs = 0,
  # Force the tar-pipe transport even if a bind mount would work.
  [switch]$NoBindMount,
  # Keep the fallback container around for debugging instead of removing it.
  [switch]$KeepContainer
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$mountTarget = 'C:\ws-mnt'

function Resolve-DockerExe {
  param([string]$Override)

  $candidates = @(
    $Override,
    $env:DOCKER_EXE,
    (Join-Path $env:ProgramFiles 'Stevedore\bin\docker.exe'),
    'D:\Stevedore\bin\docker.exe'
  ) | Where-Object { $_ }

  foreach ($candidate in $candidates) {
    if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
  }

  $onPath = Get-Command docker -ErrorAction SilentlyContinue
  if ($onPath) { return $onPath.Source }

  throw 'docker.exe not found. Install Stevedore (winget install stevedore) or pass -DockerExe.'
}

# Preflight: Build-Windows.ps1 resolves modules from ContainerHub first, then
# the vendored fallback (Scripts/Windows/modules). Fail fast if a module that
# only exists vendored (deleted upstream in ContainerHub b391a1d) is missing.
. (Join-Path $PSScriptRoot 'Resolve-BuildModule.ps1')
$null = Resolve-BuildModulePath -Name 'WindowsLogging.Common'

$docker = Resolve-DockerExe -Override $DockerExe
Write-Host "Using docker: $docker"
Write-Host "Image: $Image"
Write-Host "Configurations: $Configurations"

$isolationArgs = @('--isolation', $Isolation)
if ($Isolation -eq 'hyperv') {
  $cpus = if ($CpuCount -gt 0) { $CpuCount } else { [Environment]::ProcessorCount }
  $isolationArgs += @('--cpu-count', "$cpus", '--memory', "${MemoryGb}g")
}

# Arguments handed to the image entrypoint (VsDevCmd + ASAN runtime PATH, then %*).
# Every token must be free of spaces: it travels docker CLI -> cmd /S /C -> %*.
function Get-BuildCommandArgs {
  param([Parameter(Mandatory)][string]$WorkspacePath)

  $psArgs = @(
    'powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', (Join-Path $WorkspacePath 'Scripts\Windows\Build-Windows.ps1'),
    '-Configurations', $Configurations,
    '-SkipFormat', '-SkipTidy', '-SkipPerfTests', '-SkipMsix'
  )
  if (-not $RunTests) { $psArgs += '-SkipTests' }
  if ($ParallelJobs -gt 0) { $psArgs += @('-ParallelJobs', "$ParallelJobs") }
  return $psArgs
}

function Test-BindMountUsable {
  if ($NoBindMount) { return $false }
  Write-Host 'Probing bind mount support...'
  # The probe is EXPECTED to fail on Dev Drive hosts; docker's stderr must not
  # become a terminating NativeCommandError (Windows PowerShell turns redirected
  # native stderr into ErrorRecords, and $ErrorActionPreference is 'Stop').
  $previousPreference = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    & $docker run --rm @isolationArgs `
      --mount "type=bind,source=$repoRoot,target=$mountTarget" `
      --entrypoint cmd $Image /c "dir $mountTarget\CMakePresets.json > nul" 2>&1 | Out-Null
  } finally {
    $ErrorActionPreference = $previousPreference
  }
  return ($LASTEXITCODE -eq 0)
}

function Invoke-BindMountBuild {
  Write-Host 'Bind mount usable - building directly in the working tree.'
  $buildArgs = Get-BuildCommandArgs -WorkspacePath $mountTarget
  & $docker run --rm @isolationArgs `
    --mount "type=bind,source=$repoRoot,target=$mountTarget" `
    -w $mountTarget $Image @buildArgs
  if ($LASTEXITCODE -ne 0) { throw "Container build failed (exit $LASTEXITCODE)." }
}

function Invoke-TarPipeBuild {
  Write-Host 'Bind mount unavailable - falling back to tar-pipe transport.'
  $container = "bb-build-$([Guid]::NewGuid().ToString('N').Substring(0, 8))"
  $ws = 'C:\ws'

  & $docker run -d --name $container @isolationArgs --entrypoint cmd $Image `
    /c 'ping -n 604800 127.0.0.1 > nul' | Out-Null
  if ($LASTEXITCODE -ne 0) { throw 'Failed to start build container.' }

  try {
    & $docker exec $container cmd /c "mkdir $ws" | Out-Null

    Write-Host 'Streaming sources into the container (excluding .git and build trees)...'
    # cmd /c keeps the pipe a raw byte stream regardless of PowerShell version.
    # Anchor the build-tree excludes to the repo root (./...): unanchored
    # patterns match at every path depth in bsdtar and would strip nested
    # files like ExternalLib/Kataglyphis-ContainerHub/windows/build.ps1.
    $tarIn = "tar -cf - --exclude .git --exclude ./logs --exclude `"./build`" --exclude `"./build-*`" --exclude `"./build_*`" -C `"$repoRoot`" . | `"$docker`" exec -i $container tar -xf - -C $ws"
    cmd /c $tarIn
    if ($LASTEXITCODE -ne 0) { throw "Source transfer failed (exit $LASTEXITCODE)." }

    $buildArgs = Get-BuildCommandArgs -WorkspacePath $ws
    # docker exec bypasses the image entrypoint, so invoke it explicitly to get
    # the VS developer environment and the clang-cl ASAN runtime on PATH.
    & $docker exec -w $ws $container cmd /S /C C:\temp\scripts\entrypoint.cmd @buildArgs
    $buildExit = $LASTEXITCODE

    Write-Host 'Streaming build trees and logs back to the working tree...'
    $configModel = Import-PowerShellDataFile (Join-Path $PSScriptRoot 'Build-Windows.config.psd1')
    $outDirs = @('logs')
    foreach ($name in ($Configurations -split ',')) {
      $spec = $configModel.Build.Configurations[$name.Trim()]
      if ($spec) { $outDirs += $spec.BuildDir }
    }
    $existing = @()
    foreach ($dir in $outDirs) {
      & $docker exec $container cmd /c "if exist $ws\$dir (exit 0) else (exit 1)"
      if ($LASTEXITCODE -eq 0) { $existing += $dir }
    }
    if ($existing.Count -gt 0) {
      $tarOut = "`"$docker`" exec $container tar -cf - -C $ws $($existing -join ' ') | tar -xf - -C `"$repoRoot`""
      cmd /c $tarOut
      if ($LASTEXITCODE -ne 0) { Write-Warning "Artifact extraction failed (exit $LASTEXITCODE)." }
    }

    if ($buildExit -ne 0) { throw "Container build failed (exit $buildExit)." }
  } finally {
    if ($KeepContainer) {
      Write-Host "Keeping container '$container' for debugging (remove with: docker rm -f $container)."
    } else {
      # On hosts with the wcifs layer-teardown quirk the immediate remove can
      # fail even though a later manual 'docker rm' succeeds — surface it, but
      # never let docker's stderr become a terminating error and flip a green
      # build to exit 1 (same NativeCommandError trap as in the probe above).
      $previousPreference = $ErrorActionPreference
      $ErrorActionPreference = 'Continue'
      try {
        & $docker rm -f $container 2>&1 | Out-Null
        & $docker inspect $container 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
          Write-Warning "Container '$container' could not be removed yet (wcifs teardown lock?). Remove it later with: docker rm -f $container"
        }
      } finally {
        $ErrorActionPreference = $previousPreference
      }
    }
  }
}

if (Test-BindMountUsable) {
  Invoke-BindMountBuild
} else {
  Invoke-TarPipeBuild
}

Write-Host 'Container build finished successfully.'
