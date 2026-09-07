# Agentic loop — the BeschleunigerBallett half

A planner/executor loop that drains `BACKLOG.md` autonomously: an expensive
planner writes tasks, a cheap executor implements them, and builds, tests and
quality gates run on a cadence.

**The loop's logic is not in this directory.** It lives in the ContainerHub
submodule — `windows/scripts/modules/WindowsAgenticLoop.Common.psm1` and
`linux/scripts/lib/agentic-loop.sh`. What is here is the thin consumer half:
one config, two runners, two prompt overlays. Everything generic (queue
discipline, the blocked-task protocol, model tiering, retries, build-failure
fixing) is documented upstream and deliberately not repeated here — this file
used to restate it, and the restatement is what went stale.

## Upstream docs

| Topic | Doc |
| --- | --- |
| Module API, `Invoke-AgenticLoop` parameters, the config-key table (it does not yet list the two `*PromptOverlayFile` keys — see [The prompt overlays](#the-prompt-overlays)), prerequisites, env overrides, usage examples | [`windows-agentic-loop.md`](../../third_party/ContainerHub/docs/windows-agentic-loop.md) |
| Build-matrix entry fields, sanitizer env vars, cycling order, full-matrix sweeps | [`agentic-loop-build-matrix.md`](../../third_party/ContainerHub/docs/agentic-loop-build-matrix.md) |
| What a consumer owns, what stays upstream, and the `- [ ]` / `- [b]` / `- [x]` backlog protocol | [`templates/README.md`](../../third_party/ContainerHub/shared/agentic-loop/templates/README.md) |

## What this directory owns

| File | Purpose |
| --- | --- |
| [`AgenticLoop.config.json`](AgenticLoop.config.json) | Engine choice, model IDs, cadences, timeouts, build matrix, build/test/quality commands |
| [`Invoke-AgenticLoop.ps1`](Invoke-AgenticLoop.ps1) | Windows runner — resolves the module, loads the config, calls `Invoke-AgenticLoop` |
| [`Run-AgenticLoop.sh`](Run-AgenticLoop.sh) | Linux runner — sources the library, maps flags onto the env vars it reads, calls `run_agentic_loop` |
| [`prompts/planner-overlay.md`](prompts/planner-overlay.md) | Project delta appended to ContainerHub's shared planner system prompt |
| [`prompts/executor-overlay.md`](prompts/executor-overlay.md) | Project delta appended to ContainerHub's shared executor system prompt |

Both runners are the upstream templates with only their header comment and the
loop name changed. Keep them that way: no prompt text and no build-config list
belongs in a runner — hard-coding either is what let the Windows and Linux
copies drift apart once already.

The `opencode` engine additionally uses [`.opencode/agents/`](../../.opencode/agents)
and [`.opencode/commands/`](../../.opencode/commands) at the repo root, which
opencode discovers by itself. The commands give the OpenCode TUI `/plan`,
`/execute`, `/build <preset>`, `/test` and `/quality` for driving single loop
phases interactively. OpenCode itself is installed via `scoop install opencode`
(or `npm install -g opencode-ai`) and authenticated once with
`opencode auth login` — the upstream prerequisites section covers the other
tools but does not name that auth step.

## Running it

```pwsh
pwsh -File .\scripts\agentic-loop\Invoke-AgenticLoop.ps1
```

```bash
./scripts/agentic-loop/Run-AgenticLoop.sh
```

`-Engine`, `-DryRun`, `-MaxIterations`, `-SkipBuild`, `-SkipTests`,
`-SkipQuality`, `-PlannerOnly`, `-ExecutorOnly` (and their `--kebab-case`
equivalents on the Bash side) map one-to-one onto the module parameters
documented in
[`windows-agentic-loop.md`](../../third_party/ContainerHub/docs/windows-agentic-loop.md).

## What this repo configures

The config also restates several upstream defaults on purpose, to pin them
against upstream drift — the bullets below say which values actually deviate.
The key-by-key reference is upstream in
[`windows-agentic-loop.md`](../../third_party/ContainerHub/docs/windows-agentic-loop.md).

- `engine: "claude"` (upstream default: `opencode`), with
  `plannerModel: claude-opus-5`, `plannerFallbackModel: claude-fable-5` for
  when Opus is overloaded, and `executorModel: claude-sonnet-5`. The
  `opencode` engine stays configured as an alternative (GLM 5.2 planner,
  DeepSeek v4 Flash executor).
- `fullMatrixEveryNIterations: 5` (default 0 = no sweeps) — the full-matrix
  sweep cadence. The other cadences (`buildEveryNTasks: 3`,
  `qualityEveryNTasks: 5`, `refactorEveryNIterations: 3`) pin the defaults.
- `plannerTimeoutSeconds: 1800`, `executorTimeoutSeconds: 3600` (default 0 =
  no timeout) — per-role wall-clock timeouts; the executor value also covers
  the build fixer. The config's generic `timeoutSeconds: 1200` is dead while
  both per-role values are set: `Get-AgentTimeoutForRole` only falls through
  to it for a role whose own timeout is 0.
- `agentRetryDelaySeconds: 30` (default 20). `agentRetries: 2`,
  `fixBuildFailures: true` and `maxConsecutiveBuildFailures: 3` pin the
  defaults.
- `plannerAllowedTools: "Read Glob Grep Edit(BACKLOG.md) Bash(git:*) PowerShell(git:*)"`
  — the planner reads the whole tree but writes only the backlog. The executor
  runs with `bypassPermissions` (the upstream default, restated because it is
  a trust statement: this is a trusted repo).
- `build.windowsTestCommand` / `build.linuxTestCommand` (no upstream default)
  — the fallback test command for any matrix entry without its own
  `testCommand`. Both point at the debug build's ctest; see the matrix bullet
  below for the consequence.
- `logging.logDir: logs/agentic-loop` (pins the default) — one timestamped log
  per run, carrying all agent, build, test and quality output.
- The `buildMatrix` maps this repo's presets to their build dirs and `ctest`
  commands: `clangcl-debug` (ASAN), `clangcl-profile`, `clangcl-release` on
  Windows; `linux-debug-asan-clang`, `linux-debug-tsan-clang`,
  `linux-profile-clang`, `linux-release-clang` on Linux. The release entries
  carry `testCommand: null`, which does **not** skip tests — the module falls
  back to `build.windowsTestCommand` / `build.linuxTestCommand`, and both are
  set here, so the release lane currently re-runs the debug build's ctest.
  `null` only means "no tests" in a config that leaves both fallback keys
  unset; if the release lanes are meant to be build-only, those two keys must
  be nulled as well.

## Build and quality commands

Windows builds never run CMake on the host. They go through the Stevedore
container script
[`scripts/windows/Build-Windows-Container.ps1`](../windows/Build-Windows-Container.ps1),
whose configuration name maps to a preset via
[`Build-Windows.config.psd1`](../windows/Build-Windows.config.psd1). Stevedore
setup and service recovery:
[`windows-stevedore-and-docker.md`](../../third_party/ContainerHub/docs/windows-stevedore-and-docker.md).

Linux builds go through
[`scripts/linux/cmake-configure-build.sh`](../linux/cmake-configure-build.sh),
natively or in a Rancher Desktop container.

The quality gate on Windows is
`Build-Windows.ps1 -Configurations clangcl-debug -SkipBuild -SkipTests -SkipPerfTests -SkipMsix`
(clang-tidy + cmake-format over the clangcl-debug build — keep the
`-Configurations` flag when running it by hand, because `Build-Windows.ps1`
defaults to all five presets without it); on Linux it is
[`scripts/linux/run-static-analysis-format.sh`](../linux/run-static-analysis-format.sh).

## The prompt overlays

`--append-system-prompt-file` takes exactly one file, so the module concatenates
ContainerHub's shared role prompt with this repo's overlay into a temp file at
startup rather than making the consumer keep a whole copy of the shared prompt.
The overlays are wired through `engines.claude.plannerPromptOverlayFile` /
`engines.claude.executorPromptOverlayFile` — two keys the upstream config-key
table does not document yet (it only lists the older full-override
`plannerPromptFile` / `executorPromptFile` shape). The overlays therefore carry
only what is true of *this* renderer:

- [`prompts/planner-overlay.md`](prompts/planner-overlay.md) — which `BACKLOG.md`
  heading a task belongs under, which preset a task should name (`clangcl-debug`
  for iteration, `clangcl-profile` for benchmarks, `clangcl-release` for
  packaging), and the conventions a proposed task must never violate: no
  exceptions, `PipelineBuilder` for pipelines, VMA for buffer/image memory.
- [`prompts/executor-overlay.md`](prompts/executor-overlay.md) — the exact build
  commands, why a container build needs a container-native `--build-dir`, why
  Windows tests run the delivered executable from the repo root instead of
  `ctest --test-dir` (the container build's CTest metadata carries container
  paths), and the stale-SPIR-V trap after touching a shader.

They reach the agent on Windows only — an upstream defect, not a design
choice. The Bash half
(`third_party/ContainerHub/linux/scripts/lib/agentic-engines.sh`) still reads
only `plannerPromptFile` / `executorPromptFile`, so a Linux run silently gets
no project system prompt at all. Until `agentic-engines.sh` learns the two
overlay keys, do not expect a Linux run to honour anything the overlays say.
