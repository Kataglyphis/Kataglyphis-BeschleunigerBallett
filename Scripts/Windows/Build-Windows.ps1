param(
  [string[]]$Configurations = @('all'),
  [switch]$SkipFormat,
  [switch]$SkipTidy,
  [switch]$SkipMsix,
  [switch]$DisableIntegrationTestsMsvcDebug
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-OrDefault([string]$Value, [string]$DefaultValue) {
  if ([string]::IsNullOrWhiteSpace($Value)) { return $DefaultValue }
  return $Value
}

function Get-ConfigValue {
  param(
    [Parameter(Mandatory)]
    $Config,
    [Parameter(Mandatory)]
    [string]$Path
  )

  $cursor = $Config
  foreach ($segment in ($Path -split '\.')) {
    if ($null -eq $cursor) { return $null }
    try {
      $cursor = $cursor[$segment]
    } catch {
      return $null
    }
  }

  return $cursor
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$containerHubModulesRoot = Join-Path $repoRoot 'ExternalLib\Kataglyphis-ContainerHub\windows\scripts\modules'
$localModulesRoot = Join-Path $PSScriptRoot 'modules'

$modulePaths = @(
  (Join-Path $containerHubModulesRoot 'WindowsScripts.Shared.psm1'),
  (Join-Path $containerHubModulesRoot 'WindowsBuild.Common.psm1'),
  (Join-Path $containerHubModulesRoot 'WindowsToolchain.Common.psm1'),
  (Join-Path $localModulesRoot 'Build.CMake.psm1'),
  (Join-Path $localModulesRoot 'Build.Formatting.psm1'),
  (Join-Path $localModulesRoot 'Build.Testing.psm1'),
  (Join-Path $localModulesRoot 'Build.Packaging.psm1')
)

foreach ($modulePath in $modulePaths) {
  if (-not (Test-Path $modulePath)) {
    throw "Required module not found: $modulePath"
  }
}

Import-Module (Join-Path $containerHubModulesRoot 'WindowsBuild.Common.psm1') -Force
Import-Module (Join-Path $containerHubModulesRoot 'WindowsToolchain.Common.psm1') -Force
Import-Module (Join-Path $containerHubModulesRoot 'WindowsScripts.Shared.psm1') -Force
Import-Module (Join-Path $localModulesRoot 'Build.CMake.psm1') -Force
Import-Module (Join-Path $localModulesRoot 'Build.Formatting.psm1') -Force
Import-Module (Join-Path $localModulesRoot 'Build.Testing.psm1') -Force
Import-Module (Join-Path $localModulesRoot 'Build.Packaging.psm1') -Force

$defaultConfigPath = Join-Path $PSScriptRoot 'Build-Windows.config.psd1'
$configPath = Get-OrDefault $env:BUILD_WINDOWS_CONFIG $defaultConfigPath
if (-not (Test-Path $configPath)) {
  throw "Build config not found: $configPath"
}
$config = Import-PowerShellDataFile -Path $configPath

$workspaceRootEnvVar = Get-OrDefault $env:WORKSPACE_ROOT_ENV (Get-ConfigValue -Config $config -Path 'Build.WorkspaceRootEnv')
$workspaceEnvItem = Get-Item -Path "Env:$workspaceRootEnvVar" -ErrorAction SilentlyContinue
$workspaceRootFromEnv = if ($null -ne $workspaceEnvItem) { $workspaceEnvItem.Value } else { $null }
$workspaceRoot = Get-OrDefault $workspaceRootFromEnv $repoRoot
$workspacePath = Resolve-WorkspacePath -Path $workspaceRoot

$logDir = Get-OrDefault $env:BUILD_LOG_DIR (Get-ConfigValue -Config $config -Path 'Build.LogDir')

$buildPathMsvc = Join-Path $workspacePath (Get-OrDefault $env:BUILD_DIR_MSVC (Get-ConfigValue -Config $config -Path 'Build.BuildDirMsvc'))
$buildPathClang = Join-Path $workspacePath (Get-OrDefault $env:BUILD_DIR_CLANGCL (Get-ConfigValue -Config $config -Path 'Build.BuildDirClangCl'))
$buildPathClangTsan = Join-Path $workspacePath (Get-OrDefault $env:BUILD_DIR_CLANGCL_TSAN (Get-ConfigValue -Config $config -Path 'Build.BuildDirClangClTsan'))
$buildPathProfile = Join-Path $workspacePath (Get-OrDefault $env:BUILD_DIR_PROFILE (Get-ConfigValue -Config $config -Path 'Build.BuildDirProfile'))
$buildPathRelease = Join-Path $workspacePath (Get-OrDefault $env:BUILD_DIR_RELEASE (Get-ConfigValue -Config $config -Path 'Build.BuildDirRelease'))

$presetMsvcDebug = Get-OrDefault $env:PRESET_MSVC_DEBUG (Get-ConfigValue -Config $config -Path 'Build.Presets.MsvcDebug')
$presetMsvcRelease = Get-OrDefault $env:PRESET_MSVC_RELEASE (Get-ConfigValue -Config $config -Path 'Build.Presets.MsvcRelease')
$presetClangDebug = Get-OrDefault $env:PRESET_CLANGCL_DEBUG (Get-ConfigValue -Config $config -Path 'Build.Presets.ClangClDebug')
$presetClangDebugTsan = Get-OrDefault $env:PRESET_CLANGCL_DEBUG_TSAN (Get-ConfigValue -Config $config -Path 'Build.Presets.ClangClDebugTsan')
$presetClangProfile = Get-OrDefault $env:CLANG_PROFILE_PRESET (Get-ConfigValue -Config $config -Path 'Build.Presets.ClangClProfile')
$presetClangRelease = Get-OrDefault $env:PRESET_CLANGCL_RELEASE (Get-ConfigValue -Config $config -Path 'Build.Presets.ClangClRelease')

$availableConfigurations = @('msvc-debug', 'msvc-release', 'clang-debug', 'clang-tsan', 'profile', 'clang-release')
$selectedConfigurations = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
if ($null -eq $Configurations -or $Configurations.Count -eq 0 -or ($Configurations -contains 'all')) {
  foreach ($configurationName in $availableConfigurations) {
    $selectedConfigurations.Add($configurationName) | Out-Null
  }
} else {
  foreach ($item in $Configurations) {
    if ([string]::IsNullOrWhiteSpace($item)) { continue }
    foreach ($configurationName in $item -split ',') {
      if ([string]::IsNullOrWhiteSpace($configurationName)) { continue }
      $normalized = $configurationName.Trim().ToLowerInvariant()
      if (-not ($availableConfigurations -contains $normalized)) {
        throw "Unknown configuration '$configurationName'. Supported values: all, $($availableConfigurations -join ', ')"
      }
      $selectedConfigurations.Add($normalized) | Out-Null
    }
  }
}

function Test-ConfigurationSelected {
  param([Parameter(Mandatory)][string]$Name)
  return $selectedConfigurations.Contains($Name)
}

if ($buildPathClangTsan -eq $buildPathClang) {
  $buildPathClangTsan = Join-Path $workspacePath 'build-clangcl-tsan'
}

function Invoke-ClangTidyFixStep {
  param(
    [Parameter(Mandatory)]
    [pscustomobject]$Context,
    [Parameter(Mandatory)]
    [string]$WorkspacePath,
    [Parameter(Mandatory)]
    [string]$BuildRoot
  )

  function Ensure-CompileCommandsDatabase {
    param(
      [Parameter(Mandatory)]
      [pscustomobject]$Context,
      [Parameter(Mandatory)]
      [string]$BuildRoot
    )

    $compileDb = Join-Path $BuildRoot 'compile_commands.json'
    if (Test-Path $compileDb) {
      return $compileDb
    }

    $buildNinja = Join-Path $BuildRoot 'build.ninja'
    if (-not (Test-Path $buildNinja)) {
      throw "compile_commands.json not found at: $compileDb"
    }

    $ninjaCommand = Get-Command 'ninja' -ErrorAction SilentlyContinue
    if (-not $ninjaCommand) {
      throw "compile_commands.json not found at: $compileDb"
    }

    Write-BuildLogWarning -Context $Context -Message 'compile_commands.json missing; generating it from ninja compdb.'

    $compdbOutput = & $ninjaCommand.Source '-C' $BuildRoot '-t' 'compdb' 2>&1
    if ($LASTEXITCODE -ne 0) {
      $compdbError = ($compdbOutput | Out-String).Trim()
      throw "Failed to generate compile_commands.json via ninja -t compdb: $compdbError"
    }

    Set-Content -Path $compileDb -Value $compdbOutput -Encoding utf8

    if (-not (Test-Path $compileDb)) {
      throw "compile_commands.json not found at: $compileDb"
    }

    return $compileDb
  }

  $clangTidyCommand = Get-Command 'clang-tidy' -ErrorAction SilentlyContinue
  if (-not $clangTidyCommand) {
    throw 'clang-tidy not found on PATH.'
  }

  $compileDb = Ensure-CompileCommandsDatabase -Context $Context -BuildRoot $BuildRoot

  $srcDir = Join-Path $WorkspacePath 'Src'
  $tidyFiles = @(Get-ChildItem -Path $srcDir -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -in @('.cpp', '.cc', '.cxx') } |
    Select-Object -ExpandProperty FullName)

  if ($tidyFiles.Count -eq 0) {
    Write-BuildLog -Context $Context -Message 'No C/C++ source files found under Src for clang-tidy.'
    return
  }

  foreach ($tidyFile in $tidyFiles) {
    Invoke-BuildExternal -Context $Context -File $clangTidyCommand.Source -Parameters @(
      '-p', $BuildRoot,
      '--checks=-misc-include-cleaner',
      '--fix',
      $tidyFile
    ) | Out-Null
  }
}

$context = New-BuildContext -Workspace $workspacePath -LogDir $logDir -StopOnError

try {
  Open-BuildLog -Context $context

  Write-BuildLog -Context $context -Message "Workspace: $workspacePath"
  Write-BuildLog -Context $context -Message "Configurations=$(([string[]]$selectedConfigurations -join ', '))"

  Invoke-BuildStep -Context $context -StepName 'Tool versions' -Critical -Script {
    Invoke-ToolchainChecks -Context $context -ToolArguments @{
      'cmake' = @('--version')
      'ninja' = @('--version')
    } -RequiredTools @('cmake', 'ninja') -FailOnMissingRequiredTools
  } | Out-Null

  if (-not $SkipFormat) {
    Invoke-BuildStep -Context $context -StepName 'Python tooling + cmake-format' -Critical -Script {
      Invoke-CmakeFormatStep -Context $context -WorkspacePath $workspacePath
    } | Out-Null

    Invoke-BuildStep -Context $context -StepName 'clang-format (C/C++)' -Critical -Script {
      Invoke-ClangFormatStep -Context $context -WorkspacePath $workspacePath
    } | Out-Null
  }

  if (Test-ConfigurationSelected -Name 'msvc-debug') {
    Invoke-BuildStep -Context $context -StepName "Configure/Build: $presetMsvcDebug" -Critical -Script {
      Invoke-CmakeConfigureAndBuild -Context $context -BuildPath $buildPathMsvc -Preset $presetMsvcDebug -Configuration 'Debug' -CleanBuildRoot
    } | Out-Null

    Invoke-BuildStep -Context $context -StepName 'Test: MSVC Debug' -Critical -Script {
      $excludeRegex = @()
      if ($DisableIntegrationTestsMsvcDebug) {
        Write-BuildLogWarning -Context $context -Message 'MSVC Debug integration tests disabled via -DisableIntegrationTestsMsvcDebug.'
        $excludeRegex += '^Integration\.'
      }

      Invoke-CtestDiscoveredTests -Context $context -BuildRoot $buildPathMsvc -Configuration 'Debug' -ExcludeRegex $excludeRegex -RuntimeFlavor 'Msvc'
    } | Out-Null
  }

  if (Test-ConfigurationSelected -Name 'msvc-release') {
    Invoke-BuildStep -Context $context -StepName "Configure/Build: $presetMsvcRelease" -Critical -Script {
      Invoke-CmakeConfigureAndBuild -Context $context -BuildPath $buildPathMsvc -Preset $presetMsvcRelease -Configuration 'Release' -CleanBuildRoot
    } | Out-Null
  }

  if (Test-ConfigurationSelected -Name 'clang-debug') {
    Invoke-BuildStep -Context $context -StepName "Configure/Build: $presetClangDebug" -Critical -Script {
      Invoke-CmakeConfigureAndBuild -Context $context -BuildPath $buildPathClang -Preset $presetClangDebug -Configuration 'Debug' -CleanBuildRoot
    } | Out-Null

    if (-not $SkipTidy) {
      Invoke-BuildStep -Context $context -StepName 'clang-tidy --fix (Src)' -Critical -Script {
        Invoke-ClangTidyFixStep -Context $context -WorkspacePath $workspacePath -BuildRoot $buildPathClang
      } | Out-Null
    }

    Invoke-BuildStep -Context $context -StepName 'Test: Clang Debug' -Critical -Script {
      Invoke-CtestDiscoveredTests -Context $context -BuildRoot $buildPathClang -Configuration 'Debug' -RuntimeFlavor 'Clang'
    } | Out-Null
  }

  if (Test-ConfigurationSelected -Name 'clang-tsan') {
    Invoke-BuildOptional -Context $context -Name 'ClangCL-TSan (optional)' -Script {
      $clangClCommand = Get-Command 'clang-cl.exe' -ErrorAction SilentlyContinue
      if (-not $clangClCommand) {
        throw 'clang-cl.exe not found on PATH. Install LLVM/Visual Studio Clang tools and run from a Developer PowerShell.'
      }

      Invoke-BuildExternal -Context $context -File $clangClCommand.Source -Parameters @('--version') | Out-Null

      if (-not (Test-ClangClThreadSanitizerSupport -ClangClPath $clangClCommand.Source)) {
        throw 'clang-cl ThreadSanitizer is not supported for target x86_64-pc-windows-msvc in this toolchain. Skipping optional TSan build/test.'
      }

      Invoke-CmakeConfigureAndBuild -Context $context -BuildPath $buildPathClangTsan -Preset $presetClangDebugTsan -Configuration 'Debug' -CleanBuildRoot

      Invoke-CtestDiscoveredTests -Context $context -BuildRoot $buildPathClangTsan -Configuration 'Debug' -RuntimeFlavor 'Clang'
    }
  }

  if (Test-ConfigurationSelected -Name 'profile') {
    Invoke-BuildStep -Context $context -StepName "Configure/Build: $presetClangProfile" -Critical -Script {
      Invoke-CmakeConfigureAndBuild -Context $context -BuildPath $buildPathProfile -Preset $presetClangProfile -Configuration 'RelWithDebInfo' -CleanBuildRoot
    } | Out-Null

    Invoke-BuildStep -Context $context -StepName 'Benchmarks' -Critical -Script {
      Push-Location $buildPathProfile
      try {
        $benchmarkExe = Resolve-TestExecutable -BuildRoot $buildPathProfile -ExecutableName 'perfTestSuite.exe'
        if (-not $benchmarkExe) {
          Write-BuildLog -Context $context -Message 'Benchmark executable not found. Skipping benchmark run.'
          return
        }

        Invoke-BuildExternal -Context $context -File $benchmarkExe -Parameters @(
          '--benchmark_out=results.json',
          '--benchmark_out_format=json'
        ) | Out-Null
      } finally {
        Pop-Location
      }
    } | Out-Null
  }

  if (Test-ConfigurationSelected -Name 'clang-release') {
    Invoke-BuildStep -Context $context -StepName "Release build/package: $presetClangRelease" -Critical -Script {
      Invoke-CmakeConfigureAndBuild -Context $context -BuildPath $buildPathRelease -Preset $presetClangRelease -Configuration 'Release' -CleanBuildRoot

      Invoke-BuildExternal -Context $context -File 'cmake' -Parameters @(
        '--build', $buildPathRelease,
        '--target', 'package',
        '--config', 'Release'
      ) | Out-Null
    } | Out-Null
  }

  if ((Test-ConfigurationSelected -Name 'clang-release') -and (-not $SkipMsix)) {
    Invoke-BuildOptional -Context $context -Name 'MSIX packaging' -Script {
      $makeappxPath = Resolve-WindowsSdkToolPath -ToolName 'makeappx.exe' -OverridePath $null
      if (-not $makeappxPath) {
        throw 'makeappx.exe not found. Install Windows SDK or add it to PATH.'
      }

      $msixName = Get-OrDefault $env:MSIX_PACKAGE_NAME (Get-ConfigValue -Config $config -Path 'Msix.PackageNameDefault')
      $msixPublisher = Get-OrDefault $env:MSIX_PUBLISHER (Get-ConfigValue -Config $config -Path 'Msix.Publisher')
      $msixVersion = Get-OrDefault $env:MSIX_VERSION (Get-ConfigValue -Config $config -Path 'Msix.Version')
      $msixMinVersion = Get-OrDefault $env:MSIX_MIN_VERSION (Get-ConfigValue -Config $config -Path 'Msix.MinVersion')

      $msixStaging = Join-Path $buildPathRelease 'msix-staging'
      $assetsDir = Join-Path $msixStaging 'Assets'
      if (Test-Path $msixStaging) {
        Remove-BuildRoot -Context $context -Path $msixStaging | Out-Null
      }

      Invoke-BuildExternal -Context $context -File 'cmake' -Parameters @(
        '--install', $buildPathRelease,
        '--config', 'Release',
        '--prefix', $msixStaging
      ) | Out-Null

      Resolve-DirectoryPath -Path $assetsDir | Out-Null

      $manifestTemplateRel = Get-ConfigValue -Config $config -Path 'Msix.ManifestTemplate'
      $manifestTemplatePath = if ([System.IO.Path]::IsPathRooted($manifestTemplateRel)) { $manifestTemplateRel } else { Join-Path $workspacePath $manifestTemplateRel }
      if (-not (Test-Path $manifestTemplatePath)) {
        throw "MSIX manifest template not found: $manifestTemplatePath"
      }

      $exeRelPath = "bin/$msixName.exe"
      $template = Get-Content -Path $manifestTemplatePath -Raw -Encoding UTF8
      $manifestXml = Expand-XmlTemplateTokens -Template $template -TokenMap @{
        '__MSIX_NAME__' = $msixName
        '__MSIX_PUBLISHER__' = $msixPublisher
        '__MSIX_VERSION__' = $msixVersion
        '__MSIX_MIN_VERSION__' = $msixMinVersion
        '__EXE_REL_PATH__' = $exeRelPath
        '__STORE_LOGO_REL__' = 'Assets/StoreLogo.png'
        '__LOGO150_REL__' = 'Assets/Square150x150Logo.png'
        '__LOGO44_REL__' = 'Assets/Square44x44Logo.png'
      }

      Set-Content -Path (Join-Path $msixStaging 'AppxManifest.xml') -Value $manifestXml -Encoding UTF8
      New-TransparentPng -Path (Join-Path $msixStaging 'Assets\StoreLogo.png') -Width 50 -Height 50
      New-TransparentPng -Path (Join-Path $msixStaging 'Assets\Square150x150Logo.png') -Width 150 -Height 150
      New-TransparentPng -Path (Join-Path $msixStaging 'Assets\Square44x44Logo.png') -Width 44 -Height 44

      if (-not (Test-Path (Join-Path $msixStaging $exeRelPath))) {
        throw "Expected executable not found in MSIX staging: $exeRelPath"
      }

      $msixOutPath = Join-Path $buildPathRelease "$msixName.msix"
      Invoke-BuildExternal -Context $context -File $makeappxPath -Parameters @('pack', '/d', $msixStaging, '/p', $msixOutPath, '/o') | Out-Null
    }
  }

  Write-BuildLogSuccess -Context $context -Message 'Windows build orchestration completed.'
} finally {
  Write-BuildSummary -Context $context
  Close-BuildLog -Context $context
}

if ($context.Results.Failed.Count -gt 0) {
  throw "Windows build completed with failures ($($context.Results.Failed.Count) steps failed)."
}
