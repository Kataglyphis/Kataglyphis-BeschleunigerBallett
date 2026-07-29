# AGENTS.md

Guidance for AI agents and new contributors working in Kataglyphis-BeschleunigerBallett
(a Vulkan/OpenGL graphics-engine playground: C++23/C17, CMake presets, optional Rust).

## AI Agent Workflow (Windows)

**When building on Windows, always use Stevedore (Docker) via the container build
script.** Never invoke CMake, Ninja or MSBuild directly on the host. The build
produces binaries inside the container; to run them you must extract them from
the container onto the host first.

Concrete workflow for the `clangcl-debug` configuration:

1. **Build:**
   ```pwsh
   pwsh -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows-Container.ps1 `
     -Configurations "clangcl-debug" -SkipTests -SkipPerfTests -SkipMsix
   ```
   This produces `build-clangcl-debug/GraphicsEngine.exe` (and other artifacts)
   inside the reusable container `bb-build-persistent`.

2. **Copy the binary to the host:**
   ```pwsh
   docker cp bb-build-persistent:C:\ws\build-clangcl-debug\bin\GraphicsEngine.exe .\
   ```

3. **Run on the host** (containers have no swapchain):
   ```pwsh
   .\GraphicsEngine.exe
   ```
   Debug builds need the Vulkan validation layers on the host (see "Running on
   the Host" below). Profile/Release builds run without them.

For other configurations, adjust `-Configurations` and the binary path
accordingly (`build-clangcl-profile\bin\`, `build-clangcl-release\bin\`).

---

## Build System Overview

Everything is driven by `CMakePresets.json`. Do not invent ad-hoc CMake command lines;
pick a preset. `cmake --list-presets` shows what is available per platform.

### Windows configurations (Build-Windows.ps1)

`Scripts/Windows/Build-Windows.ps1` is the single entry point for Windows builds. Its
`-Configurations` names (comma-separated) map to presets and build directories via
`Scripts/Windows/Build-Windows.config.psd1`:

| Configuration | Preset | Build dir | What you get |
| --- | --- | --- | --- |
| `clangcl-debug` | `x64-ClangCL-Windows-Debug` | `build-clangcl-debug` | Debug + **ASAN** + UBSan, FuzzTest fuzzing mode |
| `clangcl-profile` | `x64-ClangCL-Windows-Profile` | `build-clangcl-profile` | RelWithDebInfo + tests/benchmarks |
| `clangcl-release` | `x64-ClangCL-Windows-Release` | `build-clangcl-release` | Release + CPack packaging |
| `msvc-debug` / `msvc-release` | `x64-MSVC-Windows-*` | `build-msvc-debug` | MSVC (cl) builds, optional steps |

There is also an `x64-ClangCL-Windows-Debug-ASan` preset
(AddressSanitizer without the fuzzing-mode extras; not wired into
`Build-Windows.config.psd1`). It has build and test presets. Test presets
exist for the Debug and Profile configurations (`test-<configure-preset>`);
the plain-Clang `x64-Clang-Windows-{Debug,Profile,RelWithDebInfo}` presets
were removed in 2026-07 as unused duplicates of the ClangCL set.
`x64-Clang-Windows-Release` stays: the `windows-clang-release-wix` package
preset builds on it.

Typical full sweep (ASAN debug, profile, release):

```pwsh
pwsh -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows.ps1 `
  -Configurations "clangcl-debug,clangcl-profile,clangcl-release" `
  -SkipFormat -SkipTidy -SkipTests -SkipPerfTests -SkipMsix
