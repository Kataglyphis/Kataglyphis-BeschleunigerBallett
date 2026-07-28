Set-StrictMode -Version Latest
#requires -Version 7.0


# Import shared helpers (Resolve-DirectoryPath, New-Timestamp, etc.)
# Prefer the ContainerHub copy of the shared helpers so the whole session
# uses ONE WindowsScripts.Shared module - a second path-distinct copy would
# knock the globally imported one out via this nested -Force import.
$sharedPath = Join-Path $PSScriptRoot '..\..\..\ExternalLib\Kataglyphis-ContainerHub\windows\scripts\modules\WindowsScripts.Shared.psm1'
if (-not (Test-Path $sharedPath)) { $sharedPath = Join-Path $PSScriptRoot 'WindowsScripts.Shared.psm1' }
# No -Force when already loaded: a nested force-reimport moves the module's
# exports out of the global session state on Windows PowerShell 5.1.
if (-not (Get-Module -Name 'WindowsScripts.Shared')) { Import-Module $sharedPath }

function New-LogContext {
    param(
        [Parameter(Mandatory)]
        [string]$Workspace,
        [Parameter(Mandatory)]
        [string]$LogDir,
        [string]$LogFilePrefix = 'session'
    )

    $effectiveLogDir = if ([System.IO.Path]::IsPathRooted($LogDir)) { $LogDir } else { Join-Path $Workspace $LogDir }
    $logDirPath = Resolve-DirectoryPath -Path $effectiveLogDir
    $timestamp = New-Timestamp -Format 'yyyyMMdd-HHmmss'
    $logPath = Join-Path $logDirPath "$LogFilePrefix-$timestamp.log"

    [pscustomobject]@{
        Workspace = $Workspace
        LogPath   = $logPath
        StartedAt = (Get-Date).ToString('o')
        LogWriter = $null
    }
}

function Open-LogWriter {
    param(
        [Parameter(Mandatory)]
        [pscustomobject]$Context
    )

    $parentDir = Split-Path -Parent $Context.LogPath
    if ($parentDir) {
        Resolve-DirectoryPath -Path $parentDir | Out-Null
    }

    $fileStream = New-Object System.IO.FileStream(
        $Context.LogPath,
        [System.IO.FileMode]::Append,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::ReadWrite
    )

    $writer = New-Object System.IO.StreamWriter($fileStream, [System.Text.Encoding]::UTF8)
    $writer.AutoFlush = $true
    $Context.LogWriter = $writer
}

function Close-LogWriter {
    param(
        [Parameter(Mandatory)]
        [pscustomobject]$Context
    )

    if ($Context.LogWriter) {
        try {
            $Context.LogWriter.Flush()
            $Context.LogWriter.Dispose()
        } catch {
        } finally {
            $Context.LogWriter = $null
        }
    }
}

function Write-ContextLog {
    param(
        [Parameter(Mandatory)]
        [pscustomobject]$Context,
        [Parameter(Mandatory)]
        [AllowEmptyString()]
        [string]$Message,
        [ValidateSet('Info', 'Warning', 'Error', 'Success')]
        [string]$Level = 'Info'
    )

    $suppressConsoleOutput = $false
    if ($null -ne $Context.PSObject.Properties['SuppressConsoleOutput']) {
        $suppressConsoleOutput = [bool]$Context.SuppressConsoleOutput
    }

    if (-not $Message) {
        if (-not $suppressConsoleOutput) {
            Write-Host ''
        }
        if ($Context.LogWriter) {
            $Context.LogWriter.WriteLine('')
        }
        return
    }

    if (-not $suppressConsoleOutput) {
        switch ($Level) {
            'Warning' {
                Write-Warning $Message
            }
            'Error' {
                Write-Host $Message -ForegroundColor Red
            }
            'Success' {
                Write-Host $Message -ForegroundColor Green
            }
            default {
                Write-Host $Message
            }
        }
    }

    if ($Context.LogWriter) {
        $timestamp = Get-Date -Format 'HH:mm:ss'
        $prefix = switch ($Level) {
            'Warning' { 'WARNING: ' }
            'Error' { 'ERROR: ' }
            'Success' { 'SUCCESS: ' }
            default { '' }
        }

        $Context.LogWriter.WriteLine("[$timestamp] $prefix$Message")
    }
}

Export-ModuleMember -Function @(
    'New-LogContext',
    'Open-LogWriter',
    'Close-LogWriter',
    'Write-ContextLog'
)

