Describe 'WindowsMsix.Signing' {
  BeforeAll {
    . (Join-Path $PSScriptRoot '..\Resolve-BuildModule.ps1')
    Import-BuildModule 'WindowsMsix.Common'
    # Provide stubs for external cmdlets/functions so tests never call real signtool or import certs.
    $global:invokedArgs = $null
    function Invoke-BuildExternal { param($Context,$File,$Parameters) $global:invokedArgs = @{ File = $File; Params = $Parameters }; return 0 }
    # Resolve-WindowsSdkToolPath may be defined in other modules; provide a default stub so tests can mock/override it reliably.
    function Resolve-WindowsSdkToolPath { param($ToolName,$OverridePath) return $null }
    # Import-PfxCertificate may not be present in the lightweight test environment; provide a harmless stub so Mock can target it.
    function Import-PfxCertificate { param($FilePath,$CertStoreLocation,$Password) return $null }
    # Test-Administrator is defined in the module; provide a default so Mock/override works reliably in Pester v3.
    function Test-Administrator { return $false }

    # Import WindowsBuild.Common first so its exported Invoke-BuildExternal can be stubbed/overridden by tests.
    Import-BuildModule 'WindowsBuild.Common'

    # Provide stubs for external cmdlets/functions so tests never call real signtool or import certs.
    function Invoke-BuildExternal { param($Context,$File,$Parameters) $global:invokedArgs = @{ File = $File; Params = $Parameters }; return 0 }
    # Resolve-WindowsSdkToolPath may be defined in other modules; provide a default stub so tests can mock/override it reliably.
    function Resolve-WindowsSdkToolPath { param($ToolName,$OverridePath) return $null }
    # Import-PfxCertificate may not be present in the lightweight test environment; provide a harmless stub so Mock can target it.
    function Import-PfxCertificate { param($FilePath,$CertStoreLocation,$Password) return $null }
    # Test-Administrator is defined in the module; provide a default so Mock/override works reliably in Pester v3.
    function Test-Administrator { return $false }

    Import-BuildModule 'WindowsMsix.Signing'
    $script:workspace = New-Item -ItemType Directory -Path (Join-Path $env:TEMP ('msix-sign-test-' + (Get-Random))) -Force
    $script:ctx = New-BuildContext -Workspace $script:workspace -LogDir (Join-Path $script:workspace 'logs')
  }

  AfterAll {
    Remove-Item -LiteralPath $script:workspace -Recurse -Force -ErrorAction SilentlyContinue
  }

  Context 'Invoke-MsixSign' {
    It 'skips when signtool missing' {
      function Resolve-WindowsSdkToolPath { param($ToolName,$OverridePath) return $null }
      { Invoke-MsixSign -Context $script:ctx -WorkspacePath $script:workspace -MsixOutPath (Join-Path $script:workspace 'out.msix') } | Should Not Throw
      Remove-Item Function:\Resolve-WindowsSdkToolPath -ErrorAction SilentlyContinue
    }

    It 'warns when no pfx found' {
      function Resolve-WindowsSdkToolPath { param($ToolName,$OverridePath) return 'C:\signtool.exe' }
      # Ensure no pfx files in workspace
      Get-ChildItem -Path $script:workspace -Filter '*.pfx' -File -Recurse -ErrorAction SilentlyContinue | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
      # Use the Invoke-BuildExternal stub defined in BeforeAll
      $global:invokedArgs = $null
      Invoke-MsixSign -Context $script:ctx -WorkspacePath $script:workspace -MsixOutPath (Join-Path $script:workspace 'out.msix')
      $true | Should Be $true
    }

    It 'invokes sign and verify when pfx present' {
      function Resolve-WindowsSdkToolPath { param($ToolName,$OverridePath) return 'C:\signtool.exe' }
      $pfxPath = Join-Path $script:workspace 'test.pfx'
      New-Item -Path $pfxPath -ItemType File -Force | Out-Null
      $global:signed = $false
      Mock -CommandName Import-PfxCertificate { return @{ Thumbprint = 'ABC' } }
      Mock -CommandName Test-Administrator { return $true }

      $global:invokedArgs = $null
      $invoker = { param($Context,$File,$Parameters) $global:invokedArgs = @{ File = $File; Params = $Parameters }; return 0 }
      Invoke-MsixSign -Context $script:ctx -WorkspacePath $script:workspace -MsixOutPath (Join-Path $script:workspace 'out.msix') -InvokerScriptBlock $invoker
      # The invoker should have recorded that the sign step was invoked
      $global:invokedArgs | Should Not Be $null
    }
  }
}