```

### Sanitizer semantics (do not guess — this is how it actually works)

- Sanitizer flags are applied **only to the Debug configuration**
  (`$<$<CONFIG:Debug>:...>` in `cmake/Sanitizers.cmake`). Profile/Release builds are
  never sanitized.
- ASAN and UBSan default **ON** for Debug builds (Linux GCC/Clang, MSVC, clang-cl);
  see `myproject_default_debug_sanitizers` in `cmake/ProjectOptions.cmake`.
- Linux TSan presets (`linux-debug-tsan-clang` / `linux-debug-tsan-GNU`) set
  `myproject_ENABLE_SANITIZER_THREAD=ON` and force
  `myproject_ENABLE_SANITIZER_ADDRESS=OFF` (TSan and ASAN are mutually exclusive —
  `Sanitizers.cmake` drops TSan if ASAN is on). **These are the only builds that
  detect data races.**
- **clang-cl does not support TSan** (`x86_64-pc-windows-msvc`):
  `Sanitizers.cmake` warns and drops the request, producing a plain Debug binary.
  A Windows `clangcl-tsan` preset existed for "preset parity" until 2026-07-20
  and was removed — it silently built a duplicate of `clangcl-debug`, cost ~185 s
  per full build, and its green runs read as evidence of race-freedom.
- The `USE_THREAD_SANITIZER` cache variable is **legacy plumbing consumed by
  nothing in CMake** — the effective switch is `myproject_ENABLE_SANITIZER_THREAD`.
- **On clang-cl, UBSan only works together with ASAN — never standalone.** With
  ASAN on, the UBSan handlers are folded into `clang_rt.asan_dynamic` (release
  CRT, `/MD`), and three places switch the whole Debug build to the release CRT,
  keyed on `myproject_ENABLE_SANITIZER_ADDRESS`: the `/MDd` strip and the
  `CMAKE_MSVC_RUNTIME_LIBRARY` override in `cmake/ProjectOptions.cmake`, plus the
  Rust-bridge `CXXFLAGS` in `Src/CMakeLists.txt`
  (`_myproject_configure_windows_rust_crate`). Standalone UBSan instead pulls
  `clang_rt.ubsan_standalone*`, which is built `MT_StaticRelease` (static CRT) —
  it can never link against this project's `/MD`/`/MDd` dependency mix; lld-link
  fails with `/failifmismatch` on `RuntimeLibrary`/`_ITERATOR_DEBUG_LEVEL`
  (verified 2026-07-16). So on Windows: enable UBSan only alongside ASAN, and
  turn both off together.

## Containerized Windows Builds (Stevedore)

Windows builds run inside the ContainerHub developer image
`ghcr.io/kataglyphis/kataglyphis_beschleuniger:winamd64` (clang-cl, CMake, Ninja,
Vulkan SDK, Rust, sccache — everything preinstalled). CI does exactly this
(`.github/workflows/Windows.yml`); locally use:

```pwsh
# Builds clangcl-debug,clangcl-profile,clangcl-release by default
pwsh -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows-Container.ps1
```

**All Windows-container knowledge lives in ContainerHub** — do not restate it
here. Two documents cover it:

- [`ExternalLib/Kataglyphis-ContainerHub/docs/windows-builds.md`](ExternalLib/Kataglyphis-ContainerHub/docs/windows-builds.md)
  — the image itself: build sequence, Stevedore setup, invariants.
- [`ExternalLib/Kataglyphis-ContainerHub/docs/windows-container-build-performance.md`](ExternalLib/Kataglyphis-ContainerHub/docs/windows-container-build-performance.md)
  — building *inside* it: **both transports and how to set each one up**
  (§ Transports), the reusable-container pattern and its safety rails, why
  sccache cannot cache a C++23 modules build, why a named volume cannot be a
  CMake build directory, the Windows path limit that silently truncates tar
  transfers, `docker exec` bypassing the entrypoint, and the wcifs teardown
  lock.

### Two transports — both supported

Sources get into the container either by **tar-pipe** (default) or by **bind
mount** (`-UseBindMount`). Both work; keep both.

| | no-change ninja | no-change wall |
| --- | --- | --- |
| tar-pipe + reusable container (default) | **9.6 s** | **44 s** |
| bind mount | 32.7 s | 159 s |

The bind mount is slower **on this Dev Drive host**: the build tree then lives
on D: and every ninja stat and object write crosses the `bindFlt` filter. That
result is host-specific — on a non-Dev-Drive volume or a smaller tree it can
invert, so measure before switching rather than trusting the default.

Bind mounting needs one elevated setup step plus a reboot, and both transports
must mount at the same in-container path (`C:\ws`) or CMake rejects the cache.
Setup, verification, revert and the reasoning:
[§ Transports](ExternalLib/Kataglyphis-ContainerHub/docs/windows-container-build-performance.md#transports-how-to-set-up-both).

The reusable PowerShell is upstream too
(`windows/scripts/modules/WindowsContainerBuild.Reuse.psm1`:
`Get-ReusableBuildContainer`, `Copy-IntoBuildContainer`,
`Copy-FromBuildContainer`, `Resolve-DockerExe`, `Get-ContainerIsolationArgs`,
`Test-ContainerBindMount`, `Remove-BuildContainerSafe`); this repo's script
imports it through `Scripts/Windows/Resolve-BuildModule.ps1` and keeps only
project-specific orchestration (build-directory names, `Build-Windows.ps1`
arguments, the cargo exclusions).

## Shaders: always compiled, never stale

GLSL under `Resources/Shaders/**` is compiled to `spv/*.spv` by
`Scripts/Windows/compile-shaders.ps1` (run by `Build-Windows.ps1` for **every**
configuration) with a runtime fallback in `ShaderHelper`. Both layers now
compare timestamps: a `.spv` is reused only when it is newer than its source
**and** every shared include. Until 2026-07-19 both merely checked whether the
file existed, so every shader edit after the first build was silently ignored
and the GPU ran stale SPIR-V. Full account, plus the fast
regenerate-without-rebuilding loop: [`docs/shader-build-pipeline.md`](docs/shader-build-pipeline.md).


Builds are **incremental** via a reusable container (`bb-build-persistent`):
the build tree never leaves it, so only sources go in and only executables +
logs come out. Measured 2026-07-19: **9.6 s** build step (44 s wall) for a
no-change rebuild, against 352-484 s when every build got a fresh container.
Use `-FreshContainer` to start clean — **a file deleted on the host keeps
building inside a reused container** until it is recreated (an image change
recreates it automatically).

The build **fails** if the container produced no executables, or if any it
produced did not reach the host. Both have happened silently.

Why this works and what does not, measured once and not repeated: the general
Windows-container findings are in
[ContainerHub](ExternalLib/Kataglyphis-ContainerHub/docs/windows-container-build-performance.md);
this repo's own numbers and wiring are in
[`docs/container-build-caching.md`](docs/container-build-caching.md).

## Rule: Reusable Work Belongs in ContainerHub

**Before writing a script, module, or doc here, ask whether another project
could use it. If yes, it goes into `ExternalLib/Kataglyphis-ContainerHub` and
this repo consumes it — never a copy.**

What belongs upstream:

- **PowerShell** that is not specific to this engine: container lifecycle,
  transfers, toolchain discovery, isolation settings, image handling. Add it to
  `windows/scripts/modules/` and consume it via
  `Scripts/Windows/Resolve-BuildModule.ps1` (ContainerHub first, vendored
  fallback second).
- **Knowledge about the container image or Windows containers in general** —
  build performance, platform traps, setup fixes. ContainerHub's `docs/` is the
  single home; link to it from here.
- **Anything learned the hard way** that is not about this renderer: write down
  the symptom, not just the fix, so the next person recognises it.

What stays here: engine code, shaders, this project's presets and build
orchestration (build-directory names, `Build-Windows.ps1` arguments,
project-specific exclusions), and `Resolve-BuildModule.ps1` itself — it is the
bootstrap that *finds* ContainerHub, so it cannot live inside it.

Worked example: the container-reuse work (2026-07) put seven functions in
`WindowsContainerBuild.Reuse.psm1` and two documents in ContainerHub's `docs/`,
while this repo kept only the tar-pipe orchestration for its own build
directories. Both repos are committed and pushed together, and the submodule
pin is bumped in the same change.

## Critical Invariant: Submodule Pins

Builds are only supported against the **recorded submodule gitlinks** — the commits CI
builds green. `git submodule update --checkout --recursive` restores every pin. If a
drifted submodule is what you actually want, update the gitlink AND fix the fallout in
the same change.

Known coupling to watch when bumping pins:

- `ExternalLib/FUZZTEST` pins its own Abseil LTS in
  `cmake/BuildDependencies.cmake`; this repo's `ABSL_TAG` in
  `ExternalLib/CMakeLists.txt` is declared first and wins, so it must stay >= the
  FuzzTest pin or configure fails with missing `absl::*` targets (observed:
  `absl::random_mocking_access`).

### PowerShell build modules (ContainerHub first, vendored fallback)

`Scripts/Windows/Build-Windows.ps1`, the run helpers, and the Pester tests resolve
PowerShell modules through `Scripts/Windows/Resolve-BuildModule.ps1`
(`Resolve-BuildModulePath` / `Import-BuildModule`): a module is imported from
`ExternalLib/Kataglyphis-ContainerHub/windows/scripts/modules/` when it exists
there (preferred — reusable scripts live upstream and are shared across
projects), otherwise from the vendored fallback **`Scripts/Windows/modules/`**.

The vendored directory holds only what ContainerHub's module refactor (commit
`b391a1d`) deleted upstream: `WindowsCMake`, `WindowsConfig`,
`WindowsClang`, `WindowsFormatting`, `WindowsTesting`, `WindowsWebDav`,
`WindowsMsix.Common`, `WindowsMsix.Signing` — plus `WindowsScripts.Shared`,
which the vendored modules import internally by sibling path (direct imports of
it still prefer the ContainerHub copy). `WindowsLogging.Common` was deleted:
its functions were folded into ContainerHub's `WindowsBuild.Common.psm1`.
If a module reappears upstream it wins automatically; if you improve a fallback
module, consider upstreaming it to ContainerHub and deleting the vendored copy
in the same change.

## Running on the Host (Windows)

Containers cannot present a swapchain — run the built binaries on the bare host,
from the **repo root** as working directory (`Scripts/Windows/run_clangcl_*.ps1`
wrap this). Verified 2026-07-17 on the AMD RX 9070 XT: all four clang-cl builds
render (~32 FPS ImGui overlay).

- **Debug builds require the Vulkan validation layers on the host** — without
  them the app dies at startup with exit code `-1073740791` (`0xC0000409`,
  vulkan.hpp assert after "Validation layers requested, but not available!").
  Install the Vulkan SDK (`winget install VulkanSDK`), or point `VK_LAYER_PATH`
  at a directory containing `VkLayer_khronos_validation.{dll,json}` (they can be
  extracted from the ContainerHub image under
  `C:\Users\ContainerAdministrator\scoop\apps\vulkan\current\Bin`).
  Profile/Release builds run without validation layers.
- The ASAN debug binary needs `clang_rt.asan_dynamic-x86_64.dll`; the build
  copies it next to `GraphicsEngine.exe`, and the run helpers set `ASAN_OPTIONS`
  with a **relative** `log_path` (an absolute `C:\...` path breaks ASAN option
  parsing at the drive-letter colon).

## Linux Builds

`Scripts/Linux/cmake-configure-build.sh` wraps configure+build
(`--preset linux-debug-clang`, `--build-dir build`, …). TSan presets:
`linux-debug-tsan-clang`, `linux-debug-tsan-GNU`. There is also an
`linux-debug-asan-clang` preset (AddressSanitizer + UBSan, used in CI and
fuzz-test integration). Coverage, ctest, and perf wrappers
live next to it. Vulkan SDK env can be injected with `--vulkan-setup-script`.
Run helpers (`run-debug.sh`, `run-profile.sh`, `run-release.sh`) and
`compile-shaders.sh` parallel the Windows equivalents.

## Testing

- C++ tests: `ctest --test-dir <build-dir> --output-on-failure` (add `-C Debug` for
  multi-config generators). `Build-Windows.ps1` runs them unless `-SkipTests`.
  Test presets exist for the Debug and Profile configurations
  (`test-<configure-preset>`).
- Benchmarks: `clangcl-profile` builds `perfTestSuite.exe`; run via
  `Build-Windows.ps1` without `-SkipPerfTests`.
- PowerShell module tests: Pester suites under `Scripts/Windows/tests/`.

**Adding tests is always in scope.** You do not need permission to improve
or extend the suites — a change that fixes behaviour should generally arrive
with a test that would have caught it. Prefer assertions that survive driver
and machine differences (structural pixel properties, invariants, ordering)
over exact-value comparisons, and make GPU-dependent tests skip themselves
when no adapter is present rather than fail. Ideas worth picking up live in
[`BACKLOG.md`](BACKLOG.md), alongside the sized commitments.

**Formatting and static analysis.** clang-format/clang-tidy/cmake-format
commands, the host gotchas (LLVM is not on `PATH`; the container-generated
`compile_commands.json` points at `C:/ws`; C++23 module TUs are skipped by
clang-tidy), and the suggested cadence live in
[`docs/code-quality.md`](docs/code-quality.md). Note that
`Build-Windows-Container.ps1` hard-codes `-SkipTidy`, so
containerized builds never run clang-tidy (clang-format is still
attempted unless `-SkipFormat` is also passed).

**Run more than the debug loop periodically.** `clangcl-debug` is the fast
default, but `clangcl-profile` (optimized, and the only configuration where
benchmarks mean anything) and a synchronization-validation pass each catch
classes of problem the debug loop cannot. See [`BACKLOG.md`](BACKLOG.md) for
what each one is for.

**There is no Windows ThreadSanitizer.** clang-cl does not support
`-fsanitize=thread` on this target, so a Windows "TSan" build is just a debug
build and a green run proves nothing about data races. The `clangcl-tsan`
preset that claimed otherwise was removed on 2026-07-20 — it cost ~185 s per
full build to produce a duplicate of `clangcl-debug`. Use the Linux
`linux-debug-tsan-clang` preset, which CI runs, for race detection.

## Code Conventions (C++ engine)

- **Exceptions are disabled project-wide** (`/EHs-`, `-fno-exceptions`,
  `VULKAN_HPP_NO_EXCEPTIONS`): vulkan.hpp calls return `ResultValue`;
  `throw`/`try` will not compile. `ASSERT_VULKAN(val, "msg")` logs critical
  and aborts — use it on creation/allocation calls only.
- Graphics pipelines are built via `kataglyphis.vulkan.pipeline_builder`
  (vulkan_base/PipelineBuilder) — do not hand-roll the create-info chain.
- Buffer/image memory goes through VMA (allocator owned by `VulkanDevice`);
  `VulkanBuffer`/`VulkanImage` are move-only with destructor release
  (`cleanUp()` remains for explicit early teardown and is idempotent).
- A `VkPipelineCache` persists to `pipeline_cache/kataglyphis_pipeline.cache`
  (gitignored, written on graceful shutdown only).
- Per-unit verification pattern (container build -> direct test exe ->
  validation run) and the log of what changed and why:
  [`docs/cpp-renderer-improvements.md`](docs/cpp-renderer-improvements.md).
  Do not restate it here — that document is the source of truth.

## Agentic Loop (OpenCode)

An autonomous planner/executor loop built on [OpenCode](https://opencode.ai)
continuously improves the engine. The **planner** (GLM 5.2 — expensive,
powerful) analyzes the codebase and writes detailed task entries to
`BACKLOG.md`. The **executor** (DeepSeek v4 Flash — cheap, fast) drains the
queue one task at a time, building and testing as it goes. The queue must be
fully drained before the planner adds new tasks.

**Reusable logic lives in ContainerHub's `WindowsAgenticLoop.Common` module.**
The project script is a thin consumer. See the ContainerHub module docs and
[`Scripts/AgenticLoop/README.md`](Scripts/AgenticLoop/README.md) for details.

### Files

| What | Where | Purpose |
|------|-------|---------|
| **Module** (reusable) | `ExternalLib/Kataglyphis-ContainerHub/windows/scripts/modules/WindowsAgenticLoop.Common.psm1` | Building blocks: `Invoke-OpenCode`, logging, platform detection, git helpers, loop orchestration |
| **Module docs** | `ExternalLib/Kataglyphis-ContainerHub/docs/windows-agentic-loop.md` | API reference, usage examples, PS 5.1 notes |
| **Project script** | `Scripts/AgenticLoop/Run-AgenticLoop.ps1` | Thin wrapper — imports module, loads config, calls `Invoke-AgenticLoop` |
| **Config** | `Scripts/AgenticLoop/AgenticLoop.config.json` | Model IDs, intervals, build config cycles |
| **Planner agent** | `.opencode/agents/planner.md` | Planner system prompt (GLM 5.2) |
| **Executor agent** | `.opencode/agents/executor.md` | Executor system prompt (DeepSeek v4 Flash) |
| **Slash commands** | `.opencode/commands/{plan,execute,build,test,quality}.md` | Interactive commands for OpenCode TUI |
| **Linux script** | `Scripts/AgenticLoop/Run-AgenticLoop.sh` | Bash equivalent for Linux / Rancher Desktop |

### Usage

```pwsh
# Windows — full loop (requires PowerShell 7+)
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Scripts\AgenticLoop\Run-AgenticLoop.ps1

# Dry run (test config, no opencode calls)
pwsh .\Scripts\AgenticLoop\Run-AgenticLoop.ps1 -DryRun

# Planner only (add tasks to BACKLOG.md)
pwsh .\Scripts\AgenticLoop\Run-AgenticLoop.ps1 -PlannerOnly

# Executor only (drain existing queue)
pwsh .\Scripts\AgenticLoop\Run-AgenticLoop.ps1 -ExecutorOnly
```

Builds cycle through `clangcl-debug`, `clangcl-profile`, `clangcl-release`
(Windows) or the Linux equivalents after every N completed tasks. Tests run
after each build. clang-tidy + cmake-format run every M tasks. The planner
adds refactor-focused tasks every R iterations. Logs go to
`logs/agentic-loop/`.

## Docs

Each topic has exactly one home; link, do not copy. Reusable topics live in
ContainerHub (see the rule above), project-specific ones here.

| Where | Owns |
| --- | --- |
| `README.md` | Repo-level orientation, feature highlights |
| `BACKLOG.md` | **All** open work: sized commitments, then unsized ideas and recurring chores |
| `AGENTS.md` (this file) | How to build/test/run here, invariants, code conventions |
| `docs/cpp-renderer-improvements.md` | C++ engine change log + verification pattern |
| `docs/model-loading.md` | Model-loading architecture: the two loaders, async parse/upload split, multi-mesh MeshRange flow |
| `docs/webgpu-renderer-roadmap.md` | Rust WebGPU renderer status per feature |
| `docs/webgpu-gltf-rust-plan.md` | Original WebGPU + glTF Rust renderer plan (milestones 1–5) |
| `docs/shader-sharing.md` | WGSL -> SPIR-V/GLSL pipeline between both renderers |
| `docs/webgpu-srgb-audit.md` | Colour-space decisions (no known deviations) |
| `docs/code-quality.md` | clang-format / clang-tidy / cmake-format commands + cadence |
| `docs/shader-build-pipeline.md` | GLSL→SPIR-V build step, staleness rules, fast shader iteration |
| `docs/container-build-caching.md` | Container transport, sccache volume, incremental-build options |
| `docs/gpu-golden-testing.md` | GPU golden test suites, skip-without-GPU behavior, host verification loop |
| `docs/path-tracing.md` | Path-tracing mode: pipeline shape, estimator, NEE, accumulation |
| `docs/renderer-bounds-invariant.md` | WebGPU renderer bounds invariant |
| `docs/LICENSES-README.md` | Third-party license documentation |
| `Scripts/AgenticLoop/README.md` | Agentic loop (OpenCode planner/executor) architecture, config, usage |
| `docs/source/` | Sphinx pages (`getting_started.md`, `documentation_workflow.md`, `webgpu_demo.md`, `wsl2_vulkan.rst`) |

- Keep docs, scripts, and presets aligned: when you change build behavior, update
  `README.md`, `docs/source/getting_started.md`, and this file in the same change.
