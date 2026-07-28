<#
.SYNOPSIS
  Agentic loop: planner (GLM 5.2) adds tasks to BACKLOG.md, executor
  (DeepSeek v4 Flash) drains the queue. Builds, tests, and quality checks
  run on configurable intervals.

.DESCRIPTION
  The loop alternates between two OpenCode agents:

    1. PLANNER  (expensive model, read-only + BACKLOG.md write)
       Analyzes the codebase and appends detailed task entries to BACKLOG.md.
       Every N iterations it focuses on refactor tasks.

    2. EXECUTOR (cheap model, full access)
       Picks up unchecked tasks one at a time, implements them, builds,
       tests, marks them [x], and commits. The queue must be fully drained
       before the planner runs again.

  After every K completed tasks a build is triggered, cycling through
  clangcl-debug, clangcl-profile, clangcl-release (Windows) or the Linux
  equivalents. Tests run after each build. clang-tidy + cmake-format run
  every M tasks.

  On Windows, builds go through the Stevedore container script
  (Build-Windows-Container.ps1). On Linux, through the native build script
  (cmake-configure-build.sh) with Rancher Desktop providing the container
  runtime for any containerized steps.

.PARAMETER ConfigPath
  Path to the agentic loop config JSON. Defaults to
  Scripts/AgenticLoop/AgenticLoop.config.json next to this script.

.PARAMETER DryRun
  Print what would happen without invoking opencode or running builds.

.PARAMETER MaxIterations
  Override the max-iterations from config (0 = unlimited).

.PARAMETER SkipBuild
  Skip the build phase entirely (useful for planning-only runs).

.PARAMETER SkipTests
  Skip the test phase.

.PARAMETER SkipQuality
  Skip the clang-tidy / cmake-format phase.

.PARAMETER PlannerOnly
  Run only the planner once and exit (no executor loop).

.PARAMETER ExecutorOnly
  Run only the executor to drain the current queue (no planner).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File .\Scripts\AgenticLoop\Run-AgenticLoop.ps1

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File .\Scripts\AgenticLoop\Run-AgenticLoop.ps1 -DryRun

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File .\Scripts\AgenticLoop\Run-AgenticLoop.ps1 -PlannerOnly
#>
param(
  [string]$ConfigPath,
  [switch]$DryRun,
  [int]$MaxIterations = -1,
  [switch]$SkipBuild,
  [switch]$SkipTests,
  [switch]$SkipQuality,
  [switch]$PlannerOnly,
  [switch]$ExecutorOnly
)

# ── Early bootstrap logging ──────────────────────────────────────────────
# Log file must be created BEFORE the first throw so errors are caught.
$scriptRoot = $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $scriptRoot '..\..')).Path
$logDir = Join-Path $repoRoot 'logs/agentic-loop'
if (-not (Test-Path $logDir)) {
  New-Item -ItemType Directory -Force $logDir | Out-Null
}
$timestamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$script:logFile = Join-Path $logDir "agentic-loop_$timestamp.log"
$script:exitCode = 0

# Minimal logger for bootstrap phase (used before full config is loaded).
function Write-Log {
  param([string]$Message, [string]$Level = 'INFO')
  $line = "[$(Get-Date -Format 'HH:mm:ss')] [$Level] $Message"
  if ($script:logFile) { Add-Content -Path $script:logFile -Value $line }
  Write-Host $line
}

# ── Global error trap ────────────────────────────────────────────────────
# This catches every unhandled error, writes it to the log, and preserves
# the exit code so the finally block can report it.
trap {
  $msg = $_.Exception.Message
  $line = $_.InvocationInfo.ScriptLineNumber
  $detail = "$msg (at line $line)"
  Write-Log "UNHANDLED ERROR: $detail" 'FATAL'
  Write-Log "StackTrace: $($_.Exception.StackTrace)" 'FATAL'
  $script:exitCode = 1
  # Do NOT re-throw — let the script continue to the finally block so it
  # logs the final summary. The finally block will exit with $exitCode.
  continue
}

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# ── Config ────────────────────────────────────────────────────────────────
if (-not $ConfigPath) {
  $ConfigPath = Join-Path $scriptRoot 'AgenticLoop.config.json'
}
if (-not (Test-Path $ConfigPath)) {
  Write-Log "Config not found: $ConfigPath" 'FATAL'
  Write-Log "Expected config at: $ConfigPath" 'FATAL'
  $script:exitCode = 1
  exit 1
}
$config = Get-Content $ConfigPath -Raw | ConvertFrom-Json
if (-not $config) {
  Write-Log "Failed to parse config: $ConfigPath (invalid JSON)" 'FATAL'
  exit 1
}

