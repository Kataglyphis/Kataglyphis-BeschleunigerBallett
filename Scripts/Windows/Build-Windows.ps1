param(
  [string[]]$Configurations = @('all'),
  [string]$BuildDir,
  [string]$BuildDirRelease,
  [string]$ClangProfilePreset = 'x64-ClangCL-Windows-Profile',
  [switch]$CoverageShowSources,
  [switch]$RunClangFormat,
  [switch]$RunClangTidy
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$workspaceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$moduleRoot = Join-Path $workspaceRoot 'ExternalLib\Kataglyphis-ContainerHub\windows\scripts\modules'
$buildModulePath = Join-Path $moduleRoot 'WindowsBuild.Common.psm1'

if (-not (Test-Path $buildModulePath)) {
  throw "Required module not found: $buildModulePath"
}

Import-Module $buildModulePath -Force

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
  $BuildDir = Join-Path $workspaceRoot 'build'
}

if ([string]::IsNullOrWhiteSpace($BuildDirRelease)) {
  $BuildDirRelease = Join-Path $workspaceRoot 'build_release'
}

if ([string]::IsNullOrWhiteSpace($env:NUMBER_OF_PROCESSORS)) {
  $env:NUMBER_OF_PROCESSORS = [Environment]::ProcessorCount.ToString()
}

$availableConfigurations = @('msvc-debug', 'msvc-release', 'clang-debug', 'profile', 'clang-release')
$selectedConfigurations = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)

if ($null -eq $Configurations -or $Configurations.Count -eq 0 -or ($Configurations -contains 'all')) {
  foreach ($configurationName in $availableConfigurations) {
    $selectedConfigurations.Add($configurationName) | Out-Null
  }
} else {
  foreach ($configurationName in $Configurations) {
    if ([string]::IsNullOrWhiteSpace($configurationName)) {
      continue
    }

    $normalized = $configurationName.Trim().ToLowerInvariant()
    if (-not ($availableConfigurations -contains $normalized)) {
      throw "Unknown configuration '$configurationName'. Supported values: all, $($availableConfigurations -join ', ')"
    }

    $selectedConfigurations.Add($normalized) | Out-Null
  }
}

function Test-ConfigurationSelected {
  param(
    [Parameter(Mandatory)]
    [string]$Name
  )

  return $selectedConfigurations.Contains($Name)
}

function Get-ProjectSourceFiles {
  param(
    [Parameter(Mandatory)]
    [string]$RootPath,
    [Parameter(Mandatory)]
    [string[]]$Extensions
  )

  $sourceRoot = Join-Path $RootPath 'Src'
  if (-not (Test-Path $sourceRoot)) {
    return @()
  }

  return Get-ChildItem -Path $sourceRoot -Recurse -File |
    Where-Object { $Extensions -contains $_.Extension.ToLowerInvariant() } |
    ForEach-Object { $_.FullName }
}

$buildContext = New-BuildContext -Workspace $workspaceRoot -LogDir 'logs\windows' -StopOnError
Open-BuildLog -Context $buildContext

# Keep behavior close to the old workflow; image should already contain toolchains.
if (Test-Path 'C:\Program Files\LLVM\bin') {
  $env:Path = 'C:\Program Files\LLVM\bin;' + $env:Path
}

Write-BuildLog -Context $buildContext -Message "BUILD_DIR=$BuildDir"
Write-BuildLog -Context $buildContext -Message "BUILD_DIR_RELEASE=$BuildDirRelease"
Write-BuildLog -Context $buildContext -Message "CLANG_PROFILE_PRESET=$ClangProfilePreset"
Write-BuildLog -Context $buildContext -Message "CONFIGURATIONS=$(([string[]]$selectedConfigurations -join ', '))"
Write-BuildLog -Context $buildContext -Message "COVERAGE_SHOW_SOURCES=$CoverageShowSources"
Write-BuildLog -Context $buildContext -Message "RUN_CLANG_FORMAT=$RunClangFormat"
Write-BuildLog -Context $buildContext -Message "RUN_CLANG_TIDY=$RunClangTidy"

