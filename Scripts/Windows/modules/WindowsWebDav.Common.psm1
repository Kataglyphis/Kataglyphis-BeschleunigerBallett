Set-StrictMode -Version Latest
#requires -Version 7.0


# Import shared helpers (Resolve-DirectoryPath, New-Timestamp, etc.)
# Prefer the ContainerHub copy of the shared helpers so the whole session
# uses ONE WindowsScripts.Shared module - a second path-distinct copy would
# knock the globally imported one out via this nested -Force import.
$sharedPath = Join-Path $PSScriptRoot '..\..\..\ExternalLib\Kataglyphis-ContainerHub\windows\scripts\modules\WindowsScripts.Shared.psm1'
# No -Force when already loaded: a nested force-reimport moves the module's
# exports out of the global session state on Windows PowerShell 5.1.
if (-not (Get-Module -Name 'WindowsScripts.Shared')) { Import-Module $sharedPath }

function Invoke-EarlyWebDavDownload {
  param(
    [Parameter(Mandatory)]
    [pscustomobject]$Context,
    [Parameter(Mandatory)]
    [string]$WorkspacePath,
    [Parameter(Mandatory)]
    [string]$WebDavHost,
    [Parameter(Mandatory)]
    [string]$WebDavUser,
    [Parameter(Mandatory)]
    [string]$WebDavPass,
    [Parameter(Mandatory)]
    [string]$WebDavRemote,
    [Parameter(Mandatory)]
    [string]$WebDavLocal
  )

  $earlyScript = Join-Path $WorkspacePath 'Scripts\download_pfx_files.py'
  Write-BuildLog -Context $Context -Message "DEBUG: Early WebDAV script path (raw): $earlyScript"

  if (-not (Test-Path $earlyScript)) {
    Write-BuildLogWarning -Context $Context -Message "Early WebDAV script not found: $earlyScript"
    return
  }

  # Prefer explicit 'uv' on PATH and invoke the script with 'uv run'.
  $uvCmd = Get-Command 'uv' -ErrorAction SilentlyContinue
  if (-not $uvCmd) {
    Write-BuildLogWarning -Context $Context -Message 'uv not found on PATH; cannot run early WebDAV script.'
    return
  }

  $venvPath = Join-Path $WorkspacePath '.venv'
  Write-BuildLog -Context $Context -Message "DEBUG: Ensuring uv venv at: $venvPath (activation: .venv\Scripts\Activate)"

  try {
    # If the venv already exists, prefer to keep it. However, if it's broken
    # (missing the python interpreter), remove and recreate it to ensure pip
    # commands succeed. This preserves the original intent of avoiding
    # unnecessary recreation but handles corrupted venvs gracefully.
    if (Test-Path $venvPath) {
      $pythonExe = Join-Path $venvPath 'Scripts\python.exe'
      $venvHealthy = $true

      if (-not (Test-Path $pythonExe)) {
        Write-BuildLogWarning -Context $Context -Message "DEBUG: venv exists but python interpreter missing at: $pythonExe."
        $venvHealthy = $false
      } else {
        try {
          $info = Get-Item -LiteralPath $pythonExe -ErrorAction Stop
          if ($info.Length -eq 0) {
            Write-BuildLogWarning -Context $Context -Message "DEBUG: python.exe exists but has zero length: $pythonExe"
            $venvHealthy = $false
          } else {
            # Try to run python --version to ensure the interpreter is runnable
            $proc = Start-Process -FilePath $pythonExe -ArgumentList '--version' -NoNewWindow -PassThru -Wait -ErrorAction Stop
            if ($proc.ExitCode -ne 0) {
              Write-BuildLogWarning -Context $Context -Message "DEBUG: python.exe returned non-zero exit code when checked: $($proc.ExitCode)"
              $venvHealthy = $false
            }
          }
        } catch {
          Write-BuildLogWarning -Context $Context -Message "DEBUG: Failed to run python.exe for health check: $($_.Exception.Message)"
          $venvHealthy = $false
        }
      }

      if (-not $venvHealthy) {
        Write-BuildLogWarning -Context $Context -Message "DEBUG: Recreating broken venv at: $venvPath"
        try {
          Remove-Item -LiteralPath $venvPath -Recurse -Force -ErrorAction Stop
        } catch {
          Write-BuildLogWarning -Context $Context -Message "Failed to remove broken venv: $($_.Exception.Message)"
        }
        Invoke-BuildExternal -Context $Context -File $uvCmd.Source -Parameters @('venv', $venvPath) -IgnoreExitCode | Out-Null
      } else {
        Write-BuildLog -Context $Context -Message "DEBUG: venv already exists at: $venvPath; skipping creation."
      }
    } else {
      Invoke-BuildExternal -Context $Context -File $uvCmd.Source -Parameters @('venv', $venvPath) -IgnoreExitCode | Out-Null
    }
  } catch {
    Write-BuildLogWarning -Context $Context -Message "uv venv creation failed: $($_.Exception.Message)"
  }

  try {
    Write-BuildLog -Context $Context -Message "DEBUG: Running: $($uvCmd.Source) pip install --upgrade pip"
    Invoke-BuildExternal -Context $Context -File $uvCmd.Source -Parameters @('pip', 'install', '--upgrade', 'pip') -IgnoreExitCode | Out-Null

    Write-BuildLog -Context $Context -Message "DEBUG: Installing Kataglyphis WebDAV client into uv venv: git+https://github.com/Kataglyphis/Kataglyphis-WebDavClient"
    Invoke-BuildExternal -Context $Context -File $uvCmd.Source -Parameters @('pip', 'install', 'git+https://github.com/Kataglyphis/Kataglyphis-WebDavClient') -IgnoreExitCode | Out-Null
  } catch {
    Write-BuildLogWarning -Context $Context -Message "uv pip install step failed: $($_.Exception.Message)"
  }

  Write-BuildLog -Context $Context -Message "DEBUG: Invoking early WebDAV download with: $($uvCmd.Source) run $earlyScript $WebDavHost $WebDavUser <redacted> $WebDavRemote $WebDavLocal"
  Invoke-BuildExternal -Context $Context -File $uvCmd.Source -Parameters @('run', $earlyScript, $WebDavHost, $WebDavUser, $WebDavPass, $WebDavRemote, $WebDavLocal) -IgnoreExitCode
}

Export-ModuleMember -Function @(
  'Invoke-EarlyWebDavDownload'
)

