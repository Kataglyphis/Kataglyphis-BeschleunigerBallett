Set-StrictMode -Version Latest

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
      ($_.FullName -notmatch '\\vcpkg_installed\\')
    } |
    Select-Object -ExpandProperty FullName

  return @($cmakeFiles | Sort-Object -Unique)
}

function Get-ProjectCppFiles {
  param(
    [Parameter(Mandatory)]
    [string]$WorkspacePath
  )

  $cppExtensions = @('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp')
  $gitCommand = Get-Command 'git' -ErrorAction SilentlyContinue
  if ($gitCommand) {
    try {
      $tracked = & $gitCommand.Source -C $WorkspacePath ls-files -- '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' 2>$null
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

  $cppFiles = Get-ChildItem -Path $WorkspacePath -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object {
      ($cppExtensions -contains $_.Extension.ToLowerInvariant()) -and
      ($_.FullName -notmatch '\\build([\\-]|\\)') -and
      ($_.FullName -notmatch '\\ExternalLib\\') -and
      ($_.FullName -notmatch '\\_deps\\') -and
      ($_.FullName -notmatch '\\.git\\modules\\') -and
      ($_.FullName -notmatch '\\vcpkg_installed\\')
    } |
    Select-Object -ExpandProperty FullName

  return @($cppFiles | Sort-Object -Unique)
}

function Initialize-UvVenvPython {
  param(
    [Parameter(Mandatory)]
    [pscustomobject]$Context,
    [Parameter(Mandatory)]
    [string]$WorkspacePath
  )

  $uvCommand = Get-Command 'uv' -ErrorAction SilentlyContinue
  if (-not $uvCommand) {
    throw 'uv not found on PATH. Install Astral uv before running formatting steps.'
  }

  $venvPath = Join-Path $WorkspacePath '.venv'
  $venvPython = Join-Path $venvPath 'Scripts\python.exe'
  $requirementsPath = Join-Path $WorkspacePath 'requirements.txt'

  if (-not (Test-Path $venvPython)) {
    Invoke-BuildExternal -Context $Context -File $uvCommand.Source -Parameters @('venv', '--allow-existing', $venvPath) | Out-Null
  }

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
    throw 'clang-format not found on PATH.'
  }

  $cppFiles = @(Get-ProjectCppFiles -WorkspacePath $WorkspacePath)
  if ($cppFiles.Count -eq 0) {
    Write-BuildLog -Context $Context -Message 'No C/C++ files found for clang-format.'
    return
  }

  foreach ($cppFile in $cppFiles) {
    Invoke-BuildExternal -Context $Context -File $clangFormat.Source -Parameters @('-i', $cppFile) | Out-Null
  }
}

Export-ModuleMember -Function Get-ProjectCmakeFiles, Get-ProjectCppFiles, Initialize-UvVenvPython, Invoke-CmakeFormatStep, Invoke-ClangFormatStep
