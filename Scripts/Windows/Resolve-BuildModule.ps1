<#
.SYNOPSIS
Resolves the source path of a Windows build PowerShell module.

.DESCRIPTION
#requires -Version 7.0

Reusable modules live upstream in the ContainerHub submodule
(ExternalLib/Kataglyphis-ContainerHub/windows/scripts/modules) and are preferred
whenever present, so improvements land upstream and are shared across projects.
Modules that ContainerHub's module refactor (commit b391a1d) removed are vendored
in Scripts/Windows/modules as a fallback. Dot-source this file, then call
Resolve-BuildModulePath 'WindowsBuild.Common' (module name without extension).
#>

function Resolve-BuildModulePath {
    param(
        [Parameter(Mandatory)]
        [string]$Name
    )

    $scriptsWindowsRoot = $PSScriptRoot
    if ([string]::IsNullOrWhiteSpace($scriptsWindowsRoot)) {
        throw 'Resolve-BuildModulePath must be dot-sourced from a script file.'
    }

    $repoRoot = (Resolve-Path (Join-Path $scriptsWindowsRoot '..\..')).Path
    $candidates = @(
        @{ Path = Join-Path $repoRoot "ExternalLib\Kataglyphis-ContainerHub\windows\scripts\modules\$Name.psm1"; PS7 = $true },
        @{ Path = Join-Path $scriptsWindowsRoot "modules\$Name.psm1"; PS7 = $false }
    )

    # Under PowerShell 5.1 prefer vendored fallbacks (ContainerHub modules
    # require PS 7.0; importing them would fail). Under PS 7+ prefer the
    # upstream ContainerHub copy.
    $isPs5 = ($PSVersionTable.PSVersion.Major -eq 5)
    $preferred = if ($isPs5) { $false } else { $true }
    $ordered = @($candidates | Where-Object { $_.PS7 -eq $preferred }) +
               @($candidates | Where-Object { $_.PS7 -ne $preferred })
    foreach ($candidate in $ordered) {
        if (Test-Path $candidate.Path) { return (Resolve-Path $candidate.Path).Path }
    }

    throw ("Build module '$Name' found neither in the ContainerHub submodule nor in the vendored " +
        "fallback (Scripts/Windows/modules). Checked: $($candidates -join '; ')")
}

function Import-BuildModule {
    param(
        [Parameter(Mandatory)]
        [string[]]$Name
    )

    foreach ($moduleName in $Name) {
        Import-Module (Resolve-BuildModulePath -Name $moduleName) -Force -Global
    }

    # Windows PowerShell 5.1: a module's own nested 'Import-Module ... -Force'
    # of WindowsScripts.Shared pulls its exports out of the global session
    # state. Re-expose it after the batch so script-level callers keep access.
    if ($Name -notcontains 'WindowsScripts.Shared') { return }
    Import-Module (Resolve-BuildModulePath -Name 'WindowsScripts.Shared') -Force -Global
}

