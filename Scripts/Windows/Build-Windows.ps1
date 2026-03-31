param(
  [string[]]$Configurations = @('all'),
  [switch]$SkipFormat,
  [switch]$SkipTidy,
  [switch]$SkipTests,
  [switch]$SkipPerfTests,
  [switch]$SkipMsix,
  [switch]$SkipBuild,
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
  (Join-Path $containerHubModulesRoot 'WindowsUv.Common.psm1'),
  (Join-Path $containerHubModulesRoot 'WindowsCodeQL.Common.psm1'),
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
Import-Module (Join-Path $containerHubModulesRoot 'WindowsUv.Common.psm1') -Force
Import-Module (Join-Path $containerHubModulesRoot 'WindowsCodeQL.Common.psm1') -Force
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
      $earlyScript = Join-Path $workspacePath 'Scripts\download_pfx_files.py'
      Write-BuildLog -Context $context -Message "DEBUG: Early WebDAV script path (raw): $earlyScript"

      Invoke-BuildOptional -Context $context -Name 'Early WebDAV .pfx download' -Script {
        if (-not (Test-Path $earlyScript)) {
          Write-BuildLogWarning -Context $context -Message "Early WebDAV script not found: $earlyScript"
          return
        }

        # Prefer explicit 'uv' on PATH and invoke the script with 'uv run'.
        # We intentionally avoid calling a naked python executable.
        $uvCmd = Get-Command 'uv' -ErrorAction SilentlyContinue
        if (-not $uvCmd) {
          Write-BuildLogWarning -Context $context -Message 'uv not found on PATH; cannot run early WebDAV script.'
          return
        }

        # Ensure a uv-managed venv exists (.venv in workspace) and install the WebDAV client into it.
        $venvPath = Join-Path $workspacePath '.venv'
        Write-BuildLog -Context $context -Message "DEBUG: Ensuring uv venv at: $venvPath (activation: .venv\Scripts\Activate)"

        # Create/ensure the venv. Non-fatal; if this fails we continue to the download attempt.
        try {
          Invoke-BuildExternal -Context $context -File $uvCmd.Source -Parameters @('venv', $venvPath) -IgnoreExitCode | Out-Null
        } catch {
          Write-BuildLogWarning -Context $context -Message "uv venv creation failed: $($_.Exception.Message)"
        }

        # Upgrade pip and install the Kataglyphis WebDAV client from the repository.
        try {
          Write-BuildLog -Context $context -Message "DEBUG: Running: $($uvCmd.Source) pip install --upgrade pip"
          Invoke-BuildExternal -Context $context -File $uvCmd.Source -Parameters @('pip', 'install', '--upgrade', 'pip') -IgnoreExitCode | Out-Null

          Write-BuildLog -Context $context -Message "DEBUG: Installing Kataglyphis WebDAV client into uv venv: git+https://github.com/Kataglyphis/Kataglyphis-WebDavClient"
          Invoke-BuildExternal -Context $context -File $uvCmd.Source -Parameters @('pip', 'install', 'git+https://github.com/Kataglyphis/Kataglyphis-WebDavClient') -IgnoreExitCode | Out-Null
        } catch {
          Write-BuildLogWarning -Context $context -Message "uv pip install step failed: $($_.Exception.Message)"
        }

        # Invoke the downloader via 'uv run'. Redact the password in the logged command line.
        Write-BuildLog -Context $context -Message "DEBUG: Invoking early WebDAV download with: $($uvCmd.Source) run $earlyScript $webdavHost $webdavUser <redacted> $webdavRemote $webdavLocal"
        Invoke-BuildExternal -Context $context -File $uvCmd.Source -Parameters @('run', $earlyScript, $webdavHost, $webdavUser, $webdavPass, $webdavRemote, $webdavLocal) -IgnoreExitCode
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
      Invoke-CmakeConfigureAndBuild -Context $context -BuildPath $buildPathMsvc -Preset $presetMsvcDebug -Configuration 'Debug' -CleanBuildRoot
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
      Invoke-CmakeConfigureAndBuild -Context $context -BuildPath $buildPathMsvc -Preset $presetMsvcRelease -Configuration 'Release' -CleanBuildRoot
    }
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

    if (-not $SkipTests) {
      Invoke-BuildStep -Context $context -StepName 'Test: Clang Debug' -Critical -Script {
        Invoke-CtestDiscoveredTests -Context $context -BuildRoot $buildPathClang -Configuration 'Debug' -RuntimeFlavor 'Clang'
      } | Out-Null
    }
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

      if (-not $SkipTests) {
        Invoke-CtestDiscoveredTests -Context $context -BuildRoot $buildPathClangTsan -Configuration 'Debug' -RuntimeFlavor 'Clang'
      }
    }
  }

  if (Test-ConfigurationSelected -Name 'profile') {
    Invoke-BuildStep -Context $context -StepName "Configure/Build: $presetClangProfile" -Critical -Script {
      Invoke-CmakeConfigureAndBuild -Context $context -BuildPath $buildPathProfile -Preset $presetClangProfile -Configuration 'RelWithDebInfo' -CleanBuildRoot
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
        Invoke-CmakeConfigureAndBuild -Context $context -BuildPath $buildPathRelease -Preset $presetClangRelease -Configuration 'Release' -CleanBuildRoot
      }

      # Always attempt packaging when clang-release is selected; packaging
      # should not be skipped by -SkipBuild.
      Invoke-BuildExternal -Context $context -File 'cmake' -Parameters @(
        '--build', $buildPathRelease,
        '--target', 'package',
        '--config', 'Release'
      ) | Out-Null
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

      # Attempt to sign the generated MSIX using a .pfx located at the repository root.
      # The signing password may be provided via the MSIX_PFX_PASSWORD environment variable.
      try {
        $signtoolPath = Resolve-WindowsSdkToolPath -ToolName 'signtool.exe' -OverridePath $null
        if ([string]::IsNullOrWhiteSpace($signtoolPath)) {
          Write-BuildLogWarning -Context $context -Message 'signtool.exe not found. Skipping MSIX signing.'
        } else {
          # Look for an explicit .pfx in the workspace root (non-recursive).
          $pfxFiles = Get-ChildItem -Path $workspacePath -Filter '*.pfx' -File -ErrorAction SilentlyContinue
          $pfxFiles = @($pfxFiles)
          if (($null -ne $pfxFiles) -and ($pfxFiles.Count -gt 0)) {
            $pfx = $pfxFiles[0].FullName
            Write-BuildLog -Context $context -Message "Found PFX for signing: $($pfxFiles[0].Name)"

            # Prefer MSIX_PFX_PASSWORD, fall back to MSIX_CERT_PASSWORD for CI compatibility
            $pfxPassword = Get-OrDefault $env:MSIX_PFX_PASSWORD $env:MSIX_CERT_PASSWORD
            $timestampUrl = Get-OrDefault $env:MSIX_TIMESTAMP_URL 'http://timestamp.digicert.com'

            $sigArgs = @('sign', '/fd', 'SHA256', '/f', $pfx)
            if (-not [string]::IsNullOrWhiteSpace($pfxPassword)) {
              $sigArgs += @('/p', $pfxPassword)
            } else {
              Write-BuildLogWarning -Context $context -Message 'MSIX_PFX_PASSWORD not set. Attempting to sign without password (PFX may be unprotected).' 
            }
            # Use RFC3161 timestamping with SHA256
            $sigArgs += @('/tr', $timestampUrl, '/td', 'SHA256', $msixOutPath)

            Write-BuildLog -Context $context -Message "Signing MSIX: $msixOutPath"
            Invoke-BuildExternal -Context $context -File $signtoolPath -Parameters $sigArgs | Out-Null

            # Import the signing certificate into the LocalMachine trust store so
            # subsequent signtool verify calls succeed when using a self-signed PFX.
            try {
              # Ensure we are elevated before attempting to write to LocalMachine stores.
              $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
              if (-not $isAdmin) {
                Write-BuildLogWarning -Context $context -Message 'Not running as Administrator; skipping PFX import into LocalMachine certificate store. signtool verify may fail.'
              } else {
                Write-BuildLog -Context $context -Message 'Importing PFX into Cert:\\LocalMachine\\Root to trust the signing chain for verification.'
                if (-not [string]::IsNullOrWhiteSpace($pfxPassword)) {
                  $securePassword = ConvertTo-SecureString -String $pfxPassword -AsPlainText -Force
                  $imported = Import-PfxCertificate -FilePath $pfx -CertStoreLocation 'Cert:\\LocalMachine\\Root' -Password $securePassword -ErrorAction Stop
                } else {
                  $imported = Import-PfxCertificate -FilePath $pfx -CertStoreLocation 'Cert:\\LocalMachine\\Root' -ErrorAction Stop
                }

                if ($imported -ne $null) {
                  # Import-PfxCertificate can return an array; log the thumbprints of imported certs.
                  $thumbprints = @()
                  if ($imported -is [System.Array]) { $thumbprints = $imported | ForEach-Object { $_.Thumbprint } }
                  else { $thumbprints = @($imported.Thumbprint) }
                  Write-BuildLog -Context $context -Message "Imported certificate(s) into LocalMachine\\Root: $([string]::Join(', ', $thumbprints))"
                }
              }
            } catch {
              Write-BuildLogWarning -Context $context -Message ("PFX import failed: $($_.Exception.Message)")
            }

            # Verify signature
            Write-BuildLog -Context $context -Message "Verifying MSIX signature: $msixOutPath"
            Invoke-BuildExternal -Context $context -File $signtoolPath -Parameters @('verify', '/pa', '/v', $msixOutPath) | Out-Null
            Write-BuildLog -Context $context -Message 'MSIX signing/verification completed.'
          } else {
            Write-BuildLogWarning -Context $context -Message 'No .pfx found at repository root; MSIX will not be signed.'
          }
        }
      } catch {
        Write-BuildLogWarning -Context $context -Message ("MSIX signing step failed: $($_.Exception.Message)")
      }
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
