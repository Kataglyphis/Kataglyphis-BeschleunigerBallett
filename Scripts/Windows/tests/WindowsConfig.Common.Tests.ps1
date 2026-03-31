Describe 'WindowsConfig.Common' {

  BeforeAll {
    $modulePath = Resolve-Path -Path (Join-Path $PSScriptRoot '..\..\..\ExternalLib\Kataglyphis-ContainerHub\windows\scripts\modules\WindowsConfig.Common.psm1')
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
      $avail = @('msvc-debug','clang-release')
      $set = Get-SelectedConfigurations -Configurations @('MSVC-DEBUG, clang-release') -AvailableConfigurations $avail
      $set.Contains('msvc-debug') | Should Be $true
      $set.Contains('clang-release') | Should Be $true
    }

    It 'throws on unknown config' {
      $avail = @('x')
      { Get-SelectedConfigurations -Configurations @('unknown') -AvailableConfigurations $avail } | Should Throw
    }
  }
}
