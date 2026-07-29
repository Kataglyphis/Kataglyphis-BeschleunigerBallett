#requires -Version 7.0
<#
.SYNOPSIS
Resolves the source path of a Windows build PowerShell module.

.DESCRIPTION
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
    # ContainerHub copy is always preferred (all modules now require PS 7.0).
    # Vendored fallback in Scripts/Windows/modules is used only when the
    # upstream copy does not exist.
    $candidates = @(
        (Join-Path $repoRoot "ExternalLib\Kataglyphis-ContainerHub\windows\scripts\modules\$Name.psm1"),
        (Join-Path $scriptsWindowsRoot "modules\$Name.psm1")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
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

    # Re-expose WindowsScripts.Shared after the batch: ContainerHub's
    # WindowsBuild.Common internally imports it with -Force, which can
    # shadow the global session's copy. Re-importing it last ensures
    # its exports (Resolve-WorkspacePath, Resolve-NormalizedPath, etc.)
    # are visible to the calling script.
    if ($Name -notcontains 'WindowsScripts.Shared') { return }
    Import-Module (Resolve-BuildModulePath -Name 'WindowsScripts.Shared') -Force -Global
}

