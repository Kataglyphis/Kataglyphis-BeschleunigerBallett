Describe 'Resolve-BuildModule' {
  BeforeAll {
    . (Join-Path $PSScriptRoot '..\Resolve-BuildModule.ps1')
    $script:repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
  }

  Context 'Resolve-BuildModulePath preference order' {
    It 'prefers the ContainerHub upstream copy for a module that lives there' {
      $resolved = Resolve-BuildModulePath -Name 'WindowsScripts.Shared'
      $expectedRoot = Join-Path $script:repoRoot 'ExternalLib\Kataglyphis-ContainerHub\windows\scripts\modules'
      $resolved | Should Be (Join-Path $expectedRoot 'WindowsScripts.Shared.psm1')
    }

    It 'falls back to the vendored copy for a module that was not upstreamed' {
      $resolved = Resolve-BuildModulePath -Name 'WindowsTesting.Common'
      $expectedRoot = Join-Path $script:repoRoot 'Scripts\Windows\modules'
      $resolved | Should Be (Join-Path $expectedRoot 'WindowsTesting.Common.psm1')
    }
  }

  Context 'Resolve-BuildModulePath failure mode' {
    It 'throws and names both searched locations when a module exists nowhere' {
      # Explicit try/catch instead of 'Should Throw': Pester 3.4.0 (in-box
      # Windows version) fails to observe the exception from this module call.
      $threw = $false
      $message = $null
      try {
        Resolve-BuildModulePath -Name 'NoSuchModule' | Out-Null
      } catch {
        $threw = $true
        $message = $_.Exception.Message
      }

      $threw | Should Be $true
      $message | Should Match ([regex]::Escape('ExternalLib\Kataglyphis-ContainerHub\windows\scripts\modules\NoSuchModule.psm1'))
      $message | Should Match ([regex]::Escape('Scripts\Windows\modules\NoSuchModule.psm1'))
    }
  }

  Context 'Vendored fallback directory contents' {
    It 'contains exactly the two modules AGENTS.md documents as vendored' {
      $vendoredDir = Join-Path $PSScriptRoot '..\modules'
      $actual = @(Get-ChildItem -Path $vendoredDir -Filter '*.psm1' | Select-Object -ExpandProperty Name | Sort-Object)
      $expected = @('WindowsClang.Common.psm1', 'WindowsTesting.Common.psm1')
      $actual | Should Be $expected
    }
  }

  Context 'Import-BuildModule repair guard' {
    It 'exposes WindowsScripts.Shared exports even when the caller does not name it' {
      # WindowsBuild.Common internally -Force-imports WindowsScripts.Shared and
      # can shadow the global session's copy; the repair must run regardless of
      # whether the caller listed WindowsScripts.Shared itself.
      Import-BuildModule @('WindowsBuild.Common')
      (Get-Command Resolve-WorkspacePath -ErrorAction SilentlyContinue) | Should Not Be $null
    }
  }
}
