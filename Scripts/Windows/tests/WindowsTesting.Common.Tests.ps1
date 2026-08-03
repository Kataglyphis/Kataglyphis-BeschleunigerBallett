Describe 'WindowsTesting.Common' {
  BeforeAll {
    . (Join-Path $PSScriptRoot '..\Resolve-BuildModule.ps1')
    $modulePath = Resolve-BuildModulePath -Name 'WindowsTesting.Common'
    Import-Module $modulePath -Force
  }

  Context 'Resolve-TestExecutable' {
    BeforeEach {
      $tmp = New-Item -ItemType Directory -Path (Join-Path $env:TEMP ('winTest-' + (Get-Random))) -Force
      $script:root = $tmp.FullName
    }

    AfterEach {
      Remove-Item -LiteralPath $script:root -Recurse -Force -ErrorAction SilentlyContinue
    }

    It 'finds the executable directly under BuildRoot' {
      Set-Content -Path (Join-Path $script:root 'a.exe') -Value 'x'
      Resolve-TestExecutable -BuildRoot $script:root -ExecutableName 'a.exe' | Should Be (Join-Path $script:root 'a.exe')
    }

    It 'finds the executable under the preferred Debug subpath' {
      $debugDir = Join-Path $script:root 'Debug'
      New-Item -ItemType Directory -Path $debugDir -Force | Out-Null
      Set-Content -Path (Join-Path $debugDir 'b.exe') -Value 'x'
      Resolve-TestExecutable -BuildRoot $script:root -ExecutableName 'b.exe' | Should Be (Join-Path $debugDir 'b.exe')
    }

    It 'finds the executable under the preferred Test\commit subpath' {
      $commitDir = Join-Path $script:root 'Test\commit'
      New-Item -ItemType Directory -Path $commitDir -Force | Out-Null
      Set-Content -Path (Join-Path $commitDir 'c.exe') -Value 'x'
      Resolve-TestExecutable -BuildRoot $script:root -ExecutableName 'c.exe' | Should Be (Join-Path $commitDir 'c.exe')
    }

    It 'falls back to a recursive search when the executable is outside every preferred path' {
      $nestedDir = Join-Path $script:root 'some\deeply\nested\path'
      New-Item -ItemType Directory -Path $nestedDir -Force | Out-Null
      Set-Content -Path (Join-Path $nestedDir 'd.exe') -Value 'x'
      Resolve-TestExecutable -BuildRoot $script:root -ExecutableName 'd.exe' | Should Be (Join-Path $nestedDir 'd.exe')
    }

    It 'returns $null when the executable does not exist anywhere' {
      Resolve-TestExecutable -BuildRoot $script:root -ExecutableName 'missing.exe' | Should Be $null
    }
  }

  Context 'Invoke-WithAsanOptions' {
    BeforeEach {
      Remove-Item Env:\ASAN_OPTIONS -ErrorAction SilentlyContinue
    }

    AfterEach {
      Remove-Item Env:\ASAN_OPTIONS -ErrorAction SilentlyContinue
    }

    It 'carries no trailing colon when ASAN_OPTIONS was unset' {
      $observed = Invoke-WithAsanOptions -Options 'log_path=logs/asan.log' -Script { $env:ASAN_OPTIONS }
      $observed | Should Be 'log_path=logs/asan.log'
    }

    It 'leaves ASAN_OPTIONS unset again after the script block completes' {
      Invoke-WithAsanOptions -Options 'log_path=logs/asan.log' -Script { } | Out-Null
      $env:ASAN_OPTIONS | Should Be $null
    }

    It 'prepends the new options ahead of a pre-existing value' {
      $env:ASAN_OPTIONS = 'existing_option=1'
      $observed = Invoke-WithAsanOptions -Options 'log_path=logs/asan.log' -Script { $env:ASAN_OPTIONS }
      $observed | Should Be 'log_path=logs/asan.log:existing_option=1'
    }

    It 'restores a pre-existing ASAN_OPTIONS value verbatim' {
      $env:ASAN_OPTIONS = 'existing_option=1'
      Invoke-WithAsanOptions -Options 'log_path=logs/asan.log' -Script { } | Out-Null
      $env:ASAN_OPTIONS | Should Be 'existing_option=1'
    }
  }

  Context 'Invoke-ManualTestExecutable returns a single boolean so the caller-side guard can fire' {
    BeforeEach {
      $tmp = New-Item -ItemType Directory -Path (Join-Path $env:TEMP ('winTest-' + (Get-Random))) -Force
      $script:root = $tmp.FullName
      Set-Content -Path (Join-Path $script:root 'x.exe') -Value 'x'
      function global:Invoke-BuildExternal { throw 'Process exited with exit code -1073741515' }
      function global:Write-BuildLogWarning { param($Context, $Message) }
    }

    AfterEach {
      Remove-Item function:global:Invoke-BuildExternal -ErrorAction SilentlyContinue
      Remove-Item function:global:Write-BuildLogWarning -ErrorAction SilentlyContinue
      Remove-Item -LiteralPath $script:root -Recurse -Force -ErrorAction SilentlyContinue
    }

    It 'returns a single boolean so the caller-side guard can fire' {
      $result = @(Invoke-ManualTestExecutable -Context ([pscustomobject]@{}) -BuildRoot $script:root -ExecutableName 'x.exe')
      $result.Count | Should Be 1
      $result[0] | Should Be $false
    }
  }

  Context 'Invoke-ManualTestExecutable success' {
    BeforeEach {
      $tmp = New-Item -ItemType Directory -Path (Join-Path $env:TEMP ('winTest-' + (Get-Random))) -Force
      $script:root = $tmp.FullName
      Set-Content -Path (Join-Path $script:root 'x.exe') -Value 'x'
      function global:Invoke-BuildExternal { }
      function global:Write-BuildLogWarning { param($Context, $Message) }
    }

    AfterEach {
      Remove-Item function:global:Invoke-BuildExternal -ErrorAction SilentlyContinue
      Remove-Item function:global:Write-BuildLogWarning -ErrorAction SilentlyContinue
      Remove-Item -LiteralPath $script:root -Recurse -Force -ErrorAction SilentlyContinue
    }

    It 'returns a single $true boolean' {
      $result = @(Invoke-ManualTestExecutable -Context ([pscustomobject]@{}) -BuildRoot $script:root -ExecutableName 'x.exe')
      $result.Count | Should Be 1
      $result[0] | Should Be $true
    }
  }

  Context 'Invoke-ManualTestExecutable other exceptions' {
    BeforeEach {
      $tmp = New-Item -ItemType Directory -Path (Join-Path $env:TEMP ('winTest-' + (Get-Random))) -Force
      $script:root = $tmp.FullName
      Set-Content -Path (Join-Path $script:root 'x.exe') -Value 'x'
      function global:Invoke-BuildExternal { throw 'Process exited with exit code 1' }
      function global:Write-BuildLogWarning { param($Context, $Message) }
    }

    AfterEach {
      Remove-Item function:global:Invoke-BuildExternal -ErrorAction SilentlyContinue
      Remove-Item function:global:Write-BuildLogWarning -ErrorAction SilentlyContinue
      Remove-Item -LiteralPath $script:root -Recurse -Force -ErrorAction SilentlyContinue
    }

    It 'still throws for a failure that is not the loader/runtime mismatch' {
      # Explicit try/catch instead of 'Should Throw': Pester 3.4.0 (in-box
      # Windows version) fails to observe the exception from this module call.
      $threw = $false
      try {
        Invoke-ManualTestExecutable -Context ([pscustomobject]@{}) -BuildRoot $script:root -ExecutableName 'x.exe' | Out-Null
      } catch {
        $threw = $true
      }
      $threw | Should Be $true
    }
  }
}
