Set-StrictMode -Version Latest

# Common helper functions for Windows build scripts
# These functions are shared across all WindowsXxx.Common.psm1 modules

<#
.SYNOPSIS
    Ensures a directory exists and returns its normalized path.
.DESCRIPTION
    Creates the directory if it does not exist and returns the fully resolved path.
.PARAMETER Path
    The path to ensure exists.
.OUTPUTS
    [string] The fully qualified path to the directory.
#>
function Resolve-DirectoryPath {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
    return (Resolve-Path $Path).Path
}

<#
.SYNOPSIS
    Creates a formatted timestamp string.
.DESCRIPTION
    Returns a timestamp using the specified format (default ISO 8601).
.PARAMETER Format
    A .NET DateTime format string.
.OUTPUTS
    [string] The formatted timestamp.
#>
function New-Timestamp {
    param(
        [string]$Format = 'yyyy-MM-ddTHH:mm:ss'
    )

    return (Get-Date).ToString($Format)
}

<#
.SYNOPSIS
    Creates a timestamped file path.
.DESCRIPTION
    Combines a directory, optional prefix, timestamp, and extension into a file path.
.PARAMETER Directory
    The target directory.
.PARAMETER Prefix
    Optional file name prefix.
.PARAMETER Extension
    File extension (default: .log).
.PARAMETER Format
    Timestamp format (default: yyyyMMdd-HHmmss).
.OUTPUTS
    [string] The full path to the timestamped file.
#>
function New-TimestampedFilePath {
    param(
        [Parameter(Mandatory)]
        [string]$Directory,
        [string]$Prefix = '',
        [string]$Extension = '.log',
        [string]$Format = 'yyyyMMdd-HHmmss'
    )

    $ts = New-Timestamp -Format $Format
    $name = if ($Prefix) { "$Prefix-$ts$Extension" } else { "$ts$Extension" }
    return Join-Path $Directory $name
}

<#
.SYNOPSIS
    Normalizes a file system path.
.DESCRIPTION
    Returns a fully qualified, normalized path without trailing slashes.
.PARAMETER Path
    The path to normalize.
.OUTPUTS
    [string] The normalized path.
#>
function Resolve-NormalizedPath {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    $resolved = [System.IO.Path]::GetFullPath($Path)
    return $resolved.TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
}

<#
.SYNOPSIS
    Converts a value to a list of command-line parameters.
.DESCRIPTION
    Transforms hashtables, arrays, or strings into an array of strings suitable for process arguments.
    Switch parameters (boolean $true) produce only the key, $false values are omitted.
.PARAMETER Value
    The value to convert (hashtable, array, string, or other).
.PARAMETER Prefix
    Parameter prefix (default: '-').
.OUTPUTS
    [string[]] Array of argument strings.
#>
function ConvertTo-ParameterList {
    param(
        [Parameter(Mandatory)]
        $Value,
        [string]$Prefix = '-'
    )

    if ($null -eq $Value) { return @() }

    # If it's already an array of strings, return as-is
    if ($Value -is [array] -and $Value.Count -gt 0 -and $Value[0] -is [string]) {
        return @($Value)
    }

    # If it's a single string, return as single-element array
    if ($Value -is [string]) {
        return @($Value)
    }

    # If it's a hashtable, convert key-value pairs
    if ($Value -is [hashtable]) {
        $result = @()
        foreach ($key in $Value.Keys) {
            $v = $Value[$key]
            if ($null -eq $v) { continue }

            if ($v -is [bool]) {
                if ($v) {
                    $result += "$Prefix$key"
                }
                # false booleans are omitted
            } elseif ($v -is [array]) {
                foreach ($item in $v) {
                    $result += "$Prefix$key"
                    $result += "$item"
                }
            } else {
                $result += "$Prefix$key"
                $result += "$v"
            }
        }
        return $result
    }

    # Fallback: convert to string
    return @("$Value")
}

<#
.SYNOPSIS
    Resolves and validates a workspace path.
.DESCRIPTION
    Returns a fully qualified, normalized workspace path.
    Throws if the path does not exist.
.PARAMETER Path
    The workspace path to resolve.
.OUTPUTS
    [string] The resolved workspace path.
#>
function Resolve-WorkspacePath {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    if (-not (Test-Path $Path)) {
        throw "Workspace path does not exist: $Path"
    }
    return (Resolve-Path $Path).Path
}

