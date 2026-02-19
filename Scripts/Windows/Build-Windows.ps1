param(
  [string[]]$Configurations = @('all'),
  [string]$BuildDir,
  [string]$BuildDirRelease,
  [string]$ClangProfilePreset = 'x64-ClangCL-Windows-Profile',
  [switch]$CoverageShowSources,
  [switch]$RunClangFormat,
  [switch]$RunClangTidy,
  [switch]$CreateMsix,
  [string]$MsixIdentityName = 'GraphicsEngine',
  [string]$MsixPublisher = 'CN=Jonas Heinle',
  [string]$MsixVersion = '1.4.2.0',
  [string]$MsixArchitecture = 'x64',
  [string]$MsixDisplayName = 'GraphicsEngine',
  [string]$MsixPublisherDisplayName = 'Jonas Heinle',
  [string]$MsixExecutable = 'bin/GraphicsEngine.exe',
  [string]$MsixCertPath,
  [string]$MsixCertPassword
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

function Find-WindowsSdkTool {
  param(
    [Parameter(Mandatory)]
    [string]$ToolName
  )

  $command = Get-Command $ToolName -ErrorAction SilentlyContinue
  if ($null -ne $command -and -not [string]::IsNullOrWhiteSpace($command.Source)) {
    return $command.Source
  }

  $kitsRoot = 'C:\Program Files (x86)\Windows Kits\10\bin'
  if (-not (Test-Path $kitsRoot)) {
    return $null
  }

  $sdkVersions = Get-ChildItem -Path $kitsRoot -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
    Sort-Object { [version]$_.Name } -Descending

  foreach ($sdkVersion in $sdkVersions) {
    $candidate = Join-Path $sdkVersion.FullName (Join-Path 'x64' $ToolName)
    if (Test-Path $candidate) {
      return $candidate
    }
  }

  return $null
}

function ConvertTo-XmlEscaped {
  param(
    [AllowNull()]
    [string]$Value
  )

  if ($null -eq $Value) {
    return ''
  }

  return [System.Security.SecurityElement]::Escape($Value)
}

function ConvertTo-MsixRelativePath {
  param(
    [AllowNull()]
    [string]$Value
  )

  if ([string]::IsNullOrWhiteSpace($Value)) {
    return ''
  }

  $normalizedPath = $Value.Replace('\\', '/').Replace('\', '/')
  while ($normalizedPath.StartsWith('./')) {
    $normalizedPath = $normalizedPath.Substring(2)
  }

  while ($normalizedPath.StartsWith('/')) {
    $normalizedPath = $normalizedPath.Substring(1)
  }

  return $normalizedPath
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
Write-BuildLog -Context $buildContext -Message "CREATE_MSIX=$CreateMsix"

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
      Invoke-BuildExternal -Context $buildContext -File 'cmake' -Parameters @('--build', $BuildDirRelease)
      Invoke-BuildExternal -Context $buildContext -File 'cmake' -Parameters @('--build', $BuildDirRelease, '--target', 'package', '--verbose')

      if ($CreateMsix) {
        $makeAppxPath = Find-WindowsSdkTool -ToolName 'makeappx.exe'
        if ([string]::IsNullOrWhiteSpace($makeAppxPath)) {
          throw 'MSIX packaging requested, but makeappx.exe was not found. Install Windows 10/11 SDK or add it to PATH.'
        }

        $msixRoot = Join-Path $BuildDirRelease 'msix'
        $stagingRoot = Join-Path $msixRoot 'staging'
        $assetsDir = Join-Path $stagingRoot 'Assets'

        Remove-BuildRoot -Context $buildContext -Path $msixRoot | Out-Null
        New-Item -Path $assetsDir -ItemType Directory -Force | Out-Null

        Invoke-BuildExternal -Context $buildContext -File 'cmake' -Parameters @('--install', $BuildDirRelease, '--prefix', $stagingRoot)

        $logoSource = Join-Path $workspaceRoot 'images\logo.png'
        if (-not (Test-Path $logoSource)) {
          throw "MSIX packaging requested, but logo file not found: $logoSource"
        }

        Copy-Item -Path $logoSource -Destination (Join-Path $assetsDir 'StoreLogo.png') -Force
        Copy-Item -Path $logoSource -Destination (Join-Path $assetsDir 'Square44x44Logo.png') -Force
        Copy-Item -Path $logoSource -Destination (Join-Path $assetsDir 'Square150x150Logo.png') -Force

        $identityNameEscaped = ConvertTo-XmlEscaped -Value $MsixIdentityName
        $publisherEscaped = ConvertTo-XmlEscaped -Value $MsixPublisher
        $versionEscaped = ConvertTo-XmlEscaped -Value $MsixVersion
        $displayNameEscaped = ConvertTo-XmlEscaped -Value $MsixDisplayName
        $publisherDisplayNameEscaped = ConvertTo-XmlEscaped -Value $MsixPublisherDisplayName
        $msixExecutablePath = ConvertTo-MsixRelativePath -Value $MsixExecutable
        $executableEscaped = ConvertTo-XmlEscaped -Value $msixExecutablePath

        $manifestContent = @"
<?xml version="1.0" encoding="utf-8"?>
<Package
  xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
  xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
  xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"
  IgnorableNamespaces="uap rescap">
  <Identity Name="$identityNameEscaped" Publisher="$publisherEscaped" Version="$versionEscaped" ProcessorArchitecture="$MsixArchitecture" />
  <Properties>
    <DisplayName>$displayNameEscaped</DisplayName>
    <PublisherDisplayName>$publisherDisplayNameEscaped</PublisherDisplayName>
    <Logo>Assets/StoreLogo.png</Logo>
  </Properties>
  <Dependencies>
    <TargetDeviceFamily Name="Windows.Desktop" MinVersion="10.0.17763.0" MaxVersionTested="10.0.26100.0" />
  </Dependencies>
  <Resources>
    <Resource Language="en-us" />
  </Resources>
  <Applications>
    <Application Id="App" Executable="$executableEscaped" EntryPoint="Windows.FullTrustApplication">
      <uap:VisualElements
        DisplayName="$displayNameEscaped"
        Description="$displayNameEscaped"
        Square44x44Logo="Assets/Square44x44Logo.png"
        Square150x150Logo="Assets/Square150x150Logo.png"
        BackgroundColor="transparent" />
    </Application>
  </Applications>
  <Capabilities>
    <rescap:Capability Name="runFullTrust" />
  </Capabilities>
</Package>
"@

        $manifestPath = Join-Path $stagingRoot 'AppxManifest.xml'
        Set-Content -Path $manifestPath -Value $manifestContent -Encoding UTF8

        $msixFileName = "$MsixIdentityName-$MsixVersion-$MsixArchitecture.msix"
        $msixOutputPath = Join-Path $msixRoot $msixFileName
        Invoke-BuildExternal -Context $buildContext -File $makeAppxPath -Parameters @('pack', '/d', $stagingRoot, '/p', $msixOutputPath, '/o')

        if (-not [string]::IsNullOrWhiteSpace($MsixCertPath)) {
          $signtoolPath = Find-WindowsSdkTool -ToolName 'signtool.exe'
          if ([string]::IsNullOrWhiteSpace($signtoolPath)) {
            throw 'MSIX signing requested, but signtool.exe was not found. Install Windows 10/11 SDK or add it to PATH.'
          }

          if (-not (Test-Path $MsixCertPath)) {
            throw "MSIX signing certificate not found: $MsixCertPath"
          }

          $signArguments = @('sign', '/fd', 'SHA256', '/f', $MsixCertPath, '/a')
          if (-not [string]::IsNullOrWhiteSpace($MsixCertPassword)) {
            $signArguments += @('/p', $MsixCertPassword)
          }

          $signArguments += $msixOutputPath
          Invoke-BuildExternal -Context $buildContext -File $signtoolPath -Parameters $signArguments
        } else {
          Write-BuildLog -Context $buildContext -Message "MSIX package created but not signed: $msixOutputPath"
        }
      }
    } | Out-Null
  }
} finally {
  Write-BuildSummary -Context $buildContext
  Close-BuildLog -Context $buildContext
}

if ($buildContext.Results.Failed.Count -gt 0) {
  exit 1
}
