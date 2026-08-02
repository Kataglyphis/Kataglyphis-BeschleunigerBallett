Set-StrictMode -Version Latest
#requires -Version 7.0


# Import shared helpers (Resolve-DirectoryPath, New-Timestamp, etc.)
# Prefer the ContainerHub copy of the shared helpers so the whole session
# uses ONE WindowsScripts.Shared module - a second path-distinct copy would
# knock the globally imported one out via this nested -Force import.
$sharedPath = Join-Path $PSScriptRoot '..\..\..\ExternalLib\Kataglyphis-ContainerHub\windows\scripts\modules\WindowsScripts.Shared.psm1'
# No -Force when already loaded: a nested force-reimport moves the module's
# exports out of the global session state on Windows PowerShell 5.1.
if (-not (Get-Module -Name 'WindowsScripts.Shared')) { Import-Module $sharedPath }

# File enumeration (Get-ProjectCppFiles) lives upstream in
# WindowsFormatting.Common; same no-Force, load-once discipline as above.
$formattingPath = Join-Path $PSScriptRoot '..\..\..\ExternalLib\Kataglyphis-ContainerHub\windows\scripts\modules\WindowsFormatting.Common.psm1'
if (-not (Get-Module -Name 'WindowsFormatting.Common')) { Import-Module $formattingPath }

# The compile-commands database (Get-CompileCommandsDatabase) is a CMake/ninja
# concern and lives upstream in WindowsCMake.Common; same no-Force, load-once
# discipline as above.
$cmakePath = Join-Path $PSScriptRoot '..\..\..\ExternalLib\Kataglyphis-ContainerHub\windows\scripts\modules\WindowsCMake.Common.psm1'
if (-not (Get-Module -Name 'WindowsCMake.Common')) { Import-Module $cmakePath }

function Invoke-ClangTidyFixStep {
  param(
    [Parameter(Mandatory)]
    [pscustomobject]$Context,
    [Parameter(Mandatory)]
    [string]$WorkspacePath,
    [Parameter(Mandatory)]
    [string]$BuildRoot,
    # Do not force project-specific clang-tidy checks by default. Leave the
    # checks list empty so callers can opt-in or provide their own set of
    # --checks arguments. The ContainerHub previously enforced
    # --checks=-misc-include-cleaner which caused issues for some clang-tidy
    # versions (crashes); remove the imposed flag to avoid that.
    [string[]]$Checks = @(),
    [switch]$Fix
  )

  $clangTidyCommand = Get-Command 'clang-tidy' -ErrorAction SilentlyContinue
  if (-not $clangTidyCommand) {
    throw 'clang-tidy not found on PATH.'
  }

  $compileDb = Get-CompileCommandsDatabase -Context $Context -BuildRoot $BuildRoot

  $srcDir = Join-Path $WorkspacePath 'Src'
  # File enumeration comes from upstream WindowsFormatting.Common
  # (git-ls-files fast path + build/_deps/.venv exclusions) — scoped to Src
  # and to the extensions clang-tidy should see.
  $tidyFiles = @(Get-ProjectCppFiles -WorkspacePath $WorkspacePath |
    Where-Object { $_ -like "$srcDir*" -and [System.IO.Path]::GetExtension($_) -in @('.cpp', '.cc', '.cxx') })

  $filteredFiles = @()
  foreach ($f in $tidyFiles) {
    $content = Get-Content $f -Raw -ErrorAction SilentlyContinue
    if ($content -match '^import\s+kataglyphis') {
      Write-BuildLog -Context $Context -Message "Skipping clang-tidy for $f (uses C++20 module syntax)"
      continue
    }
    $filteredFiles += $f
  }
  $tidyFiles = $filteredFiles

  if ($tidyFiles.Count -eq 0) {
    Write-BuildLog -Context $Context -Message 'No C/C++ source files found under Src for clang-tidy.'
    return
  }

  $baseParams = @('-p', $BuildRoot) + $Checks
  # Restrict analysis to the source directory to avoid noise from ExternalLib headers
  $baseParams += "--header-filter=$([regex]::Escape($srcDir)).*"
  if ($Fix) { $baseParams += '--fix' }

  foreach ($tidyFile in $tidyFiles) {
    Invoke-BuildExternal -Context $Context -File $clangTidyCommand.Source -Parameters @($baseParams + $tidyFile) | Out-Null
  }
}

Export-ModuleMember -Function @(
  'Invoke-ClangTidyFixStep'
)

