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
  [switch]$KeepContainer,
  # Discard the reusable build container and start from a clean one. Use when a
  # build behaves strangely, or after deleting files that the container may
  # still hold (sources are overwritten in place, never pruned).
  [switch]$FreshContainer
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

# Persistent compiler cache: sccache stores objects in the container FS by
# default, which is discarded with the container - so every build was a cold
# full rebuild. A named volume survives containers and makes repeat builds
# mostly cache hits.
$sccacheVolume = 'kataglyphis-sccache'
$sccacheDir = 'C:\sccache'
$cacheArgs = @(
  '-v', "${sccacheVolume}:${sccacheDir}",
  '-e', "SCCACHE_DIR=${sccacheDir}",
  '-e', 'SCCACHE_CACHE_SIZE=20G',
  # Build trees are streamed in for incremental builds - do not wipe them.
  '-e', 'KATAGLYPHIS_KEEP_BUILD_ROOT=1'
)

# NOTE: mounting the build directory as a named volume was TRIED and does not
# work here - CMake's compiler test fails inside a mounted volume with
# "ninja: error: loading 'build.ninja': The system cannot find the file
# specified", both with a fresh volume and a populated one. See
# docs/container-build-caching.md for the full measurement.

# Reusable build-container helpers live upstream in ContainerHub (they apply to
# any project built in that image, not just this engine):
#   windows/scripts/modules/WindowsContainerBuild.Reuse.psm1
# Rationale and measurements: ContainerHub docs/windows-container-build-performance.md
Import-Module (Resolve-BuildModulePath -Name 'WindowsContainerBuild.Reuse') -Force -Global
$persistentContainerName = 'bb-build-persistent'

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
  & $docker run --rm @isolationArgs @cacheArgs `
    --mount "type=bind,source=$repoRoot,target=$mountTarget" `
    -w $mountTarget $Image @buildArgs
  if ($LASTEXITCODE -ne 0) { throw "Container build failed (exit $LASTEXITCODE)." }
}

function Invoke-TarPipeBuild {
  Write-Host 'Bind mount unavailable - falling back to tar-pipe transport.'
  $container = $persistentContainerName
  $ws = 'C:\ws'

  $reusedContainer = Get-ReusableBuildContainer -DockerExe $docker -Name $container -Image $Image `
    -RunArgs ($isolationArgs + $cacheArgs) -Fresh:$FreshContainer

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

    # Incremental builds: the host already holds the previous build tree (it is
    # streamed back out after every build), so stream it back IN. ninja then
    # rebuilds only what changed instead of ~690 objects from scratch. This
    # avoids mounting the build dir as a volume, which CMake cannot configure
    # inside (see docs/container-build-caching.md).
    foreach ($configuration in ($Configurations -split ',')) {
      if ($reusedContainer) { break }# tree already lives in the container
      $trimmedConfiguration = $configuration.Trim()
      if (-not $trimmedConfiguration) { continue }
      $buildDirName = "build-$trimmedConfiguration"
      $hostBuildDir = Join-Path $repoRoot $buildDirName
      if (Test-Path $hostBuildDir) {
        Write-Host "Streaming existing $buildDirName into the container (incremental build)..."
        # Exclude the cargo tree: its cxxbridge output nests deep enough to blow
        # past the Windows path limit inside the container ("Can't create ...
        # Invalid argument"), which failed the whole transfer. Cargo rebuilds
        # its own artifacts cheaply, so skipping them costs little.
        $tarBuildIn = "tar -cf - --exclude `"$buildDirName/cargo`" -C `"$repoRoot`" `"$buildDirName`" | `"$docker`" exec -i $container tar -xf - -C $ws"
        cmd /c $tarBuildIn
        if ($LASTEXITCODE -ne 0) {
          Write-Host "  transfer reported errors - ninja will rebuild whatever did not arrive"
        }
      }
    }

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
      # Exclude the cargo tree on the way out for the same reason it is excluded
      # on the way in: its cxxbridge paths exceed the Windows path limit and
      # abort the extraction ("Artifact extraction failed"). Excluding it keeps
      # the executables and ninja state - what the host actually needs - intact.
      # The build tree now stays in the reusable container, so the host only
      # needs what it actually runs: executables, their debug info, the compile
      # database (clang-tidy) and logs. Copying the whole ~8.5 GB tree back was
      # pure overhead, and its deep cargo/cxxbridge paths are what produced the
      # "Artifact extraction failed" warnings.
      $includeArgs = ($existing | ForEach-Object {
          if ($_ -eq 'logs') { $_ } else { "$_/*.exe $_/*.dll $_/*.pdb $_/compile_commands.json" }
        }) -join ' '
      $tarOut = "`"$docker`" exec $container cmd /c `"cd /d $ws && tar -cf - $includeArgs`" | tar -xf - -C `"$repoRoot`""
      cmd /c $tarOut
      if ($LASTEXITCODE -ne 0) {
        Write-Warning "Artifact extraction reported errors (exit $LASTEXITCODE) - check executable timestamps."
      }
    }

    if ($buildExit -ne 0) { throw "Container build failed (exit $buildExit)." }
  } finally {
    if ($true) {
      # The container is intentionally reused across builds - that is what makes
      # builds incremental without moving the tree. Remove it with
      # 'docker rm -f bb-build-persistent' or rerun with -FreshContainer.
      Write-Host "Keeping build container '$container' for the next build (reset: -FreshContainer)."
    } elseif ($KeepContainer) {
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
