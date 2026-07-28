Set-StrictMode -Version Latest
#requires -Version 7.0


function Get-ProjectCmakeFiles {
  param(
    [Parameter(Mandatory)]
    [string]$WorkspacePath
  )

  $gitCommand = Get-Command 'git' -ErrorAction SilentlyContinue
  if ($gitCommand) {
    try {
      $tracked = & $gitCommand.Source -C $WorkspacePath ls-files -- 'CMakeLists.txt' '**/CMakeLists.txt' '*.cmake' 2>$null
      if ($LASTEXITCODE -eq 0 -and $tracked) {
        $trackedPaths = @($tracked |
          Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
          ForEach-Object { Join-Path $WorkspacePath $_ } |
          Where-Object {
            ($_.ToString() -notmatch '\\build([\\-]|\\)') -and
            ($_.ToString() -notmatch '\\ExternalLib\\') -and
            ($_.ToString() -notmatch '\\_deps\\') -and
            ($_.ToString() -notmatch '\\vcpkg_installed\\')
          })
        return @($trackedPaths | Sort-Object -Unique)
      }
    } catch {
    }
  }

  $cmakeFiles = Get-ChildItem -Path $WorkspacePath -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object {
      ($_.Name -eq 'CMakeLists.txt' -or $_.Extension -eq '.cmake') -and
      ($_.FullName -notmatch '\\build([\\-]|\\)') -and
      ($_.FullName -notmatch '\\ExternalLib\\') -and
      ($_.FullName -notmatch '\\_deps\\') -and
      ($_.FullName -notmatch '\\.git\\modules\\') -and
      ($_.FullName -notmatch '\\vcpkg_installed\\') -and
      ($_.FullName -notmatch '\\\.venv') -and
      ($_.FullName -notmatch '\\site-packages\\')
    } |
    Select-Object -ExpandProperty FullName

  return @($cmakeFiles | Sort-Object -Unique)
}

function Get-ProjectCppFiles {
  param(
    [Parameter(Mandatory)]
    [string]$WorkspacePath
  )

  $cppExtensions = @('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.ixx')
  $gitCommand = Get-Command 'git' -ErrorAction SilentlyContinue
  if ($gitCommand) {
    try {
      $tracked = & $gitCommand.Source -C $WorkspacePath ls-files -- '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.ixx' 2>$null
      if ($LASTEXITCODE -eq 0 -and $tracked) {
        $trackedPaths = @($tracked |
          Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
          ForEach-Object { Join-Path $WorkspacePath $_ } |
          Where-Object {
            ($_.ToString() -notmatch '\\build([\\-]|\\)') -and
            ($_.ToString() -notmatch '\\ExternalLib\\') -and
            # -notmatch, not -match. This read `-match '\\_deps\\'` until
            # 2026-07-20, which inverted the intent: it kept ONLY files under a
            # CMake _deps/ directory and dropped every project source. _deps is
            # untracked, so `git ls-files` returned nothing and the whole
            # clang-format step silently formatted zero files - which is why
            # the formatting drift never shrank no matter how often the step
            # ran.
            ($_.ToString() -notmatch '\\_deps\\') -and
            ($_.ToString() -notmatch '\\vcpkg_installed\\')
          })
        return @($trackedPaths | Sort-Object -Unique)
      }
    } catch {
    }
  }

  # This fallback is NOT rare: the container receives sources by tar-pipe, so
  # there is no .git directory, `git ls-files` fails, and everything below is
  # what actually selects files during a containerized build. It must exclude
  # at least as much as the git path above - Python virtualenvs vendor C
  # headers (lxml, numpy) that are emphatically not our sources.
  $cppFiles = Get-ChildItem -Path $WorkspacePath -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object {
      ($cppExtensions -contains $_.Extension.ToLowerInvariant()) -and
      ($_.FullName -notmatch '\\build([\\-]|\\)') -and
      ($_.FullName -notmatch '\\ExternalLib\\') -and
      ($_.FullName -notmatch '\\_deps\\') -and
      ($_.FullName -notmatch '\\.git\\modules\\') -and
      ($_.FullName -notmatch '\\vcpkg_installed\\') -and
      ($_.FullName -notmatch '\\\.venv') -and
      ($_.FullName -notmatch '\\site-packages\\')
    } |
    Select-Object -ExpandProperty FullName

  return @($cppFiles | Sort-Object -Unique)
}

