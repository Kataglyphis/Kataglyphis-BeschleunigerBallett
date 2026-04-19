param(
  [string[]]$Configurations = @('all'),
  [switch]$SkipFormat,
  [switch]$SkipTidy,
  [switch]$SkipTests,
  [switch]$SkipPerfTests,
  [switch]$SkipMsix,
  [switch]$SkipBuild,
  [switch]$VerboseBuild,
  [int]$ParallelJobs = 0,
  [switch]$DisableSccache,
  [switch]$DisableIntegrationTestsMsvcDebug,
  # WebDAV: prefer explicit script parameters (these match CLI flags), fall back to env vars
  [string]$WebDavHostname,
  [string]$WebDavUsername,
  [string]$WebDavPassword,
  [string]$RemoteBasePath,
  [string]$LocalAssetsFolder
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Config helpers moved to WindowsConfig.Common.psm1

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$containerHubModulesRoot = Join-Path $repoRoot 'ExternalLib\Kataglyphis-ContainerHub\windows\scripts\modules'
$localModulesRoot = Join-Path $PSScriptRoot 'modules'

# Import ContainerHub modules (includes shared helpers and logging)
Import-Module (Join-Path $containerHubModulesRoot 'WindowsScripts.Shared.psm1') -Force
Import-Module (Join-Path $containerHubModulesRoot 'WindowsLogging.Common.psm1') -Force
Import-Module (Join-Path $containerHubModulesRoot 'WindowsBuild.Common.psm1') -Force
Import-Module (Join-Path $containerHubModulesRoot 'WindowsToolchain.Common.psm1') -Force
Import-Module (Join-Path $containerHubModulesRoot 'WindowsUv.Common.psm1') -Force
Import-Module (Join-Path $containerHubModulesRoot 'WindowsCodeQL.Common.psm1') -Force
Import-Module (Join-Path $containerHubModulesRoot 'WindowsConfig.Common.psm1') -Force
Import-Module (Join-Path $containerHubModulesRoot 'WindowsClang.Common.psm1') -Force
Import-Module (Join-Path $containerHubModulesRoot 'WindowsWebDav.Common.psm1') -Force
Import-Module (Join-Path $containerHubModulesRoot 'WindowsMsix.Common.psm1') -Force
Import-Module (Join-Path $containerHubModulesRoot 'WindowsMsix.Signing.psm1') -Force
Import-Module (Join-Path $containerHubModulesRoot 'WindowsCMake.Common.psm1') -Force
Import-Module (Join-Path $containerHubModulesRoot 'WindowsFormatting.Common.psm1') -Force
Import-Module (Join-Path $containerHubModulesRoot 'WindowsTesting.Common.psm1') -Force

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
$presetClangFuzz = Get-OrDefault $env:PRESET_CLANGCL_FUZZ (Get-ConfigValue -Config $config -Path 'Build.Presets.ClangClFuzz')
$presetClangProfile = Get-OrDefault $env:CLANG_PROFILE_PRESET (Get-ConfigValue -Config $config -Path 'Build.Presets.ClangClProfile')
$presetClangRelease = Get-OrDefault $env:PRESET_CLANGCL_RELEASE (Get-ConfigValue -Config $config -Path 'Build.Presets.ClangClRelease')

$availableConfigurations = @('msvc-debug', 'msvc-release', 'clang-debug', 'clang-tsan', 'clang-profile', 'clang-release')
$selectedConfigurations = Get-SelectedConfigurations -Configurations $Configurations -AvailableConfigurations $availableConfigurations

# If SkipBuild is requested, clear any selected build configurations so
# configuration-specific configure/build steps are not executed. This keeps
# non-build steps (formatting, tidy, etc.) running. However, packaging and
# signing (which run as part of the clang-release step) are often desired
# even when skipping build. If the Release build output directory already
# exists we preserve 'clang-release' so packaging/signing still run.
if ($SkipBuild) {
  $selectedConfigurations.Clear()
}

function Test-ConfigurationSelected {
  param([Parameter(Mandatory)][string]$Name)
  return $selectedConfigurations.Contains($Name)
}

if ($buildPathClangTsan -eq $buildPathClang) {
  $buildPathClangTsan = Join-Path $workspacePath 'build-clangcl-tsan'
}

$context = New-BuildContext -Workspace $workspacePath -LogDir $logDir -StopOnError

# Ensure no running instance of the application or crash handler is blocking the build output
Stop-Process -Name "GraphicsEngine", "WerFault" -Force -ErrorAction SilentlyContinue

try {
  Open-BuildLog -Context $context
  # If WebDAV parameters are supplied via environment variables, attempt an
  # early download of .pfx files before other build steps. This is optional
  # and will not fail the orchestration if it errors.
  try {
    # Prefer explicit script parameters passed on the command-line, fall back to
    # environment variables for CI compatibility.
    $webdavHost = if (-not [string]::IsNullOrWhiteSpace($WebDavHostname)) { $WebDavHostname } else { $env:WEB_DAV_HOSTNAME }
    $webdavUser = if (-not [string]::IsNullOrWhiteSpace($WebDavUsername)) { $WebDavUsername } else { $env:WEB_DAV_USERNAME }
    $webdavPass = if (-not [string]::IsNullOrWhiteSpace($WebDavPassword)) { $WebDavPassword } else { $env:WEB_DAV_PASSWORD }
    $webdavRemote = if (-not [string]::IsNullOrWhiteSpace($RemoteBasePath)) { $RemoteBasePath } else { $env:WEB_DAV_REMOTE_BASE_PATH }
    $webdavLocal = if (-not [string]::IsNullOrWhiteSpace($LocalAssetsFolder)) { $LocalAssetsFolder } else { if ($env:WEB_DAV_LOCAL_BASE_PATH) { $env:WEB_DAV_LOCAL_BASE_PATH } else { $workspacePath } }

    if (-not [string]::IsNullOrWhiteSpace($webdavHost) -and -not [string]::IsNullOrWhiteSpace($webdavUser) -and -not [string]::IsNullOrWhiteSpace($webdavPass) -and -not [string]::IsNullOrWhiteSpace($webdavRemote)) {
      Invoke-BuildOptional -Context $context -Name 'Early WebDAV .pfx download' -Script {
        Invoke-EarlyWebDavDownload -Context $context -WorkspacePath $workspacePath -WebDavHost $webdavHost -WebDavUser $webdavUser -WebDavPass $webdavPass -WebDavRemote $webdavRemote -WebDavLocal $webdavLocal
      }
    } else {
      Write-BuildLog -Context $context -Message 'DEBUG: Early WebDAV step skipped: missing WebDAV parameters.'
    }
  } catch {
    Write-BuildLogWarning -Context $context -Message "Early WebDAV invocation failed: $($_.Exception.Message)"
  }
  if ($SkipBuild) {
    Write-BuildLogWarning -Context $context -Message 'Skipping all configure/build steps due to -SkipBuild.'
  }
  Write-BuildLog -Context $context -Message "Workspace: $workspacePath"
  Write-BuildLog -Context $context -Message "Configurations=$(([string[]]$selectedConfigurations -join ', '))"
  Write-BuildLog -Context $context -Message "DEBUG: ParallelJobs=$ParallelJobs (0=all cores)"
  Write-BuildLog -Context $context -Message "DEBUG: VerboseBuild=$VerboseBuild"
  Write-BuildLog -Context $context -Message "DEBUG: DisableSccache=$DisableSccache"
  Write-BuildLog -Context $context -Message "DEBUG: System ProcessorCount=$([Environment]::ProcessorCount)"

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
    # Make MSVC debug configure/build optional so failures here don't fail the whole orchestration
    Invoke-BuildOptional -Context $context -Name "Configure/Build: $presetMsvcDebug (MSVC Debug - optional)" -Script {
      Invoke-CmakeConfigureAndBuild -Context $context -BuildPath $buildPathMsvc -Preset $presetMsvcDebug -Configuration 'Debug' -CleanBuildRoot -ParallelJobs $ParallelJobs -VerboseOutput:$VerboseBuild -DisableSccache:$DisableSccache
    }

    if (-not $SkipTests) {
      # Make MSVC debug tests optional as well
      Invoke-BuildOptional -Context $context -Name 'Test: MSVC Debug (optional)' -Script {
        $excludeRegex = @()
        if ($DisableIntegrationTestsMsvcDebug) {
          Write-BuildLogWarning -Context $context -Message 'MSVC Debug integration tests disabled via -DisableIntegrationTestsMsvcDebug.'
          $excludeRegex += '^Integration\.'
        }

        Invoke-CtestDiscoveredTests -Context $context -BuildRoot $buildPathMsvc -Configuration 'Debug' -ExcludeRegex $excludeRegex -RuntimeFlavor 'Msvc'
      }
    }
  }

  if (Test-ConfigurationSelected -Name 'msvc-release') {
    # Make MSVC release configure/build optional so failures here don't fail the whole orchestration
    Invoke-BuildOptional -Context $context -Name "Configure/Build: $presetMsvcRelease (MSVC Release - optional)" -Script {
      # Precompile shaders for release builds to avoid runtime glslc dependency
      $compileShadersScript = Join-Path $PSScriptRoot 'compile-shaders.ps1'
      if (Test-Path $compileShadersScript) {
        # Derive TargetEnv from VULKAN_VERSION if available, else fallback to vulkan1.4
        $targetEnv = 'vulkan1.4'
        if ($env:VULKAN_VERSION) {
          if ($env:VULKAN_VERSION -match '^([0-9]+)\.([0-9]+)') { $targetEnv = "vulkan$($matches[1]).$($matches[2])" }
        }
        Write-BuildLog -Context $context -Message "Precompiling shaders (MSVC Release) -> targetEnv=$targetEnv"
        & $compileShadersScript -TargetEnv $targetEnv
      } else {
        Write-BuildLogWarning -Context $context -Message "Shader compile script not found: $compileShadersScript"
      }
      Invoke-CmakeConfigureAndBuild -Context $context -BuildPath $buildPathMsvc -Preset $presetMsvcRelease -Configuration 'Release' -CleanBuildRoot -ParallelJobs $ParallelJobs -VerboseOutput:$VerboseBuild -DisableSccache:$DisableSccache
    }
  }

  if (Test-ConfigurationSelected -Name 'clang-debug') {
    Invoke-BuildStep -Context $context -StepName "Configure/Build: $presetClangDebug" -Critical -Script {
      Invoke-CmakeConfigureAndBuild -Context $context -BuildPath $buildPathClang -Preset $presetClangDebug -Configuration 'Debug' -CleanBuildRoot -ParallelJobs $ParallelJobs -VerboseOutput:$VerboseBuild -DisableSccache:$DisableSccache
    } | Out-Null

    if (-not $SkipTidy) {
      Invoke-BuildStep -Context $context -StepName 'clang-tidy --fix (Src)' -Critical -Script {
        Invoke-ClangTidyFixStep -Context $context -WorkspacePath $workspacePath -BuildRoot $buildPathClang
      } | Out-Null
    }

    if (-not $SkipTests) {
      Invoke-BuildStep -Context $context -StepName 'Test: Clang Debug' -Critical -Script {
        Invoke-CtestDiscoveredTests -Context $context -BuildRoot $buildPathClang -Configuration 'Debug' -RuntimeFlavor 'Clang'
      } | Out-Null
    }
  }

  if (Test-ConfigurationSelected -Name 'clang-tsan') {
    Invoke-BuildStep -Context $context -StepName 'Configure/Build: ClangCL-TSan' -Critical -Script {
      $clangClCommand = Get-Command 'clang-cl.exe' -ErrorAction SilentlyContinue
      if (-not $clangClCommand) {
        throw 'clang-cl.exe not found on PATH. Install LLVM/Visual Studio Clang tools and run from a Developer PowerShell.'
      }

      Invoke-BuildExternal -Context $context -File $clangClCommand.Source -Parameters @('--version') | Out-Null

      Invoke-CmakeConfigureAndBuild -Context $context -BuildPath $buildPathClangTsan -Preset $presetClangDebugTsan -Configuration 'Debug' -CleanBuildRoot -ParallelJobs $ParallelJobs -VerboseOutput:$VerboseBuild -DisableSccache:$DisableSccache

      if (-not $SkipTests) {
        Invoke-CtestDiscoveredTests -Context $context -BuildRoot $buildPathClangTsan -Configuration 'Debug' -RuntimeFlavor 'Clang'
      }
    } | Out-Null
  }

  if (Test-ConfigurationSelected -Name 'clang-fuzz') {
    Invoke-BuildStep -Context $context -StepName "Configure/Build: $presetClangFuzz" -Critical -Script {
      Invoke-CmakeConfigureAndBuild -Context $context -BuildPath $buildPathClangFuzz -Preset $presetClangFuzz -Configuration 'Debug' -CleanBuildRoot -ParallelJobs $ParallelJobs -VerboseOutput:$VerboseBuild -DisableSccache:$DisableSccache

      if (-not $SkipTests) {
        Invoke-CtestDiscoveredTests -Context $context -BuildRoot $buildPathClangFuzz -Configuration 'Debug' -RuntimeFlavor 'Clang'
      }
    } | Out-Null
  }

  if (Test-ConfigurationSelected -Name 'clang-profile') {
    Invoke-BuildStep -Context $context -StepName "Configure/Build: $presetClangProfile" -Critical -Script {
      Invoke-CmakeConfigureAndBuild -Context $context -BuildPath $buildPathProfile -Preset $presetClangProfile -Configuration 'RelWithDebInfo' -CleanBuildRoot -ParallelJobs $ParallelJobs -VerboseOutput:$VerboseBuild -DisableSccache:$DisableSccache
    } | Out-Null

    if (-not $SkipPerfTests) {
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
  }

  if (Test-ConfigurationSelected -Name 'clang-release') {
    Invoke-BuildStep -Context $context -StepName "Release build/package: $presetClangRelease" -Critical -Script {
      if ($SkipBuild) {
        # When -SkipBuild is requested, skip configure/build but still run
        # the packaging step (assumes a previous Release build exists).
        Write-BuildLog -Context $context -Message 'Skipping Clang Release build due to -SkipBuild.'
      } else {
        # Precompile shaders for release builds to avoid runtime glslc dependency
        $compileShadersScript = Join-Path $PSScriptRoot 'compile-shaders.ps1'
        if (Test-Path $compileShadersScript) {
          # Derive TargetEnv from VULKAN_VERSION if available, else fallback to vulkan1.4
          $targetEnv = 'vulkan1.4'
          if ($env:VULKAN_VERSION) {
            if ($env:VULKAN_VERSION -match '^([0-9]+)\.([0-9]+)') { $targetEnv = "vulkan$($matches[1]).$($matches[2])" }
          }
          Write-BuildLog -Context $context -Message "Precompiling shaders (Clang Release) -> targetEnv=$targetEnv"
          & $compileShadersScript -TargetEnv $targetEnv
        } else {
          Write-BuildLogWarning -Context $context -Message "Shader compile script not found: $compileShadersScript"
        }
        Invoke-CmakeConfigureAndBuild -Context $context -BuildPath $buildPathRelease -Preset $presetClangRelease -Configuration 'Release' -CleanBuildRoot -ParallelJobs $ParallelJobs -VerboseOutput:$VerboseBuild -DisableSccache:$DisableSccache
      }

      # Always attempt packaging when clang-release is selected; packaging
      # should not be skipped by -SkipBuild.
      $packageArgs = @(
        '--build', $buildPathRelease,
        '--target', 'package',
        '--config', 'Release'
      )
      if ($ParallelJobs -gt 0) {
        $packageArgs += @('--parallel', $ParallelJobs.ToString())
        Write-BuildLog -Context $context -Message "DEBUG: Package build using --parallel $ParallelJobs"
      } else {
        $packageArgs += @('--parallel')
        Write-BuildLog -Context $context -Message "DEBUG: Package build using --parallel (all cores)"
      }
      Write-BuildLog -Context $context -Message "DEBUG: Package command: cmake $($packageArgs -join ' ')"
      Invoke-BuildExternal -Context $context -File 'cmake' -Parameters $packageArgs | Out-Null
    } | Out-Null
  }

  if (-not $SkipMsix) {
    Invoke-BuildOptional -Context $context -Name 'MSIX packaging' -Script {
      $makeappxPath = Resolve-WindowsSdkToolPath -ToolName 'makeappx.exe' -OverridePath $null
      if (-not $makeappxPath) {
        throw 'makeappx.exe not found. Install Windows SDK or add it to PATH.'
      }

      $msixName = Get-OrDefault $env:MSIX_PACKAGE_NAME (Get-ConfigValue -Config $config -Path 'Msix.PackageNameDefault')
      $msixPublisher = Get-OrDefault $env:MSIX_PUBLISHER (Get-ConfigValue -Config $config -Path 'Msix.Publisher')
      
      $versionFile = Join-Path $workspacePath 'version.txt'
      if (Test-Path $versionFile) {
        $msixVersion = (Get-Content -Path $versionFile).Trim() + '.0'
      } else {
        $msixVersion = Get-OrDefault $env:MSIX_VERSION (Get-ConfigValue -Config $config -Path 'Msix.Version')
      }
      
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

      # Attempt to sign the generated MSIX using the signing helper module.
      Invoke-MsixSign -Context $context -WorkspacePath $workspacePath -MsixOutPath $msixOutPath
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
