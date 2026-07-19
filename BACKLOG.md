# BACKLOG

Ideas and recurring chores that are **not** committed roadmap items. Things
here are candidates: they graduate to [`ROADMAP.md`](ROADMAP.md) when they
get a size and a decision, or get dropped. Roadmap = what we agreed to do;
backlog = what we might.

## Performance testing

The perf suite currently benchmarks `std::string` create/copy — it measures
nothing about this engine, and it is registered outside CTest, so nothing
gates on it.

- **Benchmarks that mean something.** Candidates, in rough value order:
  - `record_commands` wall time per frame for each render mode (forward,
    deferred, RT, path tracing) at a fixed scene + camera — the closest
    proxy to "did a refactor make the frame path slower".
  - Upload path: `createBufferAndUploadVectorOnDevice` for a few payload
    sizes, now that the staging buffer is reused (guards against a
    regression back to per-upload create/destroy).
  - `ObjLoader::loadModel` on a bundled model — parsing plus mesh build.
  - Cascade math: `CascadedShadowMap::updateCascades` (pure CPU, no GPU
    needed, so it can run in CI).
  - Pure-CPU units are the ones worth gating in CI; anything touching the
    GPU is machine-dependent and belongs in the "run it locally" bucket.
- **GPU-side numbers already exist**: per-pass timestamps land in
  `GUIRendererSharedVars::gpuTimings` (GUI "GPU timings" header). A headless
  mode that renders N frames and dumps the per-pass averages as JSON would
  turn them into a comparable artifact instead of a number a human squints
  at.
- **Regression tracking**: Google Benchmark can emit JSON
  (`--benchmark_out=... --benchmark_out_format=json`); storing one baseline
  per machine and diffing beats eyeballing console output.

### Measured baseline (2026-07-19, clangcl-profile, 32-core 4.3 GHz)

| Benchmark | Time |
| --- | --- |
| `BM_CameraViewMatrix` | 10.1 ns |
| `BM_ProjectionAndInverses` | 30.3 ns |
| `BM_CameraKeyControl` / `MouseControl` | ~34 ns |
| `BM_AvailableModelPaths` | 859 ns |
| `BM_ResolveModelPath_Hit` | 4.1 us |
| `BM_ResolveModelPath_Miss` | 15.7 us |
| `BM_ObjParse_Plane` (1 KB) | 23 us |
| `BM_ObjParse_Suzanne` (1 MB) | 7.1 ms |

Two things this baseline already tells us:

- **Asset loading blocks for a long time.** 1 MB of OBJ costs ~7 ms of
  pure parsing; `dinosaurs.obj` is 27 MB, so a load is plausibly ~200 ms
  of frozen main thread. That is the concrete case for the async
  asset-loading item in `ROADMAP.md` - it was previously argued from
  first principles only.
- **`resolveModelPath` is ~4x slower when it misses** (8 parent-directory
  probes). Fine once at startup, bad in a loop.

## Recurring validation runs

Debug-only builds are the default working loop (fast, sanitized). Things
that are *not* exercised that way and should be run periodically:

- **`clangcl-profile` (RelWithDebInfo) once in a while** — optimized code
  paths differ from debug: different inlining, different UB exposure,
  and it is the only configuration where the benchmarks are meaningful
  (debug timings are noise). Run it after any perf-relevant change and
  before a release; it also builds `perfTestSuite.exe`.
- **`clangcl-tsan`** — data races only show up here; nothing runs it today.
- **Synchronization validation** — `khronos_validation.validate_sync = true`
  in `vk_layer_settings.txt` next to the executable. This found 10 real
  WRITE-AFTER-WRITE hazards in July 2026; it is not part of any automated
  run, so it needs a deliberate pass after touching render passes,
  barriers, or frames-in-flight.
- **Release build** — the only configuration with logging compiled out and
  validation layers absent; behavioral surprises hide there.

## Test coverage ideas

- Headless offscreen rendering in the C++ engine, mirroring the Rust
  renderer's structural pixel assertions (colour dominance, coverage
  ratios, energy deltas rather than exact images). This is the missing
  piece that would have caught the "shadow map rendered but never sampled"
  bug — tracked as a sized item in `ROADMAP.md`.
- Fuzz the remaining untrusted inputs: `SceneConfig` parsing, the shader
  file reader, KTX2/texture loading paths.
- A GUI-state round-trip test (`GUISceneSharedVars` → renderer → back)
  so option plumbing cannot silently break, as the cascade-count default
  did.
- Property tests for `CascadedShadowMap::updateCascades`: splits strictly
  increasing, cascade frustums covering the camera frustum, no NaNs at
  degenerate FOV/aspect.

## Code quality (see `docs/code-quality.md` for the commands)

- **Decide on the formatting sweep.** 72 of 125 own sources under `Src/` and
  `Test/` do not match `.clang-format` (measured 2026-07-19). Fixing this is
  one enormous commit that will collide with anything in flight, so it wants
  a deliberate moment (right after a merge point) plus a
  `.git-blame-ignore-revs` entry. Alternative: format-on-touch only, and let
  the drift shrink over time.
- **Containerized builds never lint.** `Build-Windows-Container.ps1` passes
  `-SkipFormat -SkipTidy` unconditionally; the fast loop therefore cannot
  catch style or tidy regressions. Options: a separate periodic container
  run without the skips, or a pre-push hook that formats touched files.
- **clang-tidy cannot see C++23 module TUs** (module BMIs reference the
  container layout). Either run tidy inside the container, or accept that
  coverage is limited to the non-module surface.

## CI and release gaps