try {
  if (Test-ConfigurationSelected -Name 'msvc-debug') {
    Invoke-BuildStep -Context $buildContext -StepName 'MSVC Debug' -Critical -Script {
      Invoke-BuildExternal -Context $buildContext -File 'cmake' -Parameters @('-B', $BuildDir, '--preset', 'x64-MSVC-Windows-Debug', '-Dmyproject_ENABLE_CPPCHECK=OFF', '-DWINDOWS_CI=ON')
      Invoke-BuildExternal -Context $buildContext -File 'cmake' -Parameters @('--build', $BuildDir)

      Invoke-BuildExternal -Context $buildContext -File 'ctest' -Parameters @('--test-dir', $BuildDir, '--output-on-failure')

      Remove-BuildRoot -Context $buildContext -Path $BuildDir | Out-Null
    } | Out-Null
  }

  if (Test-ConfigurationSelected -Name 'msvc-release') {
    Invoke-BuildStep -Context $buildContext -StepName 'MSVC Release' -Critical -Script {
      Invoke-BuildExternal -Context $buildContext -File 'cmake' -Parameters @('-B', $BuildDir, '--preset', 'x64-MSVC-Windows-Release', '-Dmyproject_ENABLE_CPPCHECK=OFF')
      Invoke-BuildExternal -Context $buildContext -File 'cmake' -Parameters @('--build', $BuildDir)
      Remove-BuildRoot -Context $buildContext -Path $BuildDir | Out-Null
    } | Out-Null
  }

  if (Test-ConfigurationSelected -Name 'clang-debug') {
    Invoke-BuildStep -Context $buildContext -StepName 'ClangCL Debug' -Critical -Script {
      Invoke-BuildExternal -Context $buildContext -File 'cmake' -Parameters @('-B', $BuildDir, '--preset', 'x64-ClangCL-Windows-Debug', '-Dmyproject_ENABLE_CPPCHECK=OFF')
      Invoke-BuildExternal -Context $buildContext -File 'cmake' -Parameters @('--build', $BuildDir)

      if ($RunClangFormat) {
        $formatFiles = Get-ProjectSourceFiles -RootPath $workspaceRoot -Extensions @('.h', '.hpp', '.c', '.cc', '.cpp')
        foreach ($file in $formatFiles) {
          Invoke-BuildExternal -Context $buildContext -File 'clang-format' -Parameters @('-i', $file)
        }
      }

      if ($RunClangTidy) {
        $compileCommands = Join-Path $BuildDir 'compile_commands.json'
        if (Test-Path $compileCommands) {
          $tidyFiles = Get-ProjectSourceFiles -RootPath $workspaceRoot -Extensions @('.c', '.cc', '.cpp')
          foreach ($file in $tidyFiles) {
            Invoke-BuildExternal -Context $buildContext -File 'clang-tidy' -Parameters @('-p', $BuildDir, '--quiet', '--fix', '--fix-errors', '--format-style=file', $file)
          }
        } else {
          Write-BuildLog -Context $buildContext -Message "Skipping clang-tidy: compile_commands.json not found in $BuildDir"
        }
      }

      Push-Location $BuildDir
      try {
        Invoke-BuildExternal -Context $buildContext -File 'ctest' -Parameters @('--test-dir', $BuildDir, '--output-on-failure')
        Invoke-BuildExternal -Context $buildContext -File 'llvm-profdata.exe' -Parameters @('merge', '-sparse', 'Test\compile\default.profraw', '-o', 'compileTestSuite.profdata')
        Invoke-BuildExternal -Context $buildContext -File 'llvm-cov.exe' -Parameters @('report', 'compileTestSuite.exe', '-instr-profile=compileTestSuite.profdata')
        & 'llvm-cov.exe' export 'compileTestSuite.exe' -format=text -instr-profile='compileTestSuite.profdata' | Out-File -FilePath 'coverage.json' -Encoding UTF8
        if ($CoverageShowSources) {
          Invoke-BuildExternal -Context $buildContext -File 'llvm-cov.exe' -Parameters @('show', 'compileTestSuite.exe', '-instr-profile=compileTestSuite.profdata')
        }
      } finally {
        Pop-Location
      }
    } | Out-Null

    Invoke-BuildOptional -Context $buildContext -Name 'Clang static analysis (HTML)' -Script {
      $sourceFiles = Get-ChildItem -Path 'Src' -Recurse -Include '*.cpp', '*.cc' | ForEach-Object { $_.FullName }
      if ($null -ne $sourceFiles -and $sourceFiles.Count -gt 0) {
        Invoke-BuildExternal -Context $buildContext -File 'clang++' -Parameters @('--analyze', '-DUSE_RUST=1', '-Xanalyzer', '-analyzer-output=html', $sourceFiles)
      }
    }

    Invoke-BuildOptional -Context $buildContext -Name 'Clang static analysis (scan-build)' -Script {
      New-Item -ItemType Directory -Path 'scan-build-reports' -Force | Out-Null
      Invoke-BuildExternal -Context $buildContext -File 'scan-build' -Parameters @('--use-analyzer=C:\Program Files\LLVM\bin\clang-cl.exe', '-o', 'scan-build-reports', 'cmake', '--build', $BuildDir)
    }
  }

  if (Test-ConfigurationSelected -Name 'profile') {
    Invoke-BuildStep -Context $buildContext -StepName 'Profiling build + benchmarks' -Critical -Script {
      Invoke-BuildExternal -Context $buildContext -File 'clang' -Parameters @('--version')
      Invoke-BuildExternal -Context $buildContext -File 'cmake' -Parameters @('-B', $BuildDirRelease, '--preset', $ClangProfilePreset, '-Dmyproject_ENABLE_CPPCHECK=OFF')
      Invoke-BuildExternal -Context $buildContext -File 'cmake' -Parameters @('--build', $BuildDirRelease)

      Push-Location $BuildDirRelease
      try {
        Invoke-BuildExternal -Context $buildContext -File '.\perfTestSuite.exe' -Parameters @('--benchmark_out=results.json', '--benchmark_out_format=json')
      } finally {
        Pop-Location
      }
    } | Out-Null
  }

  if (Test-ConfigurationSelected -Name 'clang-release') {
    Invoke-BuildStep -Context $buildContext -StepName 'Clang Release + package' -Critical -Script {
      Remove-BuildRoot -Context $buildContext -Path $BuildDirRelease | Out-Null
      Invoke-BuildExternal -Context $buildContext -File 'clang' -Parameters @('--version')
      Invoke-BuildExternal -Context $buildContext -File 'cmake' -Parameters @('-B', $BuildDirRelease, '--preset', 'x64-ClangCL-Windows-Release', '-Dmyproject_ENABLE_CPPCHECK=OFF', '-DWINDOWS_CI=ON')
      $env:CMAKE_BUILD_PARALLEL_LEVEL = $env:NUMBER_OF_PROCESSORS
      Invoke-BuildExternal -Context $buildContext -File 'cmake' -Parameters @('--build', $BuildDirRelease, '-DWINDOWS_CI=ON')
      Invoke-BuildExternal -Context $buildContext -File 'cmake' -Parameters @('--build', $BuildDirRelease, '--target', 'package', '--verbose')
    } | Out-Null
  }
} finally {
  Write-BuildSummary -Context $buildContext
  Close-BuildLog -Context $buildContext
}

if ($buildContext.Results.Failed.Count -gt 0) {
  exit 1
}