# Validate required config keys
$requiredKeys = @('models', 'intervals', 'buildConfigurations', 'build', 'logging')
foreach ($k in $requiredKeys) {
  if (-not $config.$k) {
    Write-Log "Config missing required key: '$k'" 'FATAL'
    exit 1
  }
}

# Override max iterations from CLI
if ($MaxIterations -ge 0) {
  $config.intervals.maxIterations = $MaxIterations
}

# Detect platform (PS 5.1 Desktop hat kein $PSVersionTable.Platform)
$onWindows = ($env:OS -eq 'Windows_NT')
$platformKey = if ($onWindows) { 'windows' } else { 'linux' }
$buildConfigs = $config.buildConfigurations.$platformKey
if (-not $buildConfigs -or @($buildConfigs).Count -eq 0) {
  Write-Log "No build configurations defined for platform '$platformKey' in config" 'FATAL'
  exit 1
}

# ── Environment for opencode model selection ──────────────────────────────
$env:OPENCODE_PLANNER_MODEL = $config.models.planner
$env:OPENCODE_EXECUTOR_MODEL = $config.models.executor

# ── Logging (post-config, with config-driven log path) ───────────────────
if ($config.logging -and $config.logging.logDir) {
  $logDir = Join-Path $repoRoot $config.logging.logDir
  if (-not (Test-Path $logDir)) {
    New-Item -ItemType Directory -Force $logDir | Out-Null
  }
}
$logToConsole = if ($config.logging) { $config.logging.logToConsole } else { $true }

# Re-bind Write-Log to the final log file
$script:logFile = Join-Path $logDir "agentic-loop_$timestamp.log"

filter Write-LogFilter {  # convenience filter for pipeline logging
  param([string]$Level = 'INFO')
  $line = "[$(Get-Date -Format 'HH:mm:ss')] [$Level] $_"
  if ($script:logFile) { Add-Content -Path $script:logFile -Value $line }
  if ($logToConsole) { Write-Host $line }
}

function Write-Log {
  param(
    [string]$Message,
    [string]$Level = 'INFO'
  )
  $line = "[$(Get-Date -Format 'HH:mm:ss')] [$Level] $Message"
  if ($script:logFile) { Add-Content -Path $script:logFile -Value $line }
  if ($logToConsole) { Write-Host $line }
}

function Write-Section {
  param([string]$Title)
  $bar = '=' * 60
  Write-Log ""
  Write-Log $bar
  Write-Log $Title
  Write-Log $bar
}

# Log startup banner
Write-Section "Agentic Loop Starting"
Write-Log "Invocation: $((Get-PSCallStack)[0].Position.ToString())"
Write-Log "PowerShell: $($PSVersionTable.PSVersion) | Edition: $($PSVersionTable.PSEdition)"
Write-Log "Platform: $platformKey"
Write-Log "Planner model: $($config.models.planner)"
Write-Log "Executor model: $($config.models.executor)"
Write-Log "Build configs cycle: $($buildConfigs -join ', ')"
Write-Log "Build every N tasks: $($config.intervals.buildEveryNTasks)"
Write-Log "Quality every N tasks: $($config.intervals.qualityEveryNTasks)"
Write-Log "Refactor every N iterations: $($config.intervals.refactorEveryNIterations)"
Write-Log "Max iterations: $($config.intervals.maxIterations) (0 = unlimited)"
Write-Log "Max executor retries: $($config.intervals.maxExecutorRetries)"
Write-Log "Dry run: $DryRun"
Write-Log "Log file: $script:logFile"

# ── Helpers ───────────────────────────────────────────────────────────────

function Invoke-OpenCode {
  param(
    [Parameter(Mandatory)][string]$Agent,
    [Parameter(Mandatory)][string]$Model,
    [Parameter(Mandatory)][string]$Message
  )
  if ($DryRun) {
    Write-Log "[DRY RUN] opencode run --agent $Agent --model $Model"
    return "[DRY RUN] skipped"
  }

  # Check opencode is on PATH before invoking
  if (-not (Get-Command 'opencode' -ErrorAction SilentlyContinue)) {
    Write-Log "opencode not found on PATH" 'FATAL'
    $script:exitCode = 1
    return $null
  }

  Write-Log "Invoking opencode: agent=$Agent model=$Model"

  # Write message to a temp file, then pipe file content into opencode.
  # Avoid 2>&1 entirely — it crashes under $ErrorActionPreference='Stop' in PS 5.1.
  $tmpMsgFile = [System.IO.Path]::GetTempFileName()
  try {
    [System.IO.File]::WriteAllText($tmpMsgFile, $Message, [System.Text.Encoding]::UTF8)
    $output = Get-Content $tmpMsgFile -Raw | & opencode run --agent $Agent --model $Model
    $exit = $LASTEXITCODE
  } catch {
    Write-Log "opencode invocation failed: $($_.Exception.Message)" 'ERROR'
    $exit = 1
    $output = @()
  } finally {
    if (Test-Path $tmpMsgFile) { Remove-Item $tmpMsgFile -Force }
  }

  $outputStr = $output -join "`n"
  $outputLen = if ($outputStr) { $outputStr.Length } else { 0 }

  Write-Log "opencode: ${outputLen} chars output, exit $exit"
  if ($outputLen -gt 0) {
    Add-Content -Path $script:logFile -Value "--- opencode output start ---"
    if ($outputLen -gt 50000) {
      Add-Content -Path $script:logFile -Value $outputStr.Substring(0, 50000)
      Add-Content -Path $script:logFile -Value "... [truncated]"
    } else {
      Add-Content -Path $script:logFile -Value $outputStr
    }
    Add-Content -Path $script:logFile -Value "--- opencode output end ---"
  } else {
    Write-Log "opencode produced no output (exit $exit)" 'WARN'
  }

  if ($exit -ne 0) {
    Write-Log "opencode exit $exit (agent=$Agent)" 'WARN'
    if ($outputStr -match 'model.*not found|invalid model|unknown model') {
      Write-Log "Model '$Model' was rejected" 'ERROR'
    }
    if ($outputStr -match 'API key|unauthorized|401|403') {
      Write-Log "Authentication error. Run 'opencode auth login'" 'ERROR'
    }
    if ($outputStr -match 'rate limit|429|too many requests') {
      Write-Log "Rate limited" 'WARN'
    }
  }

  return $outputStr
}

function Get-UncheckedTaskCount {
  $backlog = Get-Content (Join-Path $repoRoot 'BACKLOG.md') -Raw
  $taskMatches = [regex]::Matches($backlog, '(?m)^- \[ \]')
  return $taskMatches.Count
}

function Invoke-Build {
  param([string]$Configuration)
  Write-Section "BUILD: $Configuration"

  if ($onWindows) {
    $script = Join-Path $repoRoot $config.build.windowsScript
    $cmd = "powershell -ExecutionPolicy Bypass -File `"$script`" -Configurations `"$Configuration`" -SkipTests"
  } else {
    $script = Join-Path $repoRoot $config.build.linuxScript
    $cmd = "bash `"$script`" --preset `"$Configuration`" --build-dir build"
  }

  Write-Log "Build command: $cmd"
  if ($DryRun) {
    Write-Log "[DRY RUN] skipped build"
    return $true
  }

  $output = Invoke-Expression $cmd 2>&1
  $outputStr = $output -join "`n"
  Add-Content -Path $logFile -Value "--- build output start ---"
  Add-Content -Path $logFile -Value $outputStr
  Add-Content -Path $logFile -Value "--- build output end ---"

  if ($LASTEXITCODE -ne 0) {
    Write-Log "BUILD FAILED (exit $LASTEXITCODE)" "ERROR"
    return $false
  }
  Write-Log "BUILD PASSED"
  return $true
}

function Invoke-Tests {
  Write-Section "TESTS"
  $cmd = if ($onWindows) { $config.build.windowsTestCommand } else { $config.build.linuxTestCommand }

  Write-Log "Test command: $cmd"
  if ($DryRun) {
    Write-Log "[DRY RUN] skipped tests"
    return $true
  }

  Push-Location $repoRoot
  try {
    $output = Invoke-Expression $cmd 2>&1
    $outputStr = $output -join "`n"
    Add-Content -Path $logFile -Value "--- test output start ---"
    Add-Content -Path $logFile -Value $outputStr
    Add-Content -Path $logFile -Value "--- test output end ---"

    if ($LASTEXITCODE -ne 0) {
      Write-Log "TESTS FAILED (exit $LASTEXITCODE)" "ERROR"
      return $false
    }
    Write-Log "TESTS PASSED"
    return $true
  }
  finally {
    Pop-Location
  }
}

function Invoke-Quality {
  Write-Section "QUALITY (clang-tidy + cmake-format)"
  $cmd = if ($onWindows) { $config.build.windowsQualityCommand } else { $config.build.linuxQualityCommand }
  Write-Log "Quality command: $cmd"
  if ($DryRun) {
    Write-Log "[DRY RUN] skipped quality"
    return
  }

  Push-Location $repoRoot
  try {
    $output = Invoke-Expression $cmd 2>&1
    $outputStr = $output -join "`n"
    Add-Content -Path $logFile -Value "--- quality output start ---"
    Add-Content -Path $logFile -Value $outputStr
    Add-Content -Path $logFile -Value "--- quality output end ---"
    Write-Log "Quality check complete (exit $LASTEXITCODE)"
  }
  finally {
    Pop-Location
  }
}

function Invoke-GitCommit {
  param([string]$Message)
  if (-not $config.git.autoCommit) { return }
  if ($DryRun) {
    Write-Log "[DRY RUN] skipped git commit: $Message"
    return
  }
  Push-Location $repoRoot
  try {
    & git add -A 2>&1 | Out-Null
    & git commit -m $Message 2>&1 | Out-Null
    Write-Log "Committed: $Message"
  }
  finally {
    Pop-Location
  }
}

function Invoke-Planner {
  param([switch]$RefactorFocus)
  Write-Section "PLANNER PHASE"

  if ($RefactorFocus) {
    Write-Log "Refactor-focused planning cycle"
    $message = @"
Analyze the codebase for refactoring opportunities. Focus on:
- Dead code and unused functions
- API consolidation and duplicate logic
- Test coverage gaps (especially error paths)
- Documentation drift (comments that no longer match code)
- Performance issues (unnecessary copies, O(n^2) patterns)
- C++23 modernization (std::span, std::expected, constexpr)

Read BACKLOG.md first to avoid duplicates. Add at most 3 refactor tasks
marked with (refactor) in the title. Each task must include file paths,
numbered steps, test guidance, and build instructions.
"@
  } else {
    $message = @"
Analyze the current state of the codebase. Review BACKLOG.md for existing open
tasks. Identify new work opportunities: bugs, improvements, missing tests,
technical debt, performance issues. Write detailed, actionable task entries to
BACKLOG.md following the existing format. Do NOT duplicate existing tasks.
Add at most 5 new tasks. Each task must include: size (S/M/L/XL), title,
files to read, numbered implementation steps, test guidance, and build preset.
"@
  }

  $output = Invoke-OpenCode -Agent 'planner' -Model $config.models.planner -Message $message
  Write-Log "Planner complete"
  return $output
}

function Invoke-Executor {
  param([string]$TaskHint = "")
  Write-Section "EXECUTOR PHASE — next task"

  # Note: @" must be the FIRST character on the closing line (no indent).
  $message = @"
Read BACKLOG.md and find the first unchecked task ('[ ]'). Implement it fully:
make the code changes, add or update tests, and build with the appropriate
preset. Once the task is complete and the build passes, mark it as checked
('[x]') in BACKLOG.md with a brief summary. Then commit the changes with a
descriptive message.

$TaskHint
"@

  $output = Invoke-OpenCode -Agent 'executor' -Model $config.models.executor -Message $message
  Write-Log "Executor complete"
  return $output
}

# ── Main Loop ────────────────────────────────────────────────────────────

$startTime = Get-Date
$iteration = 0
$tasksCompleted = 0
$buildCycleIndex = 0

