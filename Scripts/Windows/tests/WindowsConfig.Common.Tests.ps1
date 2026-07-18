Describe 'WindowsConfig.Common' {

  BeforeAll {
    . (Join-Path $PSScriptRoot '..\Resolve-BuildModule.ps1')
    $modulePath = Resolve-BuildModulePath -Name 'WindowsConfig.Common'
    Import-Module $modulePath -Force
  }

  Context 'Get-ConfigValue' {
    It 'returns nested value when path exists' {
      $cfg = @{ Build = @{ WorkspaceRootEnv = 'WORKSPACE' } }
      Get-ConfigValue -Config $cfg -Path 'Build.WorkspaceRootEnv' | Should Be 'WORKSPACE'
    }

    It 'returns $null for missing path' {
      $cfg = @{ A = @{ B = 1 } }
      Get-ConfigValue -Config $cfg -Path 'A.C' | Should BeNullOrEmpty
    }
  }

  Context 'Parse-Configurations' {
    It 'parses "all" into all available configs' {
      $avail = @('a','b','c')
      $set = Get-SelectedConfigurations -Configurations @('all') -AvailableConfigurations $avail
      $set.Contains('a') | Should Be $true
      $set.Contains('b') | Should Be $true
      $set.Contains('c') | Should Be $true
    }

    It 'parses comma-separated values and normalizes' {
      $avail = @('msvc-debug','clangcl-release')
      $set = Get-SelectedConfigurations -Configurations @('MSVC-DEBUG, clangcl-release') -AvailableConfigurations $avail
      $set.Contains('msvc-debug') | Should Be $true
      $set.Contains('clangcl-release') | Should Be $true
    }

    It 'throws on unknown config' {
      $avail = @('x')
      # Explicit try/catch instead of 'Should Throw': Pester 3.4.0 (in-box
      # Windows version) fails to observe the exception from this module call.
      $threw = $false
      try { Get-SelectedConfigurations -Configurations @('unknown') -AvailableConfigurations $avail | Out-Null } catch { $threw = $true }
      $threw | Should Be $true
    }
  }
}
