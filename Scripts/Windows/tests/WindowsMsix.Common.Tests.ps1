Describe 'WindowsMsix.Common' {
  BeforeAll {
    . (Join-Path $PSScriptRoot '..\Resolve-BuildModule.ps1')
    Import-BuildModule 'WindowsMsix.Common'
    $script:tmp = New-Item -ItemType Directory -Path (Join-Path $env:TEMP ('msix-test-' + (Get-Random))) -Force
  }

  AfterAll {
    Remove-Item -LiteralPath $script:tmp -Recurse -Force -ErrorAction SilentlyContinue
  }

  Context 'Expand-XmlTemplateTokens' {
    It 'escapes and replaces tokens' {
      $template = '<root>__TOKEN__</root>'
      $expanded = Expand-XmlTemplateTokens -Template $template -TokenMap @{ '__TOKEN__' = 'A & B <C>' }
      $expanded | Should Be '<root>A &amp; B &lt;C&gt;</root>'
    }
  }

  Context 'Resolve-WindowsSdkToolPath' {
    It 'returns $null when tool not found' {
      Mock -CommandName Get-Command { return $null }
      $res = Resolve-WindowsSdkToolPath -ToolName 'nonexistent.exe' -OverridePath $null
      $res | Should Be $null
    }
  }

  Context 'New-TransparentPng' {
    It 'creates a PNG file' {
      $out = Join-Path $script:tmp 't.png'
      New-TransparentPng -Path $out -Width 16 -Height 16
      Test-Path $out | Should Be $true
    }
  }
}