- **Windows CI tests nothing.** `Windows.yml` passes `-SkipTests
  -SkipPerfTests`; it only proves the code compiles. Every behavioural
  guarantee on Windows comes from a human running the suite locally, even
  though the GPU integration tests pass here routinely. Options: a
  self-hosted runner with a GPU, or at minimum run the non-GPU tests
  (camera/scene-config/fuzz) on the hosted runner.
- **Packaging paths are never exercised.** DEB (`linux-release-deb`), WiX
  (`windows-clang-release-wix`) and MSIX are configured but nothing builds
  them in CI, so breakage surfaces at release time.
- **Coverage is clang-only** (Linux). GCC and Windows contribute no
  coverage data, which skews what Codecov reports.
- **Docs builds are unverified.** Sphinx/Doxygen output is deployed by
  `Linux.yml` but nothing checks for broken links or missing pages first.
- **Golden-image CI** for the Rust renderer: the headless tests already
  render; storing reference images per GPU vendor would catch shader
  regressions that structural assertions miss (they were designed to be
  driver-independent, which is also their blind spot).

## Startup and build-time costs

- **GLSL is recompiled from source at every pipeline build.** The
  persisted `VkPipelineCache` helps the driver side, but `ShaderHelper`
  still runs the front end on every startup and every hot reload.
  `Resources/Shaders/**/spv/*.spv` already exists - consuming prebuilt
  SPIR-V (falling back to runtime compilation when the source is newer)
  would cut startup and make hot-reload stalls proportional to the one
  shader that changed.
- **sccache writes nothing (0 bytes) despite the volume being mounted.** The
  named volume and `SCCACHE_DIR` take effect (sccache reports `C:\sccache`),
  but a full build leaves the cache empty, so hit rate stays at 0%. Diagnose
  write permissions for `ContainerAdministrator` on the volume, and whether
  `sccache --stop-server` kills the server before it flushes. See
  `docs/container-build-caching.md`.
- **Container builds take ~6 minutes**, dominated by streaming the tree
  in and the build tree back (the Dev Drive blocks bind mounts, see
  [[stevedore-container-builds]]). Worth timing `sccache` hit rates and
  checking whether a persistent named volume for the build tree beats the
  tar-pipe.
- **Module dependency scanning** (`clang-scan-deps`) runs over all 53
  `.ixx` files each configure; measure before assuming it is free.

## Developer-experience papercuts (all hit during the 2026-07 campaign)

- Host `cmake` is 3.29 and **cannot read this repo's `CMakePresets.json`**
  (`version: 10`); only the container's newer CMake can. Anyone running
  `cmake --list-presets` on the host gets a confusing parse error.
- **LLVM is not on `PATH`** despite being installed - see
  `docs/code-quality.md` for the absolute paths.
- **Swapchain screenshots read black while the desktop session is
  locked**, with no error - a capture path that silently lies. Always
  take a control capture of a known-good app before believing a black
  frame is a regression.
- **Build containers occasionally survive a successful build**
  (`wcifs teardown lock`); a stale container makes it look like a build is
  still running. Compare the newest `logs/windows/build-summary-*.json`
  timestamp against container start before assuming.
- `Scripts/Windows/Build-Windows-Container.ps1` takes `-Configurations`,
  not `-Preset`; passing the wrong one silently builds **all four**
  configurations.

## Architecture debt not yet sized

- **`VulkanRenderer` is still the hub.** PipelineBuilder (-416 lines) and
  DescriptorSetGroup (-617) shrank it a lot, but it still owns the
  swapchain, sync objects, UBOs, five stages and four foreign pointers
  (`Window*`, `Scene*`, `GUI*`, `Camera*`). Candidate extractions:
  `FrameSync` (fences/semaphores/frame index), `SwapchainTarget`
  (swapchain + framebuffers + recreation), a stage registry so adding a
  pass does not mean editing the renderer.
- **Device-lost teardown is special-cased in `App.cpp`** (scene/GUI
  cleanup is skipped) - a symptom of ownership living in the wrong place.
  Full RAII up the stack would remove the special case entirely.
- **`GUI*` is a mutable cross-cutting dependency**: both `Scene` and
  `VulkanRenderer` read GUI state each frame. A plain settings struct
  owned by the app, passed by const reference, would decouple them.
- **One object only.** `shader.frag` hard-codes `object_description.i[0]`
  ("for now only one object allowed"); the whole material/descriptor path
  assumes it. Multi-object support is a prerequisite for any real scene.

## Rust renderer ideas (unsized)

- **Render-graph v2**: the current graph validates declared read/write
  wiring but does not schedule or alias resources. Automatic barrier
  placement and transient-resource aliasing are the natural next steps -
  worth it only when pass count grows again.
- **Texture streaming / bindless**: the renderer binds per-primitive sets;
  a bindless array plus streaming would be needed for photogrammetry-scale
  scenes (the Colosseum case).
- **wgpu timestamp queries** to mirror the C++ per-pass GPU timings, so
  the side-by-side comparison harness can compare *timings*, not just
  pixels.
- **Wasm size budget**: the demo payload is ~3.7 MB uncompressed and
  nothing tracks it; `wasm-opt -Oz` plus a CI size gate would keep the
  Sphinx-hosted demo honest.

## Housekeeping candidates

- The `x64-Clang-Windows-Release` preset survives only because
  `windows-clang-release-wix` packages from it; if WiX packaging moves to
  ClangCL, that preset can go too.
- `imgui.ini` is tracked and changes whenever a window is dragged — decide
  whether it is source (layout you want shipped) or user state (gitignore).