function Resolve-WindowsBuildRootCandidates {
    param(
        [Parameter(Mandatory = $true)]
        [string] $RepoRoot,

        [Parameter()]
        [string] $BuildRootDir = "",

        [Parameter()]
        [hashtable] $WindowsBuildConfig,

        [Parameter()]
        [switch] $IncludeDefaultFallbacks
    )

    $candidateInputs = [System.Collections.Generic.List[string]]::new()

    if (-not [string]::IsNullOrWhiteSpace($BuildRootDir)) {
        $candidateInputs.Add($BuildRootDir)
    }

    if ($null -ne $WindowsBuildConfig -and $WindowsBuildConfig.ContainsKey('BuildRootDir') -and -not [string]::IsNullOrWhiteSpace($WindowsBuildConfig.BuildRootDir)) {
        $candidateInputs.Add($WindowsBuildConfig.BuildRootDir)
    }

    if ($IncludeDefaultFallbacks) {
        $candidateInputs.Add('out')
        $candidateInputs.Add('build')
    }

    $resolved = [System.Collections.Generic.List[string]]::new()
    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

    foreach ($candidate in $candidateInputs) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }

        $fullPath = if ([System.IO.Path]::IsPathRooted($candidate)) {
            [System.IO.Path]::GetFullPath($candidate)
        } else {
            [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $candidate))
        }

        if ($seen.Add($fullPath)) {
            $resolved.Add($fullPath)
        }
    }

    return @($resolved)
}

function Invoke-WindowsExecutableWithPlugins {
    param(
        [Parameter(Mandatory = $true)]
        [string] $ExecutablePath,

        [Parameter()]
        [string[]] $PluginDirectories = @(),

        [Parameter(Mandatory = $true)]
        [string] $LogPath
    )

    $originalPath = $env:PATH
    $psNativePreferenceAvailable = $null -ne (Get-Variable -Name PSNativeCommandUseErrorActionPreference -Scope Global -ErrorAction SilentlyContinue)
    $originalPsNativePreference = $null
    
    try {
        $pluginPathEntries = @()
        foreach ($dir in $PluginDirectories) {
            $pluginPathEntries += $dir
            if (Test-Path -LiteralPath $dir -PathType Container) {
                $pluginSubDirs = Get-ChildItem -LiteralPath $dir -Directory -Recurse -ErrorAction SilentlyContinue |
                    ForEach-Object { $_.FullName }
                $pluginPathEntries += $pluginSubDirs
            }
        }

        $pluginPathEntries = @(
            $pluginPathEntries |
                Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
                Sort-Object -Unique
        )

        $logDir = [System.IO.Path]::GetDirectoryName($LogPath)
        if (-not [string]::IsNullOrWhiteSpace($logDir) -and -not (Test-Path $logDir)) {
            New-Item -ItemType Directory -Force -Path $logDir | Out-Null
        }

        if ($pluginPathEntries.Count -gt 0) {
            $env:PATH = (($pluginPathEntries -join ";") + ";" + $env:PATH)
        }

        if ($psNativePreferenceAvailable) {
            $originalPsNativePreference = $global:PSNativeCommandUseErrorActionPreference
            $global:PSNativeCommandUseErrorActionPreference = $false
        }
        $originalErrorActionPreference = $ErrorActionPreference
        $processExitCode = 1
        try {
            $ErrorActionPreference = "Continue"
            & $ExecutablePath 2>&1 | Tee-Object -FilePath $LogPath
            $processExitCode = $LASTEXITCODE
        }
        finally {
            $ErrorActionPreference = $originalErrorActionPreference
        }

        return $processExitCode
    }
    finally {
        if ($psNativePreferenceAvailable) {
            $global:PSNativeCommandUseErrorActionPreference = $originalPsNativePreference
        }
        $env:PATH = $originalPath
    }
}

Export-ModuleMember -Function @(
    'Resolve-DirectoryPath',
    'New-Timestamp',
    'New-TimestampedFilePath',
    'Resolve-NormalizedPath',
    'ConvertTo-ParameterList',
    'Resolve-WorkspacePath',
    'Resolve-WindowsBuildRootCandidates',
    'Invoke-WindowsExecutableWithPlugins'
)

