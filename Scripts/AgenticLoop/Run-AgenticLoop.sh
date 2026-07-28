#!/usr/bin/env bash
set -euo pipefail

# ─────────────────────────────────────────────────────────────────────────
# Agentic loop for Kataglyphis-BeschleunigerBallett (Linux / Rancher Desktop)
#
# Planner (GLM 5.2) adds tasks to BACKLOG.md.
# Executor (DeepSeek v4 Flash) drains the queue.
# Builds, tests, and quality checks run on configurable intervals.
#
# Usage:
#   ./Scripts/AgenticLoop/Run-AgenticLoop.sh [options]
#
# Options:
#   --config PATH        Config JSON path (default: AgenticLoop.config.json)
#   --dry-run            Print actions without invoking opencode or builds
#   --max-iterations N   Override max iterations (0 = unlimited)
#   --skip-build         Skip the build phase
#   --skip-tests         Skip the test phase
#   --skip-quality       Skip clang-tidy / cmake-format
#   --planner-only       Run the planner once and exit
#   --executor-only      Drain the current queue and exit
#   --help               Show this help
# ─────────────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# ── Early log file (must exist before any error output) ──────────────────
LOG_DIR="${REPO_ROOT}/logs/agentic-loop"
mkdir -p "$LOG_DIR"
TIMESTAMP=$(date '+%Y-%m-%d_%H-%M-%S')
LOG_FILE="${LOG_DIR}/agentic-loop_${TIMESTAMP}.log"
EXIT_CODE=0
START_TIME=$(date +%s)

# ── Trap handler ─────────────────────────────────────────────────────────
# Writes unhandled errors to the log and ensures cleanup runs.
cleanup() {
  local ec=$?
  [[ $ec -ne 0 ]] && EXIT_CODE=$ec
  local elapsed=$(( $(date +%s) - START_TIME ))
  local minutes=$(( elapsed / 60 ))
  section "Agentic Loop Finished"
  log "Exit code: $EXIT_CODE"
  log "Total iterations: ${iteration:-0}"
  log "Total tasks completed: ${tasks_completed:-0}"
  log "Elapsed time: ${minutes}min"
  log "Log file: $LOG_FILE"
  if [[ $EXIT_CODE -ne 0 ]]; then
    log "The loop exited with errors. Check sections above marked [ERROR] or [FATAL]." "WARN"
    log "Common fixes:" "WARN"
    log "  1. Ensure opencode is installed and authenticated (opencode auth login)" "WARN"
    log "  2. Verify model IDs in Scripts/AgenticLoop/AgenticLoop.config.json" "WARN"
    log "  3. Check that the config JSON is valid" "WARN"
    log "  4. Run with --dry-run to test the configuration without executing" "WARN"
    log "  5. For model issues, run 'opencode models' to list available models" "WARN"
    log "  6. Run with --max-iterations 1 to test a single iteration" "WARN"
  fi
  exit $EXIT_CODE
}
trap cleanup EXIT

# Bash-specific: print a log-friendly message on any unhandled error
error_handler() {
  local line=$1
  local cmd=$2
  log "Unhandled error at line $line: '$cmd'" "FATAL"
  EXIT_CODE=1
}
trap 'error_handler $LINENO "$BASH_COMMAND"' ERR

# ── Defaults ─────────────────────────────────────────────────────────────
CONFIG_PATH="${SCRIPT_DIR}/AgenticLoop.config.json"
DRY_RUN=false
MAX_ITERATIONS_OVERRIDE=""
SKIP_BUILD=false
SKIP_TESTS=false
SKIP_QUALITY=false
PLANNER_ONLY=false
EXECUTOR_ONLY=false

# ── Arg parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
  case "$1" in
    --config)        CONFIG_PATH="$2"; shift 2 ;;
    --dry-run)       DRY_RUN=true; shift ;;
    --max-iterations) MAX_ITERATIONS_OVERRIDE="$2"; shift 2 ;;
    --skip-build)    SKIP_BUILD=true; shift ;;
    --skip-tests)    SKIP_TESTS=true; shift ;;
    --skip-quality)  SKIP_QUALITY=true; shift ;;
    --planner-only)  PLANNER_ONLY=true; shift ;;
    --executor-only) EXECUTOR_ONLY=true; shift ;;
    --help|-h)
      head -25 "$0" | tail -23
      exit 0 ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

# ── Config loading (requires jq) ─────────────────────────────────────────
if ! command -v jq &>/dev/null; then
  echo "ERROR: jq is required. Install it: sudo apt install jq (or equivalent)."
  exit 1
