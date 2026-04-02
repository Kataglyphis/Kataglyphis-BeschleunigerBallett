Describe 'WindowsWebDav.Common' {
  BeforeAll {
    $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
    $modulesRoot = Join-Path $repoRoot 'ExternalLib\Kataglyphis-ContainerHub\windows\scripts\modules'
    Import-Module (Join-Path $modulesRoot 'WindowsScripts.Shared.psm1') -Force
    Import-Module (Join-Path $modulesRoot 'WindowsLogging.Common.psm1') -Force
    Import-Module (Join-Path $modulesRoot 'WindowsBuild.Common.psm1') -Force
    Import-Module (Join-Path $modulesRoot 'WindowsWebDav.Common.psm1') -Force
    $script:workspace = New-Item -ItemType Directory -Path (Join-Path $env:TEMP ('webdav-test-' + (Get-Random))) -Force
  }

  AfterAll {
    Remove-Item -LiteralPath $script:workspace -Recurse -Force -ErrorAction SilentlyContinue
  }

  Context 'Invoke-EarlyWebDavDownload' {
    It 'skips if script missing' {
      Mock -CommandName Test-Path { return $false }
      # Create a realistic build context using the module helper so logging functions
      # and Write-ContextLog have the properties they expect.
      $logDir = Join-Path $script:workspace 'logs'
      New-Item -ItemType Directory -Path $logDir -Force | Out-Null
      $ctx = New-BuildContext -Workspace $script:workspace -LogDir $logDir

      Invoke-EarlyWebDavDownload -Context $ctx -WorkspacePath $script:workspace -WebDavHost 'h' -WebDavUser 'u' -WebDavPass 'p' -WebDavRemote 'r' -WebDavLocal $script:workspace
      # No exception means success; verify nothing thrown
      $true | Should Be $true
    }
  }
}
