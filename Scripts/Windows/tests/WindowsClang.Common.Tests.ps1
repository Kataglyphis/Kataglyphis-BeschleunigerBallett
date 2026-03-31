Describe 'WindowsClang.Common' {
  BeforeAll {
    $modulePath = Resolve-Path -Path (Join-Path $PSScriptRoot '..\..\..\ExternalLib\Kataglyphis-ContainerHub\windows\scripts\modules\WindowsClang.Common.psm1')
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
      { Get-CompileCommandsDatabase -Context @{ } -BuildRoot $script:buildRoot } | Should Throw
    }

    It 'returns existing compile_commands.json when present' {
      $compilePath = Join-Path $script:buildRoot 'compile_commands.json'
      Set-Content -Path $compilePath -Value '[{"file":"a.cpp"}]' -Encoding utf8

      Get-CompileCommandsDatabase -Context @{ } -BuildRoot $script:buildRoot | Should Be $compilePath
    }
  }
}
