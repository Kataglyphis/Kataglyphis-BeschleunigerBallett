#requires -Version 7.0
<#
.SYNOPSIS
  Agentic loop: planner adds tasks to BACKLOG.md, executor drains the queue.
  Uses WindowsAgenticLoop.Common module from Kataglyphis-ContainerHub.

  Engines (config .engine, or -Engine / $env:AGENTIC_ENGINE):
    claude   — planner: Fable 5 (fallback Opus 4.8), executor: Sonnet
    opencode — planner: GLM 5.2, executor: DeepSeek v4 Flash
.PARAMETER Engine  Engine override: claude | opencode (default: config .engine).
.PARAMETER DryRun  Print actions without executing.
.PARAMETER MaxIterations  Override max iterations (0 = unlimited).
.PARAMETER PlannerOnly  Run planner once and exit.
.PARAMETER ExecutorOnly  Drain the queue and exit.
#>
param([string]$Engine = '', [switch]$DryRun, [int]$MaxIterations = -1, [switch]$SkipBuild,
      [switch]$SkipTests, [switch]$SkipQuality, [switch]$PlannerOnly, [switch]$ExecutorOnly)

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

Initialize-AgenticLoop -ConfigPath $configPath -RepoRoot $repoRoot -DryRun:$DryRun

$onWindows = Test-IsWindows
# Prefer buildMatrix (richer: per-config sanitizer, testCommand, buildDir);
# fall back to legacy buildConfigurations (string arrays) for backward compat.
$buildConfigs = if (Get-AgenticConfigValue $config 'buildMatrix' $null) {
    if ($onWindows) { $config.buildMatrix.windows } else { $config.buildMatrix.linux }
} elseif (Get-AgenticConfigValue $config 'buildConfigurations' $null) {
    if ($onWindows) { $config.buildConfigurations.windows } else { $config.buildConfigurations.linux }
} else { $null }
if (-not $buildConfigs) { Write-AgenticLog 'No build configs (need buildMatrix or buildConfigurations in config)' 'FATAL'; exit 1 }

$plannerPrompt = 'Analyze the current state of the codebase. Review BACKLOG.md for existing open tasks. Identify new work opportunities: bugs, improvements, missing tests, technical debt, performance issues. Write detailed, actionable task entries to BACKLOG.md following the existing format. Do NOT duplicate existing tasks. Add at most 5 new tasks. Each task must include: size (S/M/L/XL), title, files to read, numbered implementation steps, test guidance, and build preset.'
$refactorPlannerPrompt = 'Analyze the codebase for refactoring opportunities. Focus on dead code, API consolidation, test coverage gaps, documentation drift, performance issues, and C++23 modernization. Read BACKLOG.md first to avoid duplicates. Add at most 3 refactor tasks marked with (refactor) in the title. Each task must include file paths, numbered steps, test guidance, and build instructions.'
$executorPrompt = 'Read BACKLOG.md and find the first unchecked task (- [ ]). Implement it fully: make the code changes, add or update tests, and build with the appropriate preset. Once the task is complete and the build passes, mark it as checked (- [x]) in BACKLOG.md with a brief summary. Then commit the changes.'

try {
    Invoke-AgenticLoop -Config $config -Engine $Engine `
        -PlannerPrompt $plannerPrompt -RefactorPlannerPrompt $refactorPlannerPrompt -ExecutorPrompt $executorPrompt `
        -BuildConfigs $buildConfigs -OnWindows $onWindows -RepoRoot $repoRoot `
        -MaxIterations:$MaxIterations -SkipBuild:$SkipBuild -SkipTests:$SkipTests `
        -SkipQuality:$SkipQuality -PlannerOnly:$PlannerOnly -ExecutorOnly:$ExecutorOnly
} finally {
    Complete-AgenticLoop
}
