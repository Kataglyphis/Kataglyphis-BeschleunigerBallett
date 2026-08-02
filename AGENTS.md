# AGENTS.md

Guidance for AI agents and new contributors working in Kataglyphis-BeschleunigerBallett
(a Vulkan graphics-engine playground with a Rust WebGPU sibling renderer:
C++23/C17, CMake presets, optional Rust).

## Start here: routing

Find your task, do the first action, read the doc. Everything below expands on
these; the [Docs](#docs) table at the end is the full ownership index.

| If you are doing this | Start with |
| --- | --- |
| Building anything on Windows | `Build-Windows-Container.ps1` — see [Fast path](#fast-path-windows-build-run-verify). Never invoke CMake/Ninja/MSBuild on the host. |
| Running the engine / seeing pixels | `Scripts/Windows/run_clangcl_*.ps1` from the **repo root** — see [Running on the Host](#running-on-the-host-windows) |
| Changing a shader | Edit `Resources/ShadersSlang/*.slang` + `shader-manifest.json`, run `compile-slang-shaders.ps1`, run one golden. No C++ rebuild. [`docs/shader-build-pipeline.md`](docs/shader-build-pipeline.md) |
| Adding or changing a test | `Test/commit/VulkanEngine/` (CPU + GPU golden), `Test/fuzz/`, `Test/perf/`. Always in scope — see [Testing](#testing) |
| Touching render passes, barriers, frames-in-flight | Golden suites on the host GPU **and** `Run-SyncValidation.ps1` — [`docs/gpu-golden-testing.md`](docs/gpu-golden-testing.md) |
| Refactoring the renderer / device path | The per-unit verification loop in [`docs/gpu-golden-testing.md`](docs/gpu-golden-testing.md); log the change in [`docs/cpp-renderer-improvements.md`](docs/cpp-renderer-improvements.md) |
| "My build produced nothing" / "my deleted file still builds" | [Container reuse and delivery](#containerized-windows-builds-stevedore) — `-FreshContainer`, and the delivery check that fails the build |
| Writing a script, module, or general-purpose doc | Probably belongs upstream — [Rule: Reusable Work Belongs in ContainerHub](#rule-reusable-work-belongs-in-containerhub) |
| Pushing and expecting CI to tell you something | Windows and ARM lanes are **opt-in per commit** — see [What CI runs](#what-ci-runs-and-what-it-does-not) |
| Changing the Rust WebGPU renderer | `ExternalLib/Kataglyphis-RustProjectTemplate/crates/webgpu_renderer` — [`docs/webgpu-renderer-roadmap.md`](docs/webgpu-renderer-roadmap.md) |

### Repo map

| Path | What lives there |
| --- | --- |
| `Src/GraphicsEngineVulkan/` | The C++ Vulkan engine (`app`, `renderer`, `vulkan_base`, `memory`, `scene`, `gui`, `window`, `common`, `util`) |
| `Src/shared/`, `Src/KomputePlayground/` | Renderer-agnostic frontend/scene/imgui/util code; the Kompute compute playground |
| `Test/commit/VulkanEngine/` | The main gtest suite (`commitTestSuite.exe`) — CPU suites plus `GoldenRender.*` / `Integration.*` |
| `Test/compile/`, `Test/fuzz/`, `Test/perf/` | Compile-time checks, FuzzTest targets, Google Benchmark suite (`perfTestSuite`) |
| `Resources/ShadersSlang/` | All shaders (Slang) + `shader-manifest.json`; compiled output under `build/` is gitignored |
| `ExternalLib/Kataglyphis-RustProjectTemplate/crates/` | The Rust side, incl. `webgpu_renderer` and `gui` |
| `ExternalLib/Kataglyphis-ContainerHub/` | The submodule that owns every reusable script, module and doc (see the rule below) |
| `Scripts/Windows/`, `Scripts/Linux/`, `Scripts/AgenticLoop/` | Thin project wrappers over ContainerHub drivers + this project's payload |
| `cmake/` | `ProjectOptions.cmake` (exceptions, CRT, sanitizer defaults), `Sanitizers.cmake`, packaging |

---

## Fast path: Windows build, run, verify

**When building on Windows, always use Stevedore (Docker) via the container
build script.** Never invoke CMake, Ninja or MSBuild directly on the host — the
host toolchain is not what CI builds with, and host `cmake` cannot even read
this repo's presets (see below).

1. **Build** (`clangcl-debug` shown; the script defaults to all three clang-cl
   configurations):
   ```pwsh
   pwsh -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows-Container.ps1 `
     -Configurations "clangcl-debug"
   ```
   The build runs inside the reusable container `bb-build-persistent` and
   **streams the finished build trees back into the working tree** — no
   `docker cp` step is needed. Tests are skipped by default; pass `-RunTests`
   to run the CPU suites in-container.

2. **The artifacts are already on the host** at the build-directory root:
   `build-clangcl-debug\GraphicsEngine.exe`, `commitTestSuite.exe`, the fuzz
   executables, `compile_commands.json`, and `logs\`. (`bin\` holds only the
   copied ASAN runtime DLL — the executables are one level up.) Other
   configurations land in `build-clangcl-profile\` and `build-clangcl-release\`.

3. **Run on the host** — containers have no swapchain, and the working
   directory must be the **repo root** (`Resources/` is loaded via cwd-relative
   paths):
   ```pwsh
   .\Scripts\Windows\run_clangcl_debug.ps1        # ctest + fuzz exes + launch
   .\Scripts\Windows\run_clangcl_profile.ps1
   .\Scripts\Windows\run_clangcl_release.ps1
   ```
   Debug builds need the Vulkan validation layers installed on the host; see
   [Running on the Host](#running-on-the-host-windows). Profile/Release do not.

4. **If you changed rendering or the device path**, a green build proves
   nothing about behaviour — the GPU suites *skip* in the container. Run them
   on the host and follow the verification loop in
   [`docs/gpu-golden-testing.md`](docs/gpu-golden-testing.md).

---

## Build System Overview

Everything is driven by `CMakePresets.json`. Do not invent ad-hoc CMake command
lines; pick a preset.

> **Trap:** `cmake --list-presets` does **not** work on this host. The host
> CMake (3.29) cannot read `CMakePresets.json` (`"version": 10`,
> `cmakeMinimumRequired` 4.1) and fails with
> `CMake Error: Could not read presets ... Unrecognized "version" field`
> (verified 2026-08-02). Only the container's newer CMake can. Read the file, or
> the table below — that error is not a broken checkout.

### Windows configurations (Build-Windows.ps1)

`Scripts/Windows/Build-Windows.ps1` is the single entry point for Windows builds
(the container script invokes it inside the image). Its `-Configurations` names
(comma-separated) map to presets and build directories via
`Scripts/Windows/Build-Windows.config.psd1`:

| Configuration | Preset | Build dir | What you get |
| --- | --- | --- | --- |
| `clangcl-debug` | `x64-ClangCL-Windows-Debug` | `build-clangcl-debug` | Debug + **ASAN** + UBSan, FuzzTest fuzzing mode |
| `clangcl-profile` | `x64-ClangCL-Windows-Profile` | `build-clangcl-profile` | RelWithDebInfo + tests/benchmarks |
| `clangcl-release` | `x64-ClangCL-Windows-Release` | `build-clangcl-release` | Release + CPack packaging |
| `msvc-debug` / `msvc-release` | `x64-MSVC-Windows-*` | `build-msvc-debug` | MSVC (cl) builds, optional steps |

There is also an `x64-ClangCL-Windows-Debug-ASan` preset (AddressSanitizer
without the fuzzing-mode extras; not wired into `Build-Windows.config.psd1`).
Test presets (`test-<configure-preset>`) exist for exactly three configurations:
Debug, Debug-ASan and Profile. The plain-Clang
`x64-Clang-Windows-{Debug,Profile,RelWithDebInfo}` presets were removed in
2026-07 as unused duplicates of the ClangCL set. `x64-Clang-Windows-Release`
stays: the `windows-clang-release-wix` package preset builds on it.

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
(`.github/workflows/Windows.yml`).

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

What you need in hand to not get burned:

- Default transport is a **tar-pipe into the reusable container
  `bb-build-persistent`**; `-UseBindMount` opts into a bind mount instead
  (measured slower on this Dev Drive host — measure before switching).
- **A file deleted on the host keeps building in a reused container** — sources
  are overwritten in place, never pruned. Use `-FreshContainer` after deleting
  files, and after ANY C++23 module-interface change (see the fresh-container
  rule in [`docs/gpu-golden-testing.md`](docs/gpu-golden-testing.md)).
- **A green build is not proof anything was produced or delivered.** Both halves
  have failed silently here. The script compares the executables present in the
  container against those that reached the host and **fails the build** if the
  container produced none, or if any did not arrive. Do not "fix" that check.
- The script takes `-Configurations` (comma-separated), **not** `-Preset`.
- Reset a wedged incremental build with `docker rm -f bb-build-persistent`.

This repo's measured numbers, the `KATAGLYPHIS_KEEP_BUILD_ROOT=1` contract, the
`cargo/` exclusions and the rest of the wiring:
[`docs/container-build-caching.md`](docs/container-build-caching.md).

## Linux Builds

`Scripts/Linux/cmake-configure-build.sh` wraps configure+build
(`--preset linux-debug-clang`, `--build-dir build`, …). TSan is selected by
preset only (`linux-debug-tsan-clang`, `linux-debug-tsan-GNU`) — there is no
script flag for it; `--use-thread-sanitizer` errors out on purpose rather than
silently no-op'ing. There is also a `linux-debug-asan-clang` preset
(AddressSanitizer + UBSan, used in CI and fuzz-test integration). Coverage,
ctest, perf, static-analysis and wasm wrappers live next to it; Vulkan SDK env
can be injected with `--vulkan-setup-script`.

The Slang precompile is `cmake-configure-build.sh`'s pre-build hook and a
failure there is **fatal** (use `--allow-prebuild-failure` only deliberately —
a silent `|| warn` once left CI green with no SPIR-V at all).

### Running the Linux build locally (Rancher Desktop)

Two things that will bite you locally, both documented with the full recipe in
[ContainerHub § Persisting the cargo cache](ExternalLib/Kataglyphis-ContainerHub/docs/rancher-desktop-linux-containers.md#persisting-the-cargo-cache):
pass `--build-dir /tmp/...` (a build dir on the bind-mounted host tree breaks
FetchContent renames and cargo cleanup), and `--cargo-cache-dir` at a named
volume so Rust dependencies survive the container.

## Shaders: Slang (unified SPIR-V + WGSL)

All shaders are Slang under `Resources/ShadersSlang/`, compiled ahead of time
to **SPIR-V** for the C++ Vulkan renderer and **WGSL** for the Rust WebGPU
renderer. The shader list is data, in `shader-manifest.json` — add or retarget a
shader by editing the JSON, never by editing the compile scripts. Compiled
output is gitignored, so a fresh clone must run a compile script before the
engine has anything to load. Build step, staleness rules, and fast shader
iteration: [`docs/shader-build-pipeline.md`](docs/shader-build-pipeline.md);
sharing shader code between the two renderers:
[`docs/shader-sharing.md`](docs/shader-sharing.md).

## Rule: Reusable Work Belongs in ContainerHub

**Before writing a script, module, or doc here, ask whether another project
could use it. If yes, it goes into `ExternalLib/Kataglyphis-ContainerHub` and
this repo consumes it — never a copy.**

What belongs upstream:

- **PowerShell** that is not specific to this engine: container lifecycle,
  transfers, toolchain discovery, isolation settings, image handling. Add it to
  `windows/scripts/modules/`.
- **Bash** likewise, in `linux/scripts/lib/` (consumer-facing libraries) or
  `linux/scripts/01-core/` (primitives: logging, retry, verified downloads,
  uv/python, parallelism). Source it by relative path and fail loudly when the
  submodule is missing.
- **Knowledge about the container image or Windows containers in general** —
  build performance, platform traps, setup fixes. ContainerHub's `docs/` is the
  single home; link to it from here.
- **Anything learned the hard way** that is not about this renderer: write down
  the symptom, not just the fix, so the next person recognises it.

What stays here: engine code, shaders, this project's presets, and the
*payload* the shared drivers execute (build-directory names,
`Build-Windows.ps1` arguments, project-specific exclusions, the Slang
precompile hook) — plus `Resolve-BuildModule.ps1` itself, the bootstrap that
*finds* ContainerHub and therefore cannot live inside it.

### The wrapper map

Almost every script under `Scripts/` is a thin consumer. **When you need to know
what one actually does, read the upstream driver, not the wrapper** — the
wrapper only supplies this project's payload.

| This repo | Upstream driver (in `ExternalLib/Kataglyphis-ContainerHub/`) |
| --- | --- |
| `Scripts/Windows/Build-Windows-Container.ps1` (121 lines) | `windows/scripts/modules/WindowsContainerBuild.Reuse.psm1` → `Invoke-ContainerBuild` (+ `Get-ReusableBuildContainer`, `Copy-IntoBuildContainer`, `Copy-FromBuildContainer`, `Resolve-DockerExe`, `Get-ContainerIsolationArgs`, `Test-ContainerBindMount`, `Get-SccacheContainerEnv`, `Remove-BuildContainerSafe`) |
| `Scripts/Linux/cmake-configure-build.sh` (40 lines) | `linux/scripts/lib/cmake-build.sh` |
| `Scripts/Windows/run_clangcl_{profile,release}.ps1` | `windows/scripts/modules/WindowsAppRunner.Common.psm1` → `Invoke-AppRun`, `Resolve-AppExecutablePath` |
| `Scripts/Linux/run-{debug,profile,release}.sh` | `linux/scripts/lib/app-runner.sh` (the Bash twin of the above) |
| `Scripts/Linux/run_static_analysis_format.sh` | `linux/scripts/lib/code-quality.sh` |
| `Scripts/Linux/build-coverage-{gcovr,llvm}.sh` | `linux/scripts/lib/coverage.sh` |
| `Scripts/Linux/wasm-size-budget.sh` / `Scripts/Test-WasmSizeBudget.ps1` | `linux/scripts/lib/wasm-opt.sh` / `windows/scripts/modules/WindowsWasmOpt.Common.psm1` |
| `Scripts/Linux/run-cargo-tests.sh` | `linux/scripts/02-toolchain/rust/cargo_test.sh` |
| `Scripts/Windows/Run-SyncValidation.ps1` | `windows/scripts/modules/WindowsVulkanValidation.Common.psm1` |
| `Scripts/AgenticLoop/Run-AgenticLoop.{ps1,sh}` | `windows/scripts/modules/WindowsAgenticLoop.Common.psm1` / `linux/scripts/lib/agentic-loop.sh` |

`run_clangcl_debug.ps1` is the exception: it keeps its own flow because it
orchestrates CTest and the fuzz executables before launching — but it still
takes `Resolve-AppExecutablePath` from `WindowsAppRunner.Common`.

### PowerShell module resolution (ContainerHub first, vendored fallback)

`Build-Windows.ps1`, the run helpers and the Pester tests resolve PowerShell
modules through `Scripts/Windows/Resolve-BuildModule.ps1`
(`Resolve-BuildModulePath` / `Import-BuildModule`): a module is imported from
`ExternalLib/Kataglyphis-ContainerHub/windows/scripts/modules/` when it exists
there (preferred), otherwise from the vendored fallback
**`Scripts/Windows/modules/`**.

The vendored directory holds only the two genuinely project-specific modules:

- `WindowsClang.Common` — hard-codes the `Src` root and the `import kataglyphis`
  module-TU skip.
- `WindowsTesting.Common` — test-exe discovery + ASAN env handling for this
  repo's suites.

Everything else was upstreamed to ContainerHub on 2026-08-02
(`WindowsCMake.Common`, `WindowsConfig.Common`, `WindowsFormatting.Common`,
`WindowsWebDav.Common`, `WindowsMsix.Common`, `WindowsMsix.Signing`;
`WindowsScripts.Shared` was already upstream-only). Note in particular that
`Get-CompileCommandsDatabase` (the `ninja -t compdb` fallback) lives in upstream
`WindowsCMake.Common`, **not** in `WindowsClang.Common`. If a module reappears
upstream it wins automatically; if you improve a fallback module, consider
upstreaming it and deleting the vendored copy in the same change.

### Shipping a change that spans both repos

Both repos are committed and pushed together, ContainerHub **first** (CI
resolves its composite actions at `@main`), and the submodule pin is bumped in
the same change.

**Adopting any of this in another project** — the loop, both container flows,
the launchers, the CI actions — is a checklist upstream:
[`adopting-in-a-new-project.md`](ExternalLib/Kataglyphis-ContainerHub/docs/adopting-in-a-new-project.md).
Read it before hand-rolling equivalents elsewhere.

## Critical Invariant: Submodule Pins

Builds are only supported against the **recorded submodule gitlinks** — the commits CI
builds green. `git submodule update --checkout --recursive` restores every pin. If a
drifted submodule is what you actually want, update the gitlink AND fix the fallout in
the same change.

Known coupling to watch when bumping pins:

- `ExternalLib/FUZZTEST` pins its own Abseil LTS (`absl_TAG` in
  `cmake/BuildDependencies.cmake`); this repo's `ABSL_TAG` in
  `ExternalLib/CMakeLists.txt` is declared first and wins, so it must stay >= the
  FuzzTest pin or configure fails with missing `absl::*` targets (observed:
  `absl::random_mocking_access`). Both are `20260526.0` today.

Drift itself is guarded by `Scripts/Windows/tests/Submodule.Pins.Tests.ps1`,
which asserts no submodule sits away from its recorded commit (and that
`FUZZTEST` in particular is at a commit reachable from its remote — an
unidentified host tool keeps re-checking it out to the latest date tag). Run
the Pester suites after any pin bump. It does **not** check the Abseil version
coupling above — that one is on you.

## Running on the Host (Windows)

Containers cannot present a swapchain — run the built binaries on the bare host,
from the **repo root** as working directory (`Scripts/Windows/run_clangcl_*.ps1`
wrap this). Verified 2026-07-17 on the AMD RX 9070 XT: all four clang-cl builds
render (~32 FPS ImGui overlay).

- **Debug builds require the Vulkan validation layers on the host** — without
  them the app dies at startup with exit code `-1073740791` (`0xC0000409`,
  vulkan.hpp assert after "Validation layers requested, but not available!").
  Install the Vulkan SDK (`winget install VulkanSDK`; 1.4.350.0 is what
  `Run-SyncValidation.ps1` defaults to), or point `VK_LAYER_PATH` at a directory
  containing `VkLayer_khronos_validation.{dll,json}` (they can be extracted from
  the ContainerHub image under
  `C:\Users\ContainerAdministrator\scoop\apps\vulkan\current\Bin`).
  Profile/Release builds run without validation layers.
- The ASAN debug binary needs `clang_rt.asan_dynamic-x86_64.dll`; the build
  copies it next to `GraphicsEngine.exe`, and the run helpers set `ASAN_OPTIONS`
  with a **relative** `log_path` (an absolute `C:\...` path breaks ASAN option
  parsing at the drive-letter colon).

## Testing

- C++ tests: `ctest --test-dir <build-dir> --output-on-failure` (add `-C Debug` for
  multi-config generators). `Build-Windows.ps1` runs them unless `-SkipTests`.
  Host `ctest` cannot read a container-generated CMake tree — invoke the test
  executable directly instead (`.\build-clangcl-debug\commitTestSuite.exe`).
- Benchmarks: `clangcl-profile` builds `perfTestSuite.exe`; run via
  `Build-Windows.ps1` without `-SkipPerfTests`. `Scripts/Compare-PerfBaseline.ps1`,
  `Compare-RendererPixels.ps1` and `Compare-RendererTimings.ps1` are local-only
  comparison tools (CI runs them in validation-only mode — the runners have no GPU).
- PowerShell module tests: Pester suites under `Scripts/Windows/tests/`
  (Pester 3.4 syntax; the `pester-tests` job of `.github/workflows/Windows.yml`
  runs them with a pinned Pester 3.4.0 — gated on `[build-win]` like the rest of
  Windows CI, so they do NOT run on ordinary pushes).
- `Scripts/test-all-configs.ps1` is a local one-shot gate: the three standard
  Windows container builds plus the Linux TSan build (`-SkipLinux` drops the
  latter). Not wired into CI.
- ContainerHub's own suites (the modules this repo imports) run via
  `ExternalLib/Kataglyphis-ContainerHub/windows/scripts/tests/Invoke-Tests.ps1`.
  Run it after changing anything upstream.
- **GPU tests** (`GoldenRender.*`, `Integration.*`) skip in containers and run
  only on the host — procedure, cwd requirement and the golden-writing
  cautions are in [`docs/gpu-golden-testing.md`](docs/gpu-golden-testing.md).
  Known trap: over an RDP session the swapchain reports zero images and every
  golden fails with "No synchronization frames available" — that is the
  session, not a renderer regression (see `BACKLOG.md`).
- **Synchronization validation** catches missing/incorrect barriers that no
  pixel oracle can see (it found 10 real WRITE-AFTER-WRITE hazards in July
  2026). Run it after touching render passes, barriers, or frames-in-flight:
  ```pwsh
  pwsh -ExecutionPolicy Bypass -File .\Scripts\Windows\Run-SyncValidation.ps1
  ```
  It exits non-zero iff the run log contains `SYNC-HAZARD`. Deliberately not in
  CI (needs a GPU) — details in
  [`docs/gpu-golden-testing.md`](docs/gpu-golden-testing.md).

**Adding tests is always in scope.** You do not need permission to improve
or extend the suites — a change that fixes behaviour should generally arrive
with a test that would have caught it. Prefer assertions that survive driver
and machine differences (structural pixel properties, invariants, ordering)
over exact-value comparisons, and make GPU-dependent tests skip themselves
when no adapter is present rather than fail. Ideas worth picking up live in
[`BACKLOG.md`](BACKLOG.md), alongside the sized commitments.

**Formatting and static analysis.** clang-format/clang-tidy/cmake-format
commands, the host gotchas, the container-build behavior (clang-format always
runs; clang-tidy never does), and the suggested cadence live in
[`docs/code-quality.md`](docs/code-quality.md).

**Run more than the debug loop periodically.** `clangcl-debug` is the fast
default, but `clangcl-profile` (optimized, and the only configuration where
benchmarks mean anything) and a synchronization-validation pass each catch
classes of problem the debug loop cannot. See [`BACKLOG.md`](BACKLOG.md) for
what each one is for.

**There is no Windows ThreadSanitizer.** Use the Linux
`linux-debug-tsan-clang` preset, which CI runs, for race detection — see
"Sanitizer semantics" above.

## What CI runs, and what it does not

Only the Linux x86_64 lane runs on every push/PR to `main`/`develop`. The
heavier lanes are **opt-in per commit**, matched against the pushed HEAD
commit's message:

| Lane | Workflow | Trigger |
| --- | --- | --- |
| Linux x86_64 (build + test + coverage) | `Linux_x86.yml` → `Linux.yml` | always |
| Windows (clang-cl/MSVC container build, Pester) | `Windows.yml` | `[build-win]` in the commit message |
| Linux ARM64 | `Linux_arm.yml` → `Linux.yml` | `[build-arm]` in the commit message |

Consequence: **a Windows-only change pushed without `[build-win]` gets no CI
signal at all.** The marker must be in the HEAD commit of the push, not an
earlier one. Full rules:
[`ci-build-triggers.md`](ExternalLib/Kataglyphis-ContainerHub/docs/ci-build-triggers.md).
Reading pipeline status from a shell (`gh`):
[`github-cli-pipeline-monitoring.md`](ExternalLib/Kataglyphis-ContainerHub/docs/github-cli-pipeline-monitoring.md).

`Linux_x86.yml` and `Linux_arm.yml` both call the reusable `Linux.yml`, so a fix
to the x86 lane applies to ARM automatically. No CI lane has a GPU — the golden
and synchronization suites are host-only by construction.

The `ubuntu-24.04` leg of the Linux lane also runs the Rust renderer crate's
own test suite (`Scripts/Linux/run-cargo-tests.sh`, `cargo test -p
kataglyphis_webgpu_renderer`) after the performance benchmarks step. Before
this, the crate was compiled twice in this repo (the Rust bridge and the wasm
demo) but its ~150 tests only ran in `Kataglyphis-RustProjectTemplate`'s own
workflow — so edits made to `crates/webgpu_renderer` from this working tree
got no test signal until the submodule was pushed separately.

## Code Conventions (C++ engine)

- **Exceptions are disabled project-wide** (`/EHs-`, `-fno-exceptions` in
  `cmake/ProjectOptions.cmake`; `VULKAN_HPP_NO_EXCEPTIONS` in
  `Src/GraphicsEngineVulkan/CMakeLists.txt`): vulkan.hpp calls return
  `ResultValue`; `throw`/`try` will not compile. `ASSERT_VULKAN(val, "msg")`
  (`common/Utilities.hpp`) logs critical and aborts — use it on
  creation/allocation calls only.
- Graphics pipelines are built via `kataglyphis.vulkan.pipeline_builder`
  (`vulkan_base/PipelineBuilder.ixx`) — do not hand-roll the create-info chain.
- Buffer/image memory goes through VMA (allocator owned by `VulkanDevice`);
  `VulkanBuffer`/`VulkanImage` are move-only with destructor release
  (`cleanUp()` remains for explicit early teardown and is idempotent).
- A `VkPipelineCache` persists to `pipeline_cache/kataglyphis_pipeline.cache`
  (gitignored, written on graceful shutdown only).
- Slang emits `"main"` as the SPIR-V entry point name (not the Slang function
  name), so all `pName` values in pipeline creation use `"main"`.
- Model-loading architecture (the two loaders, the async parse/upload split,
  the multi-mesh flow): [`docs/model-loading.md`](docs/model-loading.md).
- Per-unit verification pattern (container build -> direct test exe ->
  validation run): [`docs/gpu-golden-testing.md`](docs/gpu-golden-testing.md).
  The chronological log of what changed and why:
  [`docs/cpp-renderer-improvements.md`](docs/cpp-renderer-improvements.md).
  Do not restate either here — those documents are the source of truth.

## Agentic Loop

An autonomous planner/executor loop continuously improves the engine. The
**planner** (expensive, powerful model) analyzes the codebase and writes
detailed task entries to `BACKLOG.md`. The **executor** (cheap, fast model)
drains the queue one task at a time, building and testing as it goes. The
queue must be fully drained before the planner adds new tasks; tasks marked
`- [b]` are blocked and do not count as pending.

Two engines are selectable via `engine` in
`Scripts/AgenticLoop/AgenticLoop.config.json`, `-Engine`/`--engine`, or the
`AGENTIC_ENGINE` env var: **`claude`** (default — Claude Code CLI, Opus 5
planner with Fable 5 fallback, Sonnet executor, system prompts in
`Scripts/AgenticLoop/prompts/`) and **`opencode`** (GLM 5.2 planner, DeepSeek v4
Flash executor, agents in `.opencode/agents/`).

**Reusable logic lives in ContainerHub's `WindowsAgenticLoop.Common` module
(PowerShell) and `agentic-loop.sh` library (Bash).** The project scripts are
thin consumers: run `Scripts/AgenticLoop/Run-AgenticLoop.ps1` (Windows,
requires PowerShell 7+) or `Scripts/AgenticLoop/Run-AgenticLoop.sh` (Linux,
requires `jq`). The default planner/executor **task** prompts are single-sourced
in ContainerHub at `shared/agentic-loop/prompts/*.md` — both the PowerShell
module and the Bash library read them; only the engine-neutral **system**
prompts stay project-owned. Architecture, configuration, and usage:
[`Scripts/AgenticLoop/README.md`](Scripts/AgenticLoop/README.md); build matrix
and sanitizer-aware tests:
[agentic-loop-build-matrix.md](ExternalLib/Kataglyphis-ContainerHub/docs/agentic-loop-build-matrix.md);
module API reference:
[windows-agentic-loop.md](ExternalLib/Kataglyphis-ContainerHub/docs/windows-agentic-loop.md).

## Docs

Each topic has exactly one home; link, do not copy. Reusable topics live in
ContainerHub (see the rule above), project-specific ones here.

| Where | Owns |
| --- | --- |
| `README.md` | Repo-level orientation, feature highlights |
| `BACKLOG.md` | **All** open work: sized commitments, then unsized ideas and recurring chores |
| `AGENTS.md` (this file) | How to build/test/run here, invariants, code conventions |
| `docs/cpp-renderer-improvements.md` | C++ engine chronological change log |
| `docs/model-loading.md` | Model-loading architecture: the two loaders, async parse/upload split, multi-mesh MeshRange flow |
| `docs/webgpu-renderer-roadmap.md` | Rust WebGPU renderer status per feature |
| `docs/webgpu-gltf-rust-plan.md` | Original WebGPU + glTF Rust renderer plan (milestones 1–5), kept for the record |
| `docs/shader-sharing.md` | Why/how one Slang source serves both renderers, and where the two diverge |
| `docs/shader-build-pipeline.md` | The Slang→SPIR-V/WGSL build step: manifest, staleness rules, fast iteration |
| `docs/webgpu-srgb-audit.md` | Colour-space decisions (no known deviations) |
| `docs/code-quality.md` | clang-format / clang-tidy / cmake-format commands + cadence |
| `docs/container-build-caching.md` | This repo's container transport numbers, sccache volume, incremental-build wiring |
| `docs/gpu-golden-testing.md` | GPU golden test suites, skip-without-GPU behavior, host verification loop, synchronization validation |
| `docs/path-tracing.md` | Path-tracing mode: pipeline shape, estimator, NEE, accumulation |
| `docs/renderer-bounds-invariant.md` | WebGPU renderer bounds invariant |
| `docs/LICENSES-README.md` | Third-party license documentation (German) |
| `docs/source/` | Sphinx pages (`README.md`, `getting_started.md`, `documentation_workflow.md`, `webgpu_demo.md`, `wsl2_vulkan.rst`, `graphviz_files.rst`) |
| `Scripts/AgenticLoop/README.md` | Agentic loop (claude/opencode engines) architecture, config, usage |
| `ExternalLib/Kataglyphis-ContainerHub/docs/windows-builds.md` | The Windows container image: build sequence, Stevedore setup, invariants |
| `ExternalLib/Kataglyphis-ContainerHub/docs/windows-container-build-performance.md` | Building inside the image: transports, reuse pattern, safety rails |
| `ExternalLib/Kataglyphis-ContainerHub/docs/rancher-desktop-linux-containers.md` | Running the Linux image locally: nerdctl, cargo cache volume, build-dir rules |
| `ExternalLib/Kataglyphis-ContainerHub/docs/ci-build-triggers.md` | Which CI lanes run when; the `[build-win]` / `[build-arm]` commit-message opt-ins |
| `ExternalLib/Kataglyphis-ContainerHub/docs/github-cli-pipeline-monitoring.md` | Reading and fixing CI status from a shell with `gh` |
| `ExternalLib/Kataglyphis-ContainerHub/docs/adopting-in-a-new-project.md` | Wiring another project to the loop, both container flows, launchers, CI actions |
| `ExternalLib/Kataglyphis-ContainerHub/docs/agentic-loop-build-matrix.md` | Build matrix config, sanitizer env vars, full matrix sweep |
| `ExternalLib/Kataglyphis-ContainerHub/docs/windows-agentic-loop.md` | WindowsAgenticLoop.Common module API + config reference |

- Keep docs, scripts, and presets aligned: when you change build behavior, update
  `README.md`, `docs/source/getting_started.md`, and this file in the same change.