function Initialize-UvVenvPython {
  param(
    [Parameter(Mandatory)]
    [pscustomobject]$Context,
    [Parameter(Mandatory)]
    [string]$WorkspacePath,
    [string]$PythonVersion = '3.12',
    [string]$EnvName = '.venv'
  )

  $uvCommand = Get-Command 'uv' -ErrorAction SilentlyContinue
  if (-not $uvCommand) {
    throw 'uv not found on PATH. Install Astral uv before running formatting steps.'
  }

  $venvPath = Join-Path $WorkspacePath $EnvName
  $venvPython = Join-Path $venvPath 'Scripts\python.exe'
  $requirementsPath = Join-Path $WorkspacePath 'requirements.txt'

  $logInfo = {
    param([string]$Message)
    Write-BuildLog -Context $Context -Message $Message
  }
  $logWarning = {
    param([string]$Message)
    Write-BuildLogWarning -Context $Context -Message $Message
  }
  $commandRunner = {
    param([string]$File, [string[]]$Parameters)
    Invoke-BuildExternal -Context $Context -File $File -Parameters $Parameters | Out-Null
  }

  $venvNeedsCreation = -not (Test-Path $venvPython)
  if (-not $venvNeedsCreation) {
    try {
      & $venvPython '-c' 'import sys; sys.exit(0)' 2>$null | Out-Null
      if ($LASTEXITCODE -ne 0) {
        & $logWarning "Existing venv at $venvPath is not functional; recreating."
        $venvNeedsCreation = $true
      }
    } catch {
      & $logWarning "Existing venv at $venvPath is not functional; recreating."
      $venvNeedsCreation = $true
    }
  }

  if ($venvNeedsCreation) {
    New-UvProjectEnvironment -Workspace $WorkspacePath -PythonVersion $PythonVersion -EnvName $EnvName `
      -CommandRunner $commandRunner -LogInfo $logInfo -LogWarning $logWarning | Out-Null
    $env:UV_PROJECT_ENVIRONMENT = $venvPath
  }

  if (-not (Test-Path $requirementsPath)) {
    Write-BuildLog -Context $Context -Message "No requirements.txt found at $requirementsPath, skipping dependency sync."
    return $venvPython
  }

  Write-BuildLog -Context $Context -Message "Installing requirements from $requirementsPath..."
  Invoke-BuildExternal -Context $Context -File $uvCommand.Source -Parameters @('pip', 'install', '--python', $venvPython, '-r', $requirementsPath) | Out-Null

  return $venvPython
}

function Invoke-CmakeFormatStep {
  param(
    [Parameter(Mandatory)]
    [pscustomobject]$Context,
    [Parameter(Mandatory)]
    [string]$WorkspacePath
  )

  $venvPython = Initialize-UvVenvPython -Context $Context -WorkspacePath $WorkspacePath
  $cmakeFormatExe = Join-Path (Split-Path $venvPython -Parent) 'cmake-format.exe'
  if (-not (Test-Path $cmakeFormatExe)) {
    throw "cmake-format not found in venv: $cmakeFormatExe"
  }

  $formatConfig = Join-Path $WorkspacePath '.cmake-format.yaml'
  $cmakeFiles = @(Get-ProjectCmakeFiles -WorkspacePath $WorkspacePath)
  if ($cmakeFiles.Count -eq 0) {
    Write-BuildLog -Context $Context -Message 'No CMake files found for cmake-format.'
    return
  }

  foreach ($cmakeFile in $cmakeFiles) {
    if (Test-Path $formatConfig) {
      Invoke-BuildExternal -Context $Context -File $cmakeFormatExe -Parameters @('-c', $formatConfig, '--in-place', $cmakeFile) | Out-Null
    } else {
      Invoke-BuildExternal -Context $Context -File $cmakeFormatExe -Parameters @('--in-place', $cmakeFile) | Out-Null
    }
  }
}

function Invoke-ClangFormatStep {
  param(
    [Parameter(Mandatory)]
    [pscustomobject]$Context,
    [Parameter(Mandatory)]
    [string]$WorkspacePath
  )

  $clangFormat = Get-Command 'clang-format' -ErrorAction SilentlyContinue
  if (-not $clangFormat) {
    $commonPaths = @(
      'C:\Program Files\LLVM\bin\clang-format.exe',
      'C:\Program Files (x86)\LLVM\bin\clang-format.exe'
    )
    foreach ($path in $commonPaths) {
      if (Test-Path $path) {
        $clangFormatSource = $path
        break
      }
    }

    if (-not $clangFormatSource) {
      throw 'clang-format not found on PATH or in common installation locations.'
    }
  } else {
    $clangFormatSource = $clangFormat.Source
  }

  $cppFiles = @(Get-ProjectCppFiles -WorkspacePath $WorkspacePath)
  if ($cppFiles.Count -eq 0) {
    Write-BuildLog -Context $Context -Message 'No C/C++ files found for clang-format.'
    return
  }

  foreach ($cppFile in $cppFiles) {
    Invoke-BuildExternal -Context $Context -File $clangFormatSource -Parameters @('-i', $cppFile) | Out-Null
  }
}

<#
.SYNOPSIS
  Reports how many sources deviate from .clang-format WITHOUT rewriting them.

.DESCRIPTION
  Invoke-ClangFormatStep runs `clang-format -i`, which rewrites in place. That
  makes it unusable as a routine check here: 72 of 125 own sources under Src/
  and Test/ currently deviate (measured 2026-07-19), so running it would
  produce one enormous reformatting commit as a side effect of asking a
  question. Whether to take that sweep is a deliberate decision - it collides
  with everything in flight and wants a .git-blame-ignore-revs entry.

  This uses `--dry-run -Werror`, which changes nothing and exits non-zero per
  deviating file, so drift can be tracked over time. It deliberately does NOT
  fail the build: with a known 72-file backlog a failing gate would be
  switched off within a day. Make it fail only once the count is near zero.
#>
function Invoke-ClangFormatCheck {
  param(
    [Parameter(Mandatory)]$Context,
    [Parameter(Mandatory)][string]$WorkspacePath
  )

  $clangFormat = Get-Command 'clang-format' -ErrorAction SilentlyContinue
  if (-not $clangFormat) {
    $candidates = @(
      'C:\Program Files\LLVM\bin\clang-format.exe',
      'C:\Program Files (x86)\LLVM\bin\clang-format.exe'
    )
    $clangFormatSource = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $clangFormatSource) {
      Write-BuildLog -Context $Context -Message 'clang-format not found; skipping format check.'
      return
    }
  } else {
    $clangFormatSource = $clangFormat.Source
  }

  $cppFiles = @(Get-ProjectCppFiles -WorkspacePath $WorkspacePath)
  if ($cppFiles.Count -eq 0) {
    Write-BuildLog -Context $Context -Message 'No C/C++ files found for the clang-format check.'
    return
  }

  $deviating = New-Object System.Collections.Generic.List[string]

  # clang-format --dry-run -Werror exits non-zero for every deviating file -
  # that IS the signal here, not an error. PowerShell 7.3+ defaults
  # $PSNativeCommandUseErrorActionPreference to true, so under the build's
  # $ErrorActionPreference = 'Stop' each deviating file would throw and abort
  # the step on the first hit.
  # Every deviating file makes clang-format exit non-zero AND write to stderr,
  # and here both are the expected signal rather than a failure. Getting that
  # past PowerShell took two tries: PowerShell 7.3+ turns a non-zero native
  # exit into a throw under $ErrorActionPreference='Stop', and Windows
  # PowerShell 5.1 (which the build container runs) turns redirected native
  # stderr into a terminating ErrorRecord. Dispatching through cmd.exe sidesteps
  # both - cmd swallows the output and only the exit code comes back.
  foreach ($cppFile in $cppFiles) {
    $quoted = '"{0}" --dry-run -Werror "{1}" >nul 2>nul' -f $clangFormatSource, $cppFile
    & cmd.exe /c $quoted
    if ($LASTEXITCODE -ne 0) { $deviating.Add($cppFile) }
  }

  Write-BuildLog -Context $Context -Message ("clang-format: {0} of {1} files deviate from .clang-format." -f $deviating.Count, $cppFiles.Count)
  if ($deviating.Count -gt 0) {
    Write-BuildLog -Context $Context -Message 'Not a build failure by design - see BACKLOG.md "Decide on the formatting sweep".'
    foreach ($f in ($deviating | Select-Object -First 20)) {
      Write-BuildLog -Context $Context -Message ("  deviates: {0}" -f $f)
    }
    if ($deviating.Count -gt 20) {
      Write-BuildLog -Context $Context -Message ("  ... and {0} more" -f ($deviating.Count - 20))
    }
  }
}

Export-ModuleMember -Function Get-ProjectCmakeFiles, Get-ProjectCppFiles, Initialize-UvVenvPython, Invoke-CmakeFormatStep, Invoke-ClangFormatStep, Invoke-ClangFormatCheck