fi

if [[ ! -f "$CONFIG_PATH" ]]; then
  echo "ERROR: Config not found: $CONFIG_PATH"
  exit 1
fi

PLANNER_MODEL=$(jq -r '.models.planner' "$CONFIG_PATH")
EXECUTOR_MODEL=$(jq -r '.models.executor' "$CONFIG_PATH")
BUILD_EVERY_N=$(jq -r '.intervals.buildEveryNTasks' "$CONFIG_PATH")
QUALITY_EVERY_N=$(jq -r '.intervals.qualityEveryNTasks' "$CONFIG_PATH")
REFACTOR_EVERY_N=$(jq -r '.intervals.refactorEveryNIterations' "$CONFIG_PATH")
TEST_AFTER_BUILD=$(jq -r '.intervals.testAfterBuild' "$CONFIG_PATH")
MAX_EXECUTOR_RETRIES=$(jq -r '.intervals.maxExecutorRetries' "$CONFIG_PATH")
LOOP_DELAY=$(jq -r '.intervals.loopDelaySeconds' "$CONFIG_PATH")
MAX_ITERATIONS=$(jq -r '.intervals.maxIterations' "$CONFIG_PATH")
LINUX_TEST_CMD=$(jq -r '.build.linuxTestCommand' "$CONFIG_PATH")
LINUX_QUALITY_CMD=$(jq -r '.build.linuxQualityCommand' "$CONFIG_PATH")
LINUX_BUILD_SCRIPT=$(jq -r '.build.linuxScript' "$CONFIG_PATH")
AUTO_COMMIT=$(jq -r '.git.autoCommit' "$CONFIG_PATH")
COMMIT_PREFIX=$(jq -r '.git.commitPrefix' "$CONFIG_PATH")
LOG_DIR_REL=$(jq -r '.logging.logDir' "$CONFIG_PATH")

# Read build configurations array
mapfile -t BUILD_CONFIGS < <(jq -r '.buildConfigurations.linux[]' "$CONFIG_PATH")

if [[ -n "$MAX_ITERATIONS_OVERRIDE" ]]; then
  MAX_ITERATIONS="$MAX_ITERATIONS_OVERRIDE"
fi

# ── Environment for opencode ─────────────────────────────────────────────
export OPENCODE_PLANNER_MODEL="$PLANNER_MODEL"
export OPENCODE_EXECUTOR_MODEL="$EXECUTOR_MODEL"

# ── Logging (post-config, using config-driven log path) ──────────────────
if jq -e '.logging.logDir' "$CONFIG_PATH" &>/dev/null; then
  LOG_DIR_REL_CFG=$(jq -r '.logging.logDir // "logs/agentic-loop"' "$CONFIG_PATH")
  LOG_DIR="${REPO_ROOT}/${LOG_DIR_REL_CFG}"
  mkdir -p "$LOG_DIR"
  LOG_FILE="${LOG_DIR}/agentic-loop_${TIMESTAMP}.log"
fi

log() {
  local level="${2:-INFO}"
  local line="[$(date '+%H:%M:%S')] [$level] $1"
  echo "$line" >> "$LOG_FILE"
  echo "$line"
}

section() {
  local bar="$(printf '=%.0s' {1..60})"
  log ""
  log "$bar"
  log "$1"
  log "$bar"
}

# ── Helpers ──────────────────────────────────────────────────────────────

unchecked_task_count() {
  local backlog="${REPO_ROOT}/BACKLOG.md"
  grep -c '^- \[ \]' "$backlog" 2>/dev/null || echo 0
}

invoke_opencode() {
  local agent="$1" model="$2" message="$3"
  if $DRY_RUN; then
    log "[DRY RUN] opencode run --agent $agent --model $model"
    return 0
  fi

  # Check opencode is available
  if ! command -v opencode &>/dev/null; then
    log "opencode not found on PATH. Install it: curl -fsSL https://opencode.ai/install | bash" "FATAL"
    EXIT_CODE=1
    return 1
  fi

  log "Invoking opencode: agent=$agent model=$model"
  local exit_code=0
  local output
  output=$(opencode run --agent "$agent" --model "$model" "$message" 2>&1) || exit_code=$?
  {
    echo "--- opencode output start ---"
    echo "$output"
    echo "--- opencode output end ---"
  } >> "$LOG_FILE"

  if [[ $exit_code -ne 0 ]]; then
    log "opencode exited with code $exit_code (agent=$agent)" "WARN"
    # Check for common errors
    if echo "$output" | grep -qiE "model.*not found|invalid model|unknown model"; then
      log "Model '$model' was rejected by opencode. Check the model ID." "ERROR"
    fi
    if echo "$output" | grep -qiE "API key|unauthorized|401|403"; then
      log "Authentication error. Run 'opencode auth login' to configure credentials." "ERROR"
    fi
    if echo "$output" | grep -qiE "rate limit|429|too many requests"; then
      log "Rate limited. Consider increasing loopDelaySeconds in config." "WARN"
    fi
  fi

  log "opencode finished (exit $exit_code)"
  return $exit_code
}

