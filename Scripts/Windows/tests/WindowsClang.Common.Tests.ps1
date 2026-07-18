Describe 'WindowsClang.Common' {
  BeforeAll {
    . (Join-Path $PSScriptRoot '..\Resolve-BuildModule.ps1')
    $modulePath = Resolve-BuildModulePath -Name 'WindowsClang.Common'
    Import-Module $modulePath -Force
    $tmp = New-Item -ItemType Directory -Path (Join-Path $env:TEMP ('test-build-' + (Get-Random))) -Force
    $script:buildRoot = $tmp.FullName
  }

  AfterAll {
    Remove-Item -LiteralPath $script:buildRoot -Recurse -Force -ErrorAction SilentlyContinue
  }

  Context 'Get-CompileCommandsDatabase' {
    It 'throws if build.ninja missing' {
      Mock -CommandName Test-Path { return $false } -ParameterFilter { $Path -like '*build.ninja' }
      # Explicit try/catch instead of 'Should Throw': Pester 3.4.0 (in-box
      # Windows version) fails to observe the exception from this module call.
      $threw = $false
      try { Get-CompileCommandsDatabase -Context @{ } -BuildRoot $script:buildRoot | Out-Null } catch { $threw = $true }
      $threw | Should Be $true
    }

    It 'returns existing compile_commands.json when present' {
      $compilePath = Join-Path $script:buildRoot 'compile_commands.json'
      Set-Content -Path $compilePath -Value '[{"file":"a.cpp"}]' -Encoding utf8

      Get-CompileCommandsDatabase -Context @{ } -BuildRoot $script:buildRoot | Should Be $compilePath
    }
  }
}
