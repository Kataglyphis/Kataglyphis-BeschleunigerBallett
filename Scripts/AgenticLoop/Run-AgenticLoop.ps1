#requires -Version 7.0
<#
.SYNOPSIS
  Agentic loop: planner adds tasks to BACKLOG.md, executor drains the queue.
  Uses WindowsAgenticLoop.Common module from Kataglyphis-ContainerHub.
.PARAMETER DryRun  Print actions without executing.
.PARAMETER MaxIterations  Override max iterations (0 = unlimited).
.PARAMETER PlannerOnly  Run planner once and exit.
.PARAMETER ExecutorOnly  Drain the queue and exit.
#>
param([switch]$DryRun, [int]$MaxIterations = -1, [switch]$SkipBuild, [switch]$SkipTests,
      [switch]$SkipQuality, [switch]$PlannerOnly, [switch]$ExecutorOnly)

$ErrorActionPreference = 'Stop'; Set-StrictMode -Version Latest
$scriptRoot = $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $scriptRoot '..\..')).Path

# Resolve module from ContainerHub or vendored fallback
$modulePath = $null
foreach ($c in @((Join-Path $repoRoot 'ExternalLib\Kataglyphis-ContainerHub\windows\scripts\modules\WindowsAgenticLoop.Common.psm1'),
                 (Join-Path $scriptRoot 'modules\WindowsAgenticLoop.Common.psm1'))) {
    if (Test-Path $c) { $modulePath = (Resolve-Path $c).Path; break }
}
if (-not $modulePath) { Write-Host "FATAL: Module not found" -ForegroundColor Red; exit 1 }
Import-Module $modulePath -Force

# Config
$configPath = Join-Path $scriptRoot 'AgenticLoop.config.json'
if (-not (Test-Path $configPath)) { Write-Host "FATAL: Config not found: $configPath" -ForegroundColor Red; exit 1 }
$config = Get-Content $configPath -Raw | ConvertFrom-Json
if (-not $config) { Write-Host "FATAL: Invalid JSON" -ForegroundColor Red; exit 1 }

$timeoutSeconds = if ($config.intervals.timeoutSeconds) { [int]$config.intervals.timeoutSeconds } else { 0 }
Initialize-AgenticLoop -ConfigPath $configPath -RepoRoot $repoRoot -DryRun:$DryRun -TimeoutSeconds $timeoutSeconds

$onWindows = Test-IsWindows
# Prefer buildMatrix (richer: per-config sanitizer, testCommand, buildDir);
# fall back to legacy buildConfigurations (string arrays) for backward compat.
$buildConfigs = if ($config.buildMatrix) {
    if ($onWindows) { $config.buildMatrix.windows } else { $config.buildMatrix.linux }
} elseif ($config.buildConfigurations) {
    if ($onWindows) { $config.buildConfigurations.windows } else { $config.buildConfigurations.linux }
} else { $null }
if (-not $buildConfigs) { Write-AgenticLog 'No build configs (need buildMatrix or buildConfigurations in config)' 'FATAL'; exit 1 }

try {
    Invoke-AgenticLoop -Config $config -PlannerPrompt 'plan' -ExecutorPrompt 'execute' `
        -BuildConfigs $buildConfigs -OnWindows $onWindows -RepoRoot $repoRoot `
        -MaxIterations:$MaxIterations -SkipBuild:$SkipBuild -SkipTests:$SkipTests `
        -SkipQuality:$SkipQuality -PlannerOnly:$PlannerOnly -ExecutorOnly:$ExecutorOnly
} finally {
    Complete-AgenticLoop
}