invoke_build() {
  local config_name="$1"
  section "BUILD: $config_name"
  local script="${REPO_ROOT}/${LINUX_BUILD_SCRIPT}"
  local cmd="bash \"${script}\" --preset \"${config_name}\" --build-dir build"
  log "Build command: $cmd"
  if $DRY_RUN; then
    log "[DRY RUN] skipped build"
    return 0
  fi
  cd "$REPO_ROOT"
  if eval "$cmd" >> "$LOG_FILE" 2>&1; then
    log "BUILD PASSED"
    return 0
  else
    log "BUILD FAILED" "ERROR"
    return 1
  fi
}

invoke_tests() {
  section "TESTS"
  local cmd="$LINUX_TEST_CMD"
  log "Test command: $cmd"
  if $DRY_RUN; then
    log "[DRY RUN] skipped tests"
    return 0
  fi
  cd "$REPO_ROOT"
  if eval "$cmd" >> "$LOG_FILE" 2>&1; then
    log "TESTS PASSED"
    return 0
  else
    log "TESTS FAILED" "ERROR"
    return 1
  fi
}

invoke_quality() {
  section "QUALITY (clang-tidy + cmake-format)"
  local cmd="$LINUX_QUALITY_CMD"
  log "Quality command: $cmd"
  if $DRY_RUN; then
    log "[DRY RUN] skipped quality"
    return 0
  fi
  cd "$REPO_ROOT"
  eval "$cmd" >> "$LOG_FILE" 2>&1 || true
  log "Quality check complete"
}

git_commit() {
  local msg="$1"
  if [[ "$AUTO_COMMIT" != "true" ]]; then return; fi
  if $DRY_RUN; then
    log "[DRY RUN] skipped git commit: $msg"
    return
  fi
  cd "$REPO_ROOT"
  git add -A 2>/dev/null || true
  git commit -m "$msg" 2>/dev/null || true
  log "Committed: $msg"
}

invoke_planner() {
  local refactor_focus="$1"
  section "PLANNER PHASE"
  local message
  if $refactor_focus; then
    log "Refactor-focused planning cycle"
    message="Analyze the codebase for refactoring opportunities. Focus on dead code, API consolidation, test coverage gaps, documentation drift, performance issues, and C++23 modernization. Read BACKLOG.md first to avoid duplicates. Add at most 3 refactor tasks marked with (refactor) in the title. Each task must include file paths, numbered steps, test guidance, and build instructions."
  else
    message="Analyze the current state of the codebase. Review BACKLOG.md for existing open tasks. Identify new work opportunities: bugs, improvements, missing tests, technical debt, performance issues. Write detailed, actionable task entries to BACKLOG.md following the existing format. Do NOT duplicate existing tasks. Add at most 5 new tasks. Each task must include: size (S/M/L/XL), title, files to read, numbered implementation steps, test guidance, and build preset."
  fi
  invoke_opencode "planner" "$PLANNER_MODEL" "$message"
  log "Planner complete"
}

invoke_executor() {
  section "EXECUTOR PHASE — next task"
  local message="Read BACKLOG.md and find the first unchecked task (- [ ]). Implement it fully: make the code changes, add or update tests, and build with the appropriate preset. Once the task is complete and the build passes, mark it as checked (- [x]) in BACKLOG.md with a brief summary. Then commit the changes."
  invoke_opencode "executor" "$EXECUTOR_MODEL" "$message"
  log "Executor complete"
}

# ── Main Loop ────────────────────────────────────────────────────────────

section "Agentic Loop Starting (Linux / Rancher Desktop)"
log "Planner model: $PLANNER_MODEL"
log "Executor model: $EXECUTOR_MODEL"
log "Build configs cycle: ${BUILD_CONFIGS[*]}"
log "Build every N tasks: $BUILD_EVERY_N"
log "Quality every N tasks: $QUALITY_EVERY_N"
log "Refactor every N iterations: $REFACTOR_EVERY_N"
log "Max iterations: $MAX_ITERATIONS (0 = unlimited)"
log "Dry run: $DRY_RUN"
log "Log file: $LOG_FILE"

