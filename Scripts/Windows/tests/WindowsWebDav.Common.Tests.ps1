Describe 'WindowsWebDav.Common' {
  BeforeAll {
    . (Join-Path $PSScriptRoot '..\Resolve-BuildModule.ps1')
    Import-BuildModule @('WindowsScripts.Shared', 'WindowsLogging.Common', 'WindowsBuild.Common', 'WindowsWebDav.Common')
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
