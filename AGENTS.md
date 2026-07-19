# AGENTS.md

Guidance for AI agents and new contributors working in Kataglyphis-BeschleunigerBallett
(a Vulkan/OpenGL graphics-engine playground: C++23/C17, CMake presets, optional Rust).

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
| `clangcl-tsan` | `x64-ClangCL-Windows-Debug-TSan` | `build-clangcl-tsan` | Debug, TSan requested (see caveat below), ASAN off |
| `clangcl-profile` | `x64-ClangCL-Windows-Profile` | `build-clangcl-profile` | RelWithDebInfo + tests/benchmarks |
| `clangcl-release` | `x64-ClangCL-Windows-Release` | `build-clangcl-release` | Release + CPack packaging |
| `msvc-debug` / `msvc-release` | `x64-MSVC-Windows-*` | `build-msvc-debug` | MSVC (cl) builds, optional steps |

There is also a configure-only `x64-ClangCL-Windows-Debug-ASan` preset
(AddressSanitizer without the fuzzing-mode extras). Test presets exist per
main configuration (`test-<configure-preset>`); the plain-Clang
`x64-Clang-Windows-{Debug,Profile,RelWithDebInfo}` presets were removed in
2026-07 as unused duplicates of the ClangCL set. `x64-Clang-Windows-Release`
stays: the `windows-clang-release-wix` package preset builds on it.

Typical full sweep (ASAN debug, TSan debug, profile, release):

```powershell
powershell -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows.ps1 `
  -Configurations "clangcl-debug,clangcl-tsan,clangcl-profile,clangcl-release" `
  -SkipFormat -SkipTidy -SkipTests -SkipPerfTests -SkipMsix
```

### Sanitizer semantics (do not guess — this is how it actually works)

- Sanitizer flags are applied **only to the Debug configuration**
  (`$<$<CONFIG:Debug>:...>` in `cmake/Sanitizers.cmake`). Profile/Release builds are
  never sanitized.
- ASAN and UBSan default **ON** for Debug builds (Linux GCC/Clang, MSVC, clang-cl);
  see `myproject_default_debug_sanitizers` in `cmake/ProjectOptions.cmake`.
- TSan presets set `myproject_ENABLE_SANITIZER_THREAD=ON` and force
  `myproject_ENABLE_SANITIZER_ADDRESS=OFF` (TSan and ASAN are mutually exclusive —
  `Sanitizers.cmake` drops TSan if ASAN is on).
- **clang-cl does not support TSan** (`x86_64-pc-windows-msvc`); the Windows TSan
  preset therefore configures with a warning and builds a plain Debug binary with
  **no sanitizers** (it also sets `myproject_ENABLE_SANITIZER_UNDEFINED=OFF`, see
  the CRT note below). It exists for cross-platform preset parity; real TSan runs
  need the Linux presets (`linux-debug-tsan-clang` / `linux-debug-tsan-GNU`).
- The presets' `USE_THREAD_SANITIZER` cache variable is **legacy plumbing consumed by
  nothing in CMake** — the effective switch is `myproject_ENABLE_SANITIZER_THREAD`.
  Keep both in sync if you touch the TSan presets.
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
  turn both off together (as the TSan preset does).

## Containerized Windows Builds (Stevedore)

Windows builds run inside the ContainerHub developer image
`ghcr.io/kataglyphis/kataglyphis_beschleuniger:winamd64` (clang-cl, CMake, Ninja,
Vulkan SDK, Rust, sccache — everything preinstalled). CI does exactly this
(`.github/workflows/Windows.yml`); locally use:

```powershell
# Builds clangcl-debug,clangcl-tsan,clangcl-profile,clangcl-release by default
powershell -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows-Container.ps1
```

**All Windows-container knowledge lives in ContainerHub** — do not restate it
here. Two documents cover it:

- [`ExternalLib/Kataglyphis-ContainerHub/docs/windows-builds.md`](ExternalLib/Kataglyphis-ContainerHub/docs/windows-builds.md)
  — the image itself: build sequence, Stevedore setup, invariants.
- [`ExternalLib/Kataglyphis-ContainerHub/docs/windows-container-build-performance.md`](ExternalLib/Kataglyphis-ContainerHub/docs/windows-container-build-performance.md)
  — building *inside* it: the reusable-container pattern and its safety rails,
  why sccache cannot cache a C++23 modules build, why a named volume cannot be
  a CMake build directory, the Windows path limit that silently truncates tar
  transfers, the Dev Drive bind-mount restriction, `docker exec` bypassing the
  entrypoint, and the wcifs teardown lock.

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
logs come out. Measured 48 s for a no-change rebuild and 63 s after touching a
header, against 352-484 s when every build got a fresh container. Use
`-FreshContainer` to start clean (deleted files are not pruned from a reused
container; an image change recreates it automatically).

The general Windows-container findings behind this — the pattern, its safety
rails, and three approaches that do NOT work — are documented once in
ContainerHub:
`ExternalLib/Kataglyphis-ContainerHub/docs/windows-container-build-performance.md`.
`sccache` is wired to a persistent volume but does **not** help — this is a
C++23 modules build and its hit rate is measured at 0%. If a build ever
behaves strangely, delete the build directory for a clean cold build. Full
measurements, including two approaches that do not work, in
[`docs/container-build-caching.md`](docs/container-build-caching.md).

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
`b391a1d`) deleted upstream: `WindowsLogging`, `WindowsCMake`, `WindowsConfig`,
`WindowsClang`, `WindowsFormatting`, `WindowsTesting`, `WindowsWebDav`,
`WindowsMsix.Common`, `WindowsMsix.Signing` — plus `WindowsScripts.Shared`,
which the vendored modules import internally by sibling path (direct imports of
it still prefer the ContainerHub copy). If a module reappears upstream it wins
automatically; if you improve a fallback module, consider upstreaming it to
ContainerHub and deleting the vendored copy in the same change.

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
`linux-debug-tsan-clang`, `linux-debug-tsan-GNU`. Coverage, ctest, and perf wrappers
live next to it. Vulkan SDK env can be injected with `--vulkan-setup-script`.

## Testing

- C++ tests: `ctest --test-dir <build-dir> --output-on-failure` (add `-C Debug` for
  multi-config generators). `Build-Windows.ps1` runs them unless `-SkipTests`.
  Test presets exist per main configuration (`test-<configure-preset>`).
- Benchmarks: `clangcl-profile` builds `perfTestSuite.exe`; run via
  `Build-Windows.ps1` without `-SkipPerfTests`.
- PowerShell module tests: Pester suites under `Scripts/Windows/tests/`.

**Adding tests is always in scope.** You do not need permission to improve
or extend the suites — a change that fixes behaviour should generally arrive
with a test that would have caught it. Prefer assertions that survive driver
and machine differences (structural pixel properties, invariants, ordering)
over exact-value comparisons, and make GPU-dependent tests skip themselves
when no adapter is present rather than fail. Ideas worth picking up live in
[`BACKLOG.md`](BACKLOG.md); sized commitments live in [`ROADMAP.md`](ROADMAP.md).

**Formatting and static analysis.** clang-format/clang-tidy/cmake-format
commands, the host gotchas (LLVM is not on `PATH`; the container-generated
`compile_commands.json` points at `C:/ws`; C++23 module TUs are skipped by
clang-tidy), and the suggested cadence live in
[`docs/code-quality.md`](docs/code-quality.md). Note that
`Build-Windows-Container.ps1` hard-codes `-SkipFormat -SkipTidy`, so
containerized builds never run them.

**Run more than the debug loop periodically.** `clangcl-debug` is the fast
default, but `clangcl-profile` (optimized, and the only configuration where
benchmarks mean anything), `clangcl-tsan`, and a synchronization-validation
pass each catch classes of problem the debug loop cannot. See
[`BACKLOG.md`](BACKLOG.md) for what each one is for.

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

## Docs

Each topic has exactly one home; link, do not copy.

| Where | Owns |
| --- | --- |
| `README.md` | Repo-level orientation, feature highlights |
| `ROADMAP.md` | Agreed future work, sized, with blocked items marked |
| `BACKLOG.md` | Unsized ideas and recurring chores (perf tests, periodic runs) |
| `AGENTS.md` (this file) | How to build/test/run here, invariants, code conventions |
| `docs/cpp-renderer-improvements.md` | C++ engine change log + verification pattern |
| `docs/webgpu-renderer-roadmap.md` | Rust WebGPU renderer status per feature |
| `docs/shader-sharing.md` | WGSL -> SPIR-V/GLSL pipeline between both renderers |
| `docs/webgpu-srgb-audit.md` | Colour-space decisions and the one known deviation |
| `docs/code-quality.md` | clang-format / clang-tidy / cmake-format commands + cadence |
| `docs/shader-build-pipeline.md` | GLSL→SPIR-V build step, staleness rules, fast shader iteration |
| `docs/container-build-caching.md` | Container transport, sccache volume, incremental-build options |
| `docs/source/` | Sphinx pages (`getting_started.md`, `documentation_workflow.md`) |

- Keep docs, scripts, and presets aligned: when you change build behavior, update
  `README.md`, `docs/source/getting_started.md`, and this file in the same change.