try {
  # Planner-only mode
  if ($PlannerOnly) {
    Write-Log "Planner-only mode"
    $doRefactor = (($iteration + 1) % $config.intervals.refactorEveryNIterations -eq 0)
    Invoke-Planner -RefactorFocus:$doRefactor
    Write-Section "Agentic Loop Complete (planner-only)"
    exit 0
  }

  # Executor-only mode (drain current queue)
  if ($ExecutorOnly) {
    Write-Log "Executor-only mode: draining current queue"
    $unchecked = Get-UncheckedTaskCount
    Write-Log "Unchecked tasks: $unchecked"
    while ($unchecked -gt 0) {
      Invoke-Executor
      $tasksCompleted++
      $unchecked = Get-UncheckedTaskCount
      Write-Log "Tasks completed: $tasksCompleted | Remaining: $unchecked"

      if (-not $SkipBuild -and ($tasksCompleted % $config.intervals.buildEveryNTasks -eq 0)) {
        $configName = $buildConfigs[$buildCycleIndex % $buildConfigs.Count]
        $buildOk = Invoke-Build -Configuration $configName
        $buildCycleIndex++
        if ($buildOk -and (-not $SkipTests) -and $config.intervals.testAfterBuild) {
          Invoke-Tests | Out-Null
        }
      }
      if (-not $SkipQuality -and ($tasksCompleted % $config.intervals.qualityEveryNTasks -eq 0)) {
        Invoke-Quality
      }
    }
    Write-Section "Agentic Loop Complete (executor-only, $tasksCompleted tasks)"
    exit 0
  }

  # Full loop
  while ($true) {
    $iteration++
    Write-Section "ITERATION $iteration"

    # Check max iterations
    if ($config.intervals.maxIterations -gt 0 -and $iteration -gt $config.intervals.maxIterations) {
      Write-Log "Reached max iterations ($($config.intervals.maxIterations)). Stopping."
      break
    }

    # ── Phase 1: Planner ──────────────────────────────────────────────
    $doRefactor = ($iteration % $config.intervals.refactorEveryNIterations -eq 0)
    Invoke-Planner -RefactorFocus:$doRefactor

    # ── Phase 2: Executor (drain the queue) ───────────────────────────
    $unchecked = Get-UncheckedTaskCount
    Write-Log "Tasks in queue: $unchecked"

    $retryCount = 0
    while ($unchecked -gt 0) {
      Write-Log "Processing task ($tasksCompleted completed so far, $unchecked remaining)"

      $output = Invoke-Executor

      # Check if the executor actually made progress
      $newUnchecked = Get-UncheckedTaskCount
      if ($newUnchecked -ge $unchecked) {
        $retryCount++
        Write-Log "No progress detected (still $newUnchecked unchecked). Retry $retryCount/$($config.intervals.maxExecutorRetries)" "WARN"
        if ($retryCount -ge $config.intervals.maxExecutorRetries) {
          Write-Log "Max retries reached. Skipping remaining tasks and moving to next planning cycle." "ERROR"
          break
        }
      } else {
        $retryCount = 0
        $tasksCompleted++
        $unchecked = $newUnchecked
        Write-Log "Task complete. Tasks completed: $tasksCompleted | Remaining: $unchecked"

        # Git commit after each task
        Invoke-GitCommit -Message "$($config.git.commitPrefix): task #$tasksCompleted completed (iteration $iteration)"

        # ── Build phase ───────────────────────────────────────────────
        if (-not $SkipBuild -and ($tasksCompleted % $config.intervals.buildEveryNTasks -eq 0)) {
          $configName = $buildConfigs[$buildCycleIndex % $buildConfigs.Count]
          $buildOk = Invoke-Build -Configuration $configName
          $buildCycleIndex++

          # ── Test phase ──────────────────────────────────────────────
          if ($buildOk -and (-not $SkipTests) -and $config.intervals.testAfterBuild) {
            Invoke-Tests | Out-Null
          }
        }

        # ── Quality phase ─────────────────────────────────────────────
        if (-not $SkipQuality -and ($tasksCompleted % $config.intervals.qualityEveryNTasks -eq 0)) {
          Invoke-Quality
        }
      }
    }

    if ($unchecked -eq 0) {
      Write-Log "Queue drained. All tasks complete."
    }

    # ── Delay between iterations ──────────────────────────────────────
    if ($config.intervals.loopDelaySeconds -gt 0) {
      Write-Log "Sleeping $($config.intervals.loopDelaySeconds)s..."
      if (-not $DryRun) {
        Start-Sleep -Seconds $config.intervals.loopDelaySeconds
      }
    }
  }
}
finally {
  $elapsed = if ($startTime) { [math]::Round(((Get-Date) - $startTime).TotalMinutes, 1) } else { 'N/A' }
  Write-Section "Agentic Loop Finished"
  Write-Log "Exit code: $script:exitCode"
  Write-Log "Total iterations: $iteration"
  Write-Log "Total tasks completed: $tasksCompleted"
  Write-Log "Elapsed time: ${elapsed}min"
  Write-Log "Log file: $script:logFile"

  if ($script:exitCode -ne 0) {
    Write-Log "The loop exited with errors. Check sections above marked [ERROR] or [FATAL]." 'WARN'
    Write-Log "Common fixes:" 'WARN'
    Write-Log "  1. Ensure opencode is installed and authenticated (opencode auth login)" 'WARN'
    Write-Log "  2. Verify model IDs in Scripts/AgenticLoop/AgenticLoop.config.json" 'WARN'
    Write-Log "  3. Check that the config JSON is valid" 'WARN'
    Write-Log "  4. Run with -DryRun to test the configuration without executing" 'WARN'
    Write-Log "  5. For model issues, run 'opencode models' to list available models" 'WARN'
    Write-Log "  6. Run with -MaxIterations 1 to test a single iteration" 'WARN'
  }

  exit $script:exitCode
}