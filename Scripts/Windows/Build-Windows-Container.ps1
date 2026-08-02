#requires -Version 7.0
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
# The mount target is C:\ws - the SAME path the tar-pipe transport uses, and
# that matters: CMake bakes absolute paths into CMakeCache.txt and refuses to
# reuse a cache generated elsewhere ("The source C:/ws-mnt/CMakeLists.txt does
# not match the source C:/ws/CMakeLists.txt used to generate cache"). Sharing
# one path means a tree built under either transport stays usable by the other,
# so switching between them does not force a cold rebuild.
#
# It must still be a path that is NOT baked into the image: mounting over a
# directory that exists in the image (C:\workspace) fails at CreateComputeSystem
# when the host OS build differs from the image base build. C:\ws is absent from
# the image (verified) and is created by the mount.

param(

  # Comma-separated Build-Windows.ps1 configurations to build.
  [string]$Configurations = 'clangcl-debug,clangcl-profile,clangcl-release',
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
  # Opt into the bind-mount transport. Off by default because it is MEASURED
  # SLOWER on this Dev Drive host - see Test-BindMountUsable below.
  [switch]$UseBindMount,
  # Discard the reusable build container and start from a clean one. Use when a
  # build behaves strangely, or after deleting files that the container may
  # still hold (sources are overwritten in place, never pruned).
  [switch]$FreshContainer
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$mountTarget = 'C:\ws'

# Preflight: Build-Windows.ps1 resolves modules from ContainerHub first, then
# the vendored fallback (Scripts/Windows/modules). Fail fast if a module that
# only exists vendored (deleted upstream in ContainerHub b391a1d) is missing.
. (Join-Path $PSScriptRoot 'Resolve-BuildModule.ps1')
$null = Resolve-BuildModulePath -Name 'WindowsBuild.Common'

# Reusable build-container helpers live upstream in ContainerHub - they apply to
# any project built in that image, not just this engine. Must load before first
# use (Resolve-DockerExe below).
# Rationale + measurements: ContainerHub docs/windows-container-build-performance.md
Import-Module (Resolve-BuildModulePath -Name 'WindowsContainerBuild.Reuse') -Force -Global

$docker = Resolve-DockerExe -Override $DockerExe
Write-Host "Using docker: $docker"
Write-Host "Image: $Image"
Write-Host "Configurations: $Configurations"

$isolationArgs = Get-ContainerIsolationArgs -Isolation $Isolation -CpuCount $CpuCount -MemoryGb $MemoryGb

# Arguments handed to the image entrypoint (VsDevCmd + ASAN runtime PATH, then %*).
# Every token must be free of spaces: it travels docker CLI -> cmd /S /C -> %*.
function Get-BuildCommandArgs {
  param([Parameter(Mandatory)][string]$WorkspacePath)

  $psArgs = @(
    'pwsh', '-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', (Join-Path $WorkspacePath 'Scripts\Windows\Build-Windows.ps1'),
    '-Configurations', $Configurations,
    '-SkipTidy', '-SkipPerfTests', '-SkipMsix'
  )
  if (-not $RunTests) { $psArgs += '-SkipTests' }
  if ($ParallelJobs -gt 0) { $psArgs += @('-ParallelJobs', "$ParallelJobs") }
  return $psArgs
}

# Persistent compiler cache - in the CONTAINER filesystem, deliberately NOT on
# a named volume.
#
# The volume was the cause of the "every sccache write fails" mystery.
# Diagnosed 2026-07-20 by running the server by hand with SCCACHE_LOG=trace:
# every DiskCache::put_raw died with os error 3 ("The system cannot find the
# path specified") when SCCACHE_DIR sat on the wcifs volume mount, while
# PowerShell in the same container could write the same paths - including
# \?\-prefixed ones - without error. Pointing SCCACHE_DIR at a
# container-local directory made the very next compile pair go miss -> HIT
# with zero write errors, so the failure is specific to how the sccache
# server writes (tempfile + rename) on a wcifs volume, and no cache written
# through that mount was ever going to persist anything.
#
# Container-local means the cache dies with the container - which is fine,
# because builds run in the PERSISTENT container (bb-build-persistent); the
# cache survives exactly as long as the thing that uses it. A volume that
# takes 100% write errors persisted nothing anyway.
$sccacheDir = 'C:\sccache-local'
$cacheArgs = @(
  '-e', "SCCACHE_DIR=${sccacheDir}",
  '-e', 'SCCACHE_CACHE_SIZE=20G',
  # Without these, a failing cache write is silent: sccache reports the count
  # in its stats and discards the reason. Measured 2026-07-20: 66 write errors
  # out of 66 misses, i.e. every single write failing, with no way to see why.
  # The error log must NOT live under $sccacheDir: sccache's disk cache
  # creates its own directory, but the server OPENS THE ERROR LOG FIRST and
  # dies if its parent does not exist - and then every sccache-wrapped tool
  # fails with "Timed out waiting for server startup". With
  # RUSTC_WRAPPER=sccache that poisons even `cargo tree`, which corrosion
  # reports as "Failed to find a dependency on cxxbridge-cmd" - three
  # indirections from the cause. C:\ always exists.
  '-e', 'SCCACHE_ERROR_LOG=C:\sccache-error.log',
  '-e', 'SCCACHE_LOG=warn',
  # Build trees are streamed in for incremental builds - do not wipe them.
  '-e', 'KATAGLYPHIS_KEEP_BUILD_ROOT=1'
)

# NOTE: mounting the build directory as a named volume was TRIED and does not
# work here - CMake's compiler test fails inside a mounted volume with
# "ninja: error: loading 'build.ninja': The system cannot find the file
# specified", both with a fresh volume and a populated one. See
# docs/container-build-caching.md for the full measurement.

$persistentContainerName = 'bb-build-persistent'

function Test-BindMountUsable {
  # Bind mounting looks like the obvious win - no tar transport at all - and it
  # is SLOWER here. Measured 2026-07-19 on this Dev Drive host, same tree, both
  # transports at C:\ws:
  #
  #   no-change build   tar-pipe + reused container   9.6 s ninja / 44 s wall
  #   no-change build   bind mount                   32.7 s ninja / 159 s wall
  #   cold build        tar-pipe                    327.9 s / 364 s
  #   cold build        bind mount                  318.1 s / 474 s
  #
  # Removing the transport does not pay for what it adds: the build tree then
  # lives on the Dev Drive and every ninja stat and object write crosses the
  # bindFlt filter from inside the container. Copying the sources in bulk once
  # is cheaper than paying filtered I/O on ~1000 targets. Repeated to confirm
  # it was not a first-run artifact (32.7 s vs 34.5 s).
  #
  # Kept behind an opt-in switch because the trade may invert elsewhere: on a
  # non-Dev-Drive volume, or with a much smaller build tree, the transport can
  # dominate instead.
  if (-not $UseBindMount) { return $false }
  Write-Host 'Probing bind mount support...'
  return (Test-ContainerBindMount -DockerExe $docker -Image $Image -SourcePath $repoRoot `
      -TargetPath $mountTarget -ProbeFile 'CMakePresets.json' -RunArgs $isolationArgs)
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
  if ($UseBindMount) {
    Write-Host 'Bind mount requested but unusable - falling back to tar-pipe transport.'
  } else {
    Write-Host 'Using tar-pipe transport with a reusable container (faster here; -UseBindMount to override).'
  }
  $container = $persistentContainerName
  $ws = 'C:\ws'

  # Returns the container actually used: a blocked -Fresh removal falls back to
  # a uniquely named container, so never assume it matches $persistentContainerName.
  $containerInfo = Get-ReusableBuildContainer -DockerExe $docker -Name $container -Image $Image `
    -RunArgs ($isolationArgs + $cacheArgs) -Fresh:$FreshContainer
  $reusedContainer = $containerInfo.Reused
  $container = $containerInfo.Name

  try {
    & $docker exec $container cmd /c "mkdir $ws" | Out-Null

    $null = Initialize-ContainerPwsh -DockerExe $docker -Container $container

    Write-Host 'Streaming sources into the container (excluding .git and build trees)...'
    if ($reusedContainer) {
        Write-Host 'Pruning stale sources from the reusable container...'
        $null = Remove-StaleContainerSources -DockerExe $docker -Container $container `
          -WorkspacePath $ws -KeepDirs @('logs', 'sccache-local')
    }

    # Anchor the build-tree excludes to the repo root (./...): unanchored
    # patterns match at every path depth in bsdtar and would strip nested
    # files like ExternalLib/Kataglyphis-ContainerHub/windows/build.ps1.
    # The host-side cargo target tree is excluded too: the container builds
    # its own Rust artifacts under the build dirs, and a stale incremental
    # cache streamed in once wedged every later transfer ("Can't unlink
    # already-existing object: Permission denied", observed 2026-08-02).
    $sourcesIn = Copy-IntoBuildContainer -DockerExe $docker -Container $container `
      -SourceRoot $repoRoot -TargetPath $ws `
      -Exclude @('.git', './logs', './build', './build-*', './build_*',
                 './ExternalLib/Kataglyphis-RustProjectTemplate/target')
    if (-not $sourcesIn) { throw 'Source transfer failed.' }

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
        $treeIn = Copy-IntoBuildContainer -DockerExe $docker -Container $container `
          -SourceRoot $repoRoot -TargetPath $ws `
          -Items @($buildDirName) -Exclude @("$buildDirName/cargo")
        if (-not $treeIn) {
          Write-Host "  transfer reported errors - ninja will rebuild whatever did not arrive"
        }
      }
    }

    # The build tree streamed from the host carries a CMakeCache.txt with HOST
    # source-directory paths (D:/...). Inside the container the source is at
    # C:/ws/..., so CMake rejects the cache. Delete it so CMake reconfigures
    # from scratch with container-local paths. Object files survive (ninja
    # incremental), so this is fast after the first reconfigure.
    Write-Host 'Deleting stale CMakeCache.txt (container paths differ from host)...'
    & $docker exec $container cmd /c "if exist $ws\build-clangcl-debug\CMakeCache.txt del /q $ws\build-clangcl-debug\CMakeCache.txt 2>nul"
    # Also remove any stale CMakeFiles directory that could interfere
    & $docker exec $container cmd /c "if exist $ws\build-clangcl-debug\CMakeFiles rmdir /s /q $ws\build-clangcl-debug\CMakeFiles 2>nul"

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
      # Dropping the heavy intermediates keeps the transfer small while the
      # host still gets the executables, debug info, compile database and logs
      # it needs. The container keeps the full tree, so nothing here has to
      # seed a rebuild.
      $artifactsOut = Copy-FromBuildContainer -DockerExe $docker -Container $container `
        -SourcePath $ws -TargetRoot $repoRoot -Items $existing `
        -Exclude @('*/CMakeFiles', '*/_deps', '*/cargo', '*.obj', '*.lib', '*.ilk', '*.pcm', '*/corrosion')
      if (-not $artifactsOut) {
        Write-Warning 'Artifact extraction reported errors - check executable timestamps.'
      }
    }

    if ($buildExit -ne 0) { throw "Container build failed (exit $buildExit)." }

    # A green build is not proof that anything was produced or delivered —
    # both halves of that have already failed here silently (see
    # Test-BuildArtifactsDelivered upstream). Throws on either failure mode.
    foreach ($dir in $existing) {
      if ($dir -eq 'logs') { continue }
      $delivered = Test-BuildArtifactsDelivered -DockerExe $docker -Container $container `
        -WorkspacePath $ws -Directory $dir -HostRoot $repoRoot
      Write-Host "Verified $delivered executable(s) delivered from $dir."
    }
  } finally {
    # The container is intentionally reused across builds - that is what makes
    # builds incremental without moving the tree. Remove it with
    # 'docker rm -f bb-build-persistent' (upstream Remove-BuildContainerSafe
    # tolerates the wcifs teardown lock) or rerun with -FreshContainer.
    Write-Host "Keeping build container '$container' for the next build (reset: -FreshContainer)."
  }
}

if (Test-BindMountUsable) {
  Invoke-BindMountBuild
} else {
  Invoke-TarPipeBuild
}

Write-Host 'Container build finished successfully.'

