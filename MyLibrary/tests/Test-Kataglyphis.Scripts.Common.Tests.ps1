Describe 'Kataglyphis.Scripts.Common basic behavior' {
    It 'Resolve-DirectoryPath creates and returns an absolute path' {
        $tempDir = Join-Path -Path $env:TEMP -ChildPath ('kat_test_' + [guid]::NewGuid().ToString())
        if (Test-Path $tempDir) { Remove-Item -Recurse -Force $tempDir }

        Import-Module -Name (Join-Path $PSScriptRoot '..\modules\Kataglyphis.Scripts.Common\Kataglyphis.Scripts.Common.psm1') -Force
        $resolved = Resolve-DirectoryPath -Path $tempDir
        Test-Path $resolved | Should -BeTrue
        $resolved | Should -Be ([System.IO.Path]::GetFullPath($tempDir))

        # cleanup
        if (Test-Path $tempDir) { Remove-Item -Recurse -Force $tempDir }
    }

    It 'ConvertTo-ParameterList returns flat array for arrays and scalars' {
        Import-Module -Name (Join-Path $PSScriptRoot '..\modules\Kataglyphis.Scripts.Common\Kataglyphis.Scripts.Common.psm1') -Force
        $a = ConvertTo-ParameterList -Value @('one','two')
        $a | Should -BeOfType System.Object[]
        $a.Count | Should -Be 2

        $s = ConvertTo-ParameterList -Value 'single'
        $s | Should -BeOfType System.Object[]
        $s.Count | Should -Be 1
    }
}
