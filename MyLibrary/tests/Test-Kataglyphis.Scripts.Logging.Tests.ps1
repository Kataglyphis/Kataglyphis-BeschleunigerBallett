Describe 'Kataglyphis.Scripts.Logging basic behavior' {
    It 'Can create log context and write to file' {
        $testDir = Join-Path $env:TEMP ('kat_log_test_' + [guid]::NewGuid().ToString())
        $workspace = $testDir
        $logDir = 'logs'
        New-Item -ItemType Directory -Path $testDir -Force | Out-Null

        Import-Module -Name (Join-Path $PSScriptRoot '..\modules\Kataglyphis.Scripts.Logging\Kataglyphis.Scripts.Logging.psm1') -Force
        $ctx = New-LogContext -Workspace $workspace -LogDir $logDir -LogFilePrefix 'test'
        Open-LogWriter -Context $ctx
        Write-ContextLog -Context $ctx -Message 'hello' -Level 'Info'
        Close-LogWriter -Context $ctx

        Test-Path $ctx.LogPath | Should -BeTrue
        (Get-Content $ctx.LogPath) -join "`n" | Should -Match 'hello'

        # cleanup
        Remove-Item -Recurse -Force $testDir
    }
}