iteration=0
tasks_completed=0
build_cycle_index=0

# Planner-only mode
if $PLANNER_ONLY; then
  log "Planner-only mode"
  do_refactor=$(( (iteration + 1) % REFACTOR_EVERY_N == 0 ))
  invoke_planner $do_refactor
  section "Agentic Loop Complete (planner-only)"
  exit 0
fi

# Executor-only mode
if $EXECUTOR_ONLY; then
  log "Executor-only mode: draining current queue"
  unchecked=$(unchecked_task_count)
  log "Unchecked tasks: $unchecked"
  while [[ "$unchecked" -gt 0 ]]; do
    invoke_executor
    tasks_completed=$((tasks_completed + 1))
    unchecked=$(unchecked_task_count)
    log "Tasks completed: $tasks_completed | Remaining: $unchecked"
    if ! $SKIP_BUILD && (( tasks_completed % BUILD_EVERY_N == 0 )); then
      config_name="${BUILD_CONFIGS[$((build_cycle_index % ${#BUILD_CONFIGS[@]}))]}"
      if invoke_build "$config_name"; then
        if ! $SKIP_TESTS && [[ "$TEST_AFTER_BUILD" == "true" ]]; then
          invoke_tests || true
        fi
      fi
      build_cycle_index=$((build_cycle_index + 1))
    fi
    if ! $SKIP_QUALITY && (( tasks_completed % QUALITY_EVERY_N == 0 )); then
      invoke_quality
    fi
  done
  section "Agentic Loop Complete (executor-only, $tasks_completed tasks)"
  exit 0
fi

# Full loop
while true; do
  iteration=$((iteration + 1))
  section "ITERATION $iteration"

  if [[ "$MAX_ITERATIONS" -gt 0 ]] && [[ "$iteration" -gt "$MAX_ITERATIONS" ]]; then
    log "Reached max iterations ($MAX_ITERATIONS). Stopping."
    break
  fi

  # Phase 1: Planner
  do_refactor=$(( iteration % REFACTOR_EVERY_N == 0 ))
  invoke_planner $do_refactor

  # Phase 2: Executor (drain the queue)
  unchecked=$(unchecked_task_count)
  log "Tasks in queue: $unchecked"

  retry_count=0
  while [[ "$unchecked" -gt 0 ]]; do
    log "Processing task ($tasks_completed completed so far, $unchecked remaining)"
    invoke_executor

    new_unchecked=$(unchecked_task_count)
    if [[ "$new_unchecked" -ge "$unchecked" ]]; then
      retry_count=$((retry_count + 1))
      log "No progress detected (still $new_unchecked unchecked). Retry $retry_count/$MAX_EXECUTOR_RETRIES" "WARN"
      if [[ "$retry_count" -ge "$MAX_EXECUTOR_RETRIES" ]]; then
        log "Max retries reached. Skipping remaining tasks." "ERROR"
        break
      fi
    else
      retry_count=0
      tasks_completed=$((tasks_completed + 1))
      unchecked=$new_unchecked
      log "Task complete. Tasks completed: $tasks_completed | Remaining: $unchecked"

      git_commit "${COMMIT_PREFIX}: task #${tasks_completed} completed (iteration ${iteration})"

      # Build phase
      if ! $SKIP_BUILD && (( tasks_completed % BUILD_EVERY_N == 0 )); then
        config_name="${BUILD_CONFIGS[$((build_cycle_index % ${#BUILD_CONFIGS[@]}))]}"
        if invoke_build "$config_name"; then
          if ! $SKIP_TESTS && [[ "$TEST_AFTER_BUILD" == "true" ]]; then
            invoke_tests || true
          fi
        fi
        build_cycle_index=$((build_cycle_index + 1))
      fi

      # Quality phase
      if ! $SKIP_QUALITY && (( tasks_completed % QUALITY_EVERY_N == 0 )); then
        invoke_quality
      fi
    fi
  done

  if [[ "$unchecked" -eq 0 ]]; then
    log "Queue drained. All tasks complete."
  fi

  # Delay between iterations
  if [[ "$LOOP_DELAY" -gt 0 ]]; then
    log "Sleeping ${LOOP_DELAY}s..."
    if ! $DRY_RUN; then
      sleep "$LOOP_DELAY"
    fi
  fi
done

# cleanup trap handles the final summary (defined at top of script)