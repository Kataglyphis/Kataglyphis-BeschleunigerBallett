Set-StrictMode -Version Latest

# Import shared helpers (Resolve-DirectoryPath, New-Timestamp, etc.)
# Prefer the ContainerHub copy of the shared helpers so the whole session
# uses ONE WindowsScripts.Shared module - a second path-distinct copy would
# knock the globally imported one out via this nested -Force import.
$sharedPath = Join-Path $PSScriptRoot '..\..\..\ExternalLib\Kataglyphis-ContainerHub\windows\scripts\modules\WindowsScripts.Shared.psm1'
if (-not (Test-Path $sharedPath)) { $sharedPath = Join-Path $PSScriptRoot 'WindowsScripts.Shared.psm1' }
# No -Force when already loaded: a nested force-reimport moves the module's
# exports out of the global session state on Windows PowerShell 5.1.
if (-not (Get-Module -Name 'WindowsScripts.Shared')) { Import-Module $sharedPath }

function Test-Administrator {
  try {
    return ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
  } catch {
    return $false
  }
}

function Invoke-MsixSign {
  param(
    [Parameter(Mandatory)] [pscustomobject]$Context,
    [Parameter(Mandatory)] [string]$WorkspacePath,
    [Parameter(Mandatory)] [string]$MsixOutPath,
    # Optional invoker scriptblock for testability. If provided, it will be called instead of Invoke-BuildExternal.
    [scriptblock]$InvokerScriptBlock
  )

  try {
    $signtoolPath = Resolve-WindowsSdkToolPath -ToolName 'signtool.exe' -OverridePath $null
    if ([string]::IsNullOrWhiteSpace($signtoolPath)) {
      Write-BuildLogWarning -Context $Context -Message 'signtool.exe not found. Skipping MSIX signing.'
      return
    }

    # Look for an explicit .pfx in the workspace root (non-recursive).
    $pfxFiles = Get-ChildItem -Path $WorkspacePath -Filter '*.pfx' -File -ErrorAction SilentlyContinue
    $pfxFiles = @($pfxFiles)
    if (($null -ne $pfxFiles) -and ($pfxFiles.Count -gt 0)) {
      $pfx = $pfxFiles[0].FullName
      Write-BuildLog -Context $Context -Message "Found PFX for signing: $($pfxFiles[0].Name)"

      # Prefer MSIX_PFX_PASSWORD, fall back to MSIX_CERT_PASSWORD for CI compatibility
      $pfxPassword = Get-OrDefault $env:MSIX_PFX_PASSWORD $env:MSIX_CERT_PASSWORD
      $timestampUrl = Get-OrDefault $env:MSIX_TIMESTAMP_URL 'http://timestamp.digicert.com'

      $sigArgs = @('sign', '/fd', 'SHA256', '/f', $pfx)
      if (-not [string]::IsNullOrWhiteSpace($pfxPassword)) {
        $sigArgs += @('/p', $pfxPassword)
      } else {
        Write-BuildLogWarning -Context $Context -Message 'MSIX_PFX_PASSWORD not set. Attempting to sign without password (PFX may be unprotected).' 
      }
      # Use RFC3161 timestamping with SHA256
      $sigArgs += @('/tr', $timestampUrl, '/td', 'SHA256', $MsixOutPath)

      Write-BuildLog -Context $Context -Message "Signing MSIX: $MsixOutPath"
      if ($InvokerScriptBlock) {
        & $InvokerScriptBlock -Context $Context -File $signtoolPath -Parameters $sigArgs | Out-Null
      } else {
        Invoke-BuildExternal -Context $Context -File $signtoolPath -Parameters $sigArgs | Out-Null
      }

      # Import the signing certificate into the LocalMachine trust store so
      # subsequent signtool verify calls succeed when using a self-signed PFX.
      try {
        if (-not (Test-Administrator)) {
          Write-BuildLogWarning -Context $Context -Message 'Not running as Administrator; skipping PFX import into LocalMachine certificate store. signtool verify may fail.'
        } else {
          Write-BuildLog -Context $Context -Message 'Importing PFX into Cert:\\LocalMachine\\Root to trust the signing chain for verification.'
          if (-not [string]::IsNullOrWhiteSpace($pfxPassword)) {
            $securePassword = ConvertTo-SecureString -String $pfxPassword -AsPlainText -Force
            $imported = Import-PfxCertificate -FilePath $pfx -CertStoreLocation 'Cert:\\LocalMachine\\Root' -Password $securePassword -ErrorAction Stop
          } else {
            $imported = Import-PfxCertificate -FilePath $pfx -CertStoreLocation 'Cert:\\LocalMachine\\Root' -ErrorAction Stop
          }

          if ($imported -ne $null) {
            $thumbprints = @()
            if ($imported -is [System.Array]) { $thumbprints = $imported | ForEach-Object { $_.Thumbprint } }
            else { $thumbprints = @($imported.Thumbprint) }
            Write-BuildLog -Context $Context -Message "Imported certificate(s) into LocalMachine\\Root: $([string]::Join(', ', $thumbprints))"
          }
        }
      } catch {
        Write-BuildLogWarning -Context $Context -Message ("PFX import failed: $($_.Exception.Message)")
      }

      # Verify signature
      Write-BuildLog -Context $Context -Message "Verifying MSIX signature: $MsixOutPath"
      if ($InvokerScriptBlock) {
        & $InvokerScriptBlock -Context $Context -File $signtoolPath -Parameters @('verify', '/pa', '/v', $MsixOutPath) | Out-Null
      } else {
        Invoke-BuildExternal -Context $Context -File $signtoolPath -Parameters @('verify', '/pa', '/v', $MsixOutPath) | Out-Null
      }
      Write-BuildLog -Context $Context -Message 'MSIX signing/verification completed.'
    } else {
      Write-BuildLogWarning -Context $Context -Message 'No .pfx found at repository root; MSIX will not be signed.'
    }
  } catch {
    Write-BuildLogWarning -Context $Context -Message ("MSIX signing step failed: $($_.Exception.Message)")
  }
}

# Approved-verb wrapper for the signing flow. Keeps Invoke-MsixSign for compatibility.
function Start-MsixSigning {
  param(
    [Parameter(Mandatory)] [pscustomobject]$Context,
    [Parameter(Mandatory)] [string]$WorkspacePath,
    [Parameter(Mandatory)] [string]$MsixOutPath,
    [scriptblock]$InvokerScriptBlock
  )

  return Invoke-MsixSign -Context $Context -WorkspacePath $WorkspacePath -MsixOutPath $MsixOutPath -InvokerScriptBlock $InvokerScriptBlock
}

Export-ModuleMember -Function @(
  'Invoke-MsixSign',
  'Start-MsixSigning',
  'Test-Administrator'
)
