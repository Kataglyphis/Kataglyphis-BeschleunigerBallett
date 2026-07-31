# BACKLOG

The single list of open work across the whole project — sized commitments and
unsized ideas together. Detailed per-area status lives in `docs/`
(`cpp-renderer-improvements.md`, `webgpu-renderer-roadmap.md`,
`shader-sharing.md`); this file is what is still to do.

Sizes: S (< half a day), M (a day-ish), L (multi-day), XL (multi-week).
Checkbox items are sized and agreed; the prose sections below the fold are
candidates that have not been sized yet. A candidate graduates by acquiring a
size and a decision, or gets dropped.

Checkbox states: `- [ ]` actionable (the agentic loop's executor picks these
up), `- [b]` blocked/parked (waiting on a prerequisite or an owner decision —
the loop skips these and they do not count toward its pending-task queue),
`- [x]` done (pruned automatically; history lives in git).

> Merged from the former `ROADMAP.md` on 2026-07-20. There is no longer a
> separate roadmap file — one list, so a stale entry in one place cannot
> contradict a fresh one in the other. That had already happened: the roadmap
> still described cascaded shadows as completely broken a day after they were
> fixed.

## C++ Vulkan engine

> **The "two instruments disagree" entry that used to be here was my own
> error, and the mistake is worth keeping.** The golden test appeared to
> report 11.60% darkened / mean 63.70 -> 47.77 while an independent
> measurement of the same states showed no change. The golden numbers had
> been taken with **stale probe SPIR-V still compiled in** - a forced
> full-coverage triangle in the shadow geometry shader plus a forced return
> in `calc_cascaded_shadow`. That is why the figure matched the forced-1.0
> ceiling to two decimal places: it *was* the forced probe.
> `BuildIntegrity.CompiledShadersAreNotOlderThanTheirSources` caught the
> staleness minutes later and I did not connect it to the measurement I had
> just taken. Both instruments now agree. **After touching any shader,
> recompile and re-run the integrity tests BEFORE trusting a rendered
> measurement - including one taken moments earlier.**
- [b] **Renderer-level RAII cleanup consolidation** (M, **blocked on being
  testable**) — the stage-level work landed 2026-07-19; `VulkanRenderer`'s
  hand-ordered `cleanUp()` and the device-lost special-casing in `App.cpp`
  are what is left.

  Deliberately not attempted 2026-07-20. The whole point of the change is the
  device-lost path — `App.cpp` skips `scene->cleanUp()`/`gui->cleanUp()` when
  the device is lost — and device loss cannot be induced here, so removing
  that guard would be an untestable behaviour change to the one path that
  only runs when things have already gone wrong. The payoff is code
  cleanliness, not a user-visible defect. Get a way to simulate device loss
  first (a device-simulation layer, or a deliberate fault injection behind a
  debug flag); then the refactor is safe and its correctness is checkable.

## Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate`)

- [b] **Basis ETC1S/UASTC transcoding** (M, **blocked on a transcoder + a test
  asset**) — KTX2 BCn passthrough is done; supercompressed files are already
  rejected with a clear error (`ktx2_loader.rs`: "supercompression … not supported
  yet"). Two concrete blockers surfaced 2026-07-21: (1) the viable transcoder is
  the `basis-universal` crate, a **C++ binding** (build.rs compiles the upstream
  basisu — a build-system dependency, not pure Rust; no mature pure-Rust
  transcoder exists), and (2) there is **no Basis-compressed KTX2 test asset**
  in-repo (only `tests/assets/red_bc1.ktx2`, plain BC1), so an implementation
  can't be verified headlessly. Do it as a deliberate cycle: vendor `basis-universal`,
  generate an ETC1S + a UASTC `.ktx2` via `toktx`/`basisu`, then transcode to a
  BCn `CompressedFormat` on desktop and to ETC2/ASTC on the web path.
- [b] **Indirect draws** (M, blocked on GPU occlusion culling producing draw
  arguments) — instancing landed without them. Indirect only
  pays once draw arguments come from the GPU (culling compute, batched
  submission); with CPU-side instance counts it adds a buffer round trip for
  nothing. Revisit alongside GPU occlusion culling below, which is what would
  produce those arguments.
- [b] **WebXR** (XL) — parked.
- [b] **Colosseum demo scene** (blocked on you) —
  pick a licensed photogrammetry scan, keep the asset out of git.

  This entry used to claim "LOD + KTX2 machinery is ready" while the LOD
  subsystem was library-and-tests-only, called by no render pass at all. That
  is now genuinely true for LOD (see the render-path item above) — set
  `lod_enabled` before `upload_scene`. **KTX2 is still not ready**: Basis
  ETC1S/UASTC transcoding is unimplemented (`asset/ktx2_loader.rs` rejects any
  supercompression), so a scan shipping Basis-compressed textures will not
  load.

## Cross-renderer

---

Everything below is **unsized**: ideas and recurring chores that have not been
committed to.

## Performance testing

- **Benchmarks still missing**, in rough value order:
  - `record_commands` wall time per frame for each render mode (forward,
    deferred, RT, path tracing) at a fixed scene + camera — the closest
    proxy to "did a refactor make the frame path slower".
  - Upload path: `createBufferAndUploadVectorOnDevice` for a few payload
    sizes, now that the staging buffer is reused (guards against a
    regression back to per-upload create/destroy).
  - Pure-CPU units are the ones worth gating in CI; anything touching the
    GPU is machine-dependent and belongs in the "run it locally" bucket.
  - **glTF parse is now benchmarked** (2026-07-21): `BM_GltfParse_CubeGlb` and
    `BM_GltfParse_CubeTextured` (`Test/perf/perfSuite.cpp`) mirror the OBJ pair -
    parse the document, load buffers, walk every POSITION accessor. They drive
    `cgltf` DIRECTLY with a local `CGLTF_IMPLEMENTATION`, exactly as the OBJ
    benchmarks drive tinyobj, and deliberately NOT `GltfLoader::parseCpu`:
    importing the engine loader would pull `Device`/`Model`/`Texture` (and their
    Vulkan-touching global ctors) into this headless binary - the same
    headless-global-ctor hazard that took the fuzzer down - and also re-collides
    the duplicate `TINYOBJLOADER_IMPLEMENTATION` that only stays benign while
    `ObjLoader.cpp.o` is never linked in. The `.glb` (inline binary buffer) and
    `.gltf` (base64 data-URI) exercise different `cgltf_load_buffers` decode
    paths. Verified building + running under `linux-profile-GNU` (the CI
    benchmark config); host figures belong in the baseline table below, taken on
    a clean host run rather than a container (wcifs I/O dominates the wall time).
- **GPU-side numbers already exist**: per-pass timestamps land in
  `GUIRendererSharedVars::gpuTimings` (GUI "GPU timings" header). A headless
  mode that renders N frames and dumps the per-pass averages as JSON would
  turn them into a comparable artifact instead of a number a human squints
  at. Nothing asserts a budget for `GpuTimedPass::ShadowCascades` today.
- **Regression tracking**: Google Benchmark can emit JSON
  (`--benchmark_out=... --benchmark_out_format=json`); storing one baseline
  per machine and diffing beats eyeballing console output. **Done
  (2026-07-31):** `Scripts/Compare-PerfBaseline.ps1` diffs a fresh JSON run
  against the checked-in `Test/perf/baselines/win-9070xt-32core.json`, flags
  any benchmark that regressed beyond a configurable tolerance (default
  +25%), and is deliberately not wired into CI (see the "measured baseline"
  table below for why: machine-dependent numbers).

### Measured baseline (2026-07-19, clangcl-profile, 32-core 4.3 GHz)

| Benchmark | Time |
| --- | --- |
| `BM_CameraViewMatrix` | 10.1 ns |
| `BM_ProjectionAndInverses` | 30.3 ns |
| `BM_CameraKeyControl` / `MouseControl` | ~34 ns |
| `BM_AvailableModelPaths` | 2.72 ns (was 859 ns before the `std::span` return, 2026-07-31) |
| `BM_ResolveModelPath_Hit` | 4.1 us |
| `BM_ResolveModelPath_Miss` | 15.7 us |
| `BM_ObjParse_Plane` (1 KB) | 23 us |
| `BM_ObjParse_Suzanne` (1 MB) | 7.1 ms |
| `BM_ComputeCascadeData/1` (2026-07-31) | 372 ns |
| `BM_ComputeCascadeData/3` (2026-07-31) | 1.03 us |
| `BM_FrustumCull/64` (2026-07-31) | 339 ns |
| `BM_FrustumCull/512` (2026-07-31) | 2.93 us |
| `BM_FrustumCullShadowCaster/64` (2026-07-31) | 326 ns |
| `BM_FrustumCullShadowCaster/512` (2026-07-31) | 2.77 us |

Two things this baseline already tells us:

- **Asset loading blocks for a long time.** 1 MB of OBJ costs ~7 ms of
  pure parsing; `dinosaurs.obj` is 27 MB, so a load is plausibly ~200 ms
  of frozen main thread. That is the concrete case for the async
  asset-loading item above — it was previously argued from first
  principles only.
- **`resolveModelPath` is ~4x slower when it misses** (8 parent-directory
  probes). Fine once at startup, bad in a loop.

## Recurring validation runs

Debug-only builds are the default working loop (fast, sanitized). Things
that are *not* exercised that way and should be run periodically:

- **Multiview `viewMask` validation warning (observed 2026-07-23) — fix landed
  2026-07-31, GPU confirmation blocked by #2106.** `VulkanDevice` now queries
  `VkPhysicalDeviceVulkan11Properties::maxMultiviewViewCount`
  (`getMaxMultiviewViewCount()`) and both cascade-count decision points
  (startup init, `handleShadowResolutionChange`) route through a new
  `clampCascadeCount()` free function (`CascadedShadowMap.ixx/.cpp`, CPU-tested:
  `ClampCascadeCountRespectsBothLimits` / `ClampCascadeCountFloorsToOne`), so a
  device with `maxMultiviewViewCount < MAX_CASCADES` can no longer create a
  non-conformant render pass. **Could not confirm on the GPU host that the
  original two VUIDs are gone**: the RX 9070 XT run instead hit the
  `multiview`-feature-not-enabled abort tracked in #2106 below (`viewMask is
  0x7, but multiview feature is not enabled` —
  `VUID-VkSubpassDescription2-multiview-06558`) before the render pass this fix
  touches gets far enough to show the original VUIDs one way or the other.
  `maxMultiviewViewCount` itself did read correctly (6, logged at device
  selection). Re-run the golden suite once #2106 is fixed and strike this
  bullet if the original VUIDs are confirmed gone.
- **`clangcl-profile` (RelWithDebInfo) once in a while** — optimized code
  paths differ from debug: different inlining, different UB exposure,
  and it is the only configuration where the benchmarks are meaningful
  (debug timings are noise). Run it after any perf-relevant change and
  before a release; it also builds `perfTestSuite.exe`.
- **`clangcl-tsan` does NOT detect data races** — checked 2026-07-20 by
  building it and inspecting the result. `cmake/Sanitizers.cmake` warns
  "clang-cl ThreadSanitizer is not supported for target
  x86_64-pc-windows-msvc" and drops the request, so the preset produces a
  plain debug build: no `-fsanitize=thread` in `build.ninja`, no `__tsan_*`
  symbols in the binary. The suite passes 40/40 under it and that result
  means nothing. This entry previously read "data races only show up here;
  nothing runs it today", which was wrong in a way that would have made a
  green run look like evidence.

  Race coverage on Windows is therefore unavailable today. `ThreadSanitizer`
  works on Linux (`linux-debug-tsan-clang`), which CI runs. **Resolved
  2026-07-20: the `clangcl-tsan` preset was removed entirely** (verified
  2026-07-31 — no `tsan` preset remains in `CMakePresets.json` outside the two
  Linux ones; AGENTS.md § "There is no Windows ThreadSanitizer" records it).
  This bullet stays as the record of why a green Windows "TSan" run meant
  nothing.
- **Synchronization validation** — `khronos_validation.validate_sync = true`
  in `vk_layer_settings.txt` next to the executable. This found 10 real
  WRITE-AFTER-WRITE hazards in July 2026; it is not part of any automated
  run, so it needs a deliberate pass after touching render passes,
  barriers, or frames-in-flight.
- **Release build** — the only configuration with logging compiled out and
  validation layers absent; behavioral surprises hide there.

## Test coverage ideas

> **This section was rewritten on 2026-07-20 because it had rotted.** An audit
> of every open item against the code found the stale ones clustered almost
> entirely here: two of its three bullets were fully done and the third was
> substantially overtaken. They rotted for a structural reason worth
> remembering — each was completed *as part of a sized item elsewhere* (the
> fuzzing `[x]` above, the GUI round-trip in Completed), and nobody walked back
> up to the unsized prose to strike it. **Any unsized prose that shadows a
> sized item will rot the same way.** Prefer extending the sized item.

- ~~Fuzz the shader file reader and KTX2/texture loading~~ — **done**;
  `Test/fuzz/shader_file_reader_fuzz_test.cpp` and
  `Test/fuzz/texture_loading_fuzz_test.cpp` exist and are registered. KTX2 was
  deliberately descoped: the C++ engine does not use it.
- ~~GUI-state round-trip test~~ — **done**;
  `Test/commit/VulkanEngine/guiSceneVarsRoundTripSuite.cpp`, and it is in the
  Windows CI filter.
- Headless offscreen assertions in the C++ engine — **re-scoped, largely done.**
  `goldenRenderSuite.cpp` now carries 22 GPU tests (forward/deferred raster,
  shadows, frustum culling, RT, path tracing incl. white furnace, textures,
  mask alpha, double-sided, KHR texture transform, second-model load). What is
  still genuinely missing is coverage of the *shadow* path beyond the single
  darkened-pixel ratio, and of the post-processing chain (both need careful
  oracle design — see `docs/gpu-golden-testing.md` on why the tonemap is hard
  to isolate).

- **GUI-input coverage — DONE (2026-07-23).** The question "is every possible
  user input tested?" was NO: the fuzz tests cover only the FILE inputs reached
  via the GUI (models/paths/shaders/textures), and the golden/integration
  suites set only a handful of GUI vars. `GoldenRender.GuiInputSweepNeverCrashes`
  now drives EVERY user-touchable control across its allowed ImGui slider/combo
  range - all ten cloud params, light dir/colour, shadow cascade count (1..8) /
  resolution / distance, PT sample/bounce counts, mode (forward/deferred/RT/PT)
  - in 16 random combinations plus the deterministic all-maximum worst case,
  asserting no crash / device-lost / validation error (debug build runs
  validation + ASan). It is a property test (no pixel oracle). Verified that
  `num_shadow_cascades` 4..8 (slider max 8 > `MAX_CASCADES` 3) is SAFE -
  `VulkanRenderer.cpp` clamps `active_cascades` before the SceneUBO write. Also
  added `GltfParseUnit.TriangleFanIsTriangulated...` and the
  `MeshRangeSlice.*` unit suite.

**Always dump the picture, not just the number** (2026-07-20).
`GoldenRender.DISABLED_DumpsFrameToPng` writes the captured frame, the same
frame with the effect disabled, and an amplified difference, to PNG:

    KATAGLYPHIS_FRAME_DUMP=out ./commitTestSuite.exe \
      --gtest_also_run_disabled_tests --gtest_filter=*DumpsFrameToPng*

A count says how much changed; only the shape says whether what changed is
the effect. This exists because measurement alone twice produced confident
wrong calls on shadows — a shadow baked into the model reported as cast, and
a classifier that only ever saw the ImGui overlay. It immediately earned its
keep by contradicting the golden shadow metric (see the open item above).

**Caution learned the hard way** (2026-07-19, cost most of a day): captures
are **tonemapped**, and the ImGui overlay is composited into them. A pixel
classifier written against raw scene colours (`r < 60 && b < 60`) silently
measured only the overlay and produced two confident, wrong conclusions
("numCascades reads 0 in the shader", "a CPU/GPU UBO race") that had to be
retracted. Any new pixel assertion needs a liveness check and an
unconditional control capture before its output is believed.

## Code quality (see `docs/code-quality.md` for the commands)

- **Decide on the formatting sweep.** 72 of 125 own sources under `Src/` and
  `Test/` do not match `.clang-format` (measured 2026-07-19). Fixing this is
  one enormous commit that will collide with anything in flight, so it wants
  a deliberate moment (right after a merge point) plus a
  `.git-blame-ignore-revs` entry. Alternative: format-on-touch only, and let
  the drift shrink over time. **Owner decision, not an agent's.**
- **Container builds now report formatting drift** (2026-07-20): every
  container build runs a non-destructive `clang-format --dry-run -Werror`
  pass and logs the count. Currently **77 of 136 files deviate**. It does not
  fail the build on purpose - with a backlog that size a failing gate gets
  switched off within a day. Make it fail once the count is near zero.
  `-SkipTidy` is still passed unconditionally, so clang-tidy remains
  uncovered (and cannot see module TUs anyway - see below).
- **clang-tidy cannot see C++23 module TUs** (module BMIs reference the
  container layout). Either run tidy inside the container, or accept that
  coverage is limited to the non-module surface.

### Deep code-review pass (2026-07-23)

Three subsystem reviews (renderer, scene/loaders, vulkan_base+memory+app),
each finding verified against the source before any change. **Applied this
pass** (all non-`.ixx`, one build+test cycle):

- **BUG `VulkanInstance.cpp` extension check** — `strcmp(...) != 0 != 0`
  parsed as `(strcmp != 0) != 0`, so `check_instance_extension_support` set
  `has_extension = true` on the first *non-matching* extension and effectively
  always returned true. Fixed to `== 0`.
- **BUG `VulkanSwapChain.cpp` swapchain usage** — `eSampled | eStorage` were
  requested unconditionally while only `eTransferSrc` was capability-guarded.
  `eStorage` is an optional swapchain usage; on a surface that lacks it
  `createSwapchainKHR` fails and `ASSERT_VULKAN` aborts. Now masks the optional
  bits against `supportedUsageFlags` (like `eTransferSrc`). No behaviour change
  on hardware that supports them; portability fix elsewhere.
- **BUG texture-index misalignment (both loaders)** — `GltfLoader`/`ObjLoader`
  `uploadParsed()` skipped `addTexture` on a decode/load failure, but each
  material's `textureID` is a dense index; a skipped slot shifted every later
  texture down one and could index past the descriptor array (malformed embedded
  image / missing `.mtl` file). Now fills the failed slot with the default
  texture to keep alignment. Valid-texture path (golden suite) unchanged.
- Dead/duplicate removals: `ASManager` double-assigned
  `instanceShaderBindingTableRecordOffset`; `VulkanRenderer` unused
  `[[maybe_unused]] toVkResult`; `Mesh` dead identity-matrix `transpose_transform`
  block; `Mesh` reserved-identifier `__vbm` alias.

**DONE — wave 2 (commit `76d05179`, `.ixx`-touching, FreshContainer + 119 tests green):**

- `memory/Allocator` is now move-only + RAII (was copyable with a
  non-releasing destructor — double-`vmaDestroyAllocator` risk). New
  `allocatorOwnershipSuite` pins the move-only contract at compile time.
- Removed dead members/resources: `Mesh::objectDescriptionBuffer`,
  `Model::texture_list` (+ getter), `VulkanImage`/`GUI` `commandBufferManager`,
  `compute_command_pool`, and the `pointShadowMap` allocation (omni shadow map
  system deleted entirely 2026-07-24, see item #12).
- `VulkanDevice` const getters; stray `eIndexBuffer` bit dropped from the two
  material storage buffers.

**DONE — wave 3 (commit `519c2329`, container-verifiable refactors, FreshContainer green):**

- `GltfLoader::parseCpu` → private `processPrimitive()` (the parse suites cover
  the decomposed path); `Mesh::create*Buffer` → one `uploadDeviceLocalBuffer<T>`;
  `VulkanDevice::getQueueFamilies()` delegates to the physical-device overload;
  `CascadedShadowMap` drops dead `getFrustumCornersWorldSpace` and stores/reuses
  the chosen depth format; `Rasterizer` single `OFFSCREEN_FORMAT`; deleted the
  dead pre-module `ObjLoader.hpp` + 3 stale IDE-filter refs.

**GPU verification is now available** (see `[[host-gpu-golden-verification]]`):
the container-built `commitTestSuite.exe` runs the golden suite on the host
RX 9070 XT from the repo root. The render-path refactors below are therefore
verified, not blind — each was confirmed by all 19-21 GPU tests passing.

**DONE — wave 4 (commit `c43af69b`, GPU-verified):** `VulkanRenderer::record_commands`
decomposed into `recordRasterPass` + `recordRaytracingOrPathTracing` (verbatim,
command order preserved); the 5-site offscreen-texture ternary collapsed to
`activeOffscreenTexture()`.

**DONE — wave 5 (commit `f1032540`, GPU-verified + new test):**
`VulkanImage::transitionImageLayout` (device overload delegates to the
command-buffer overload — barrier logic in one place); `DeferredRasterizer`
hoists the set-0 bind out of the per-mesh loop and drops the lighting pass's
dead dummy vertex input; new `TriangleFanIsTriangulatedAroundTheHubVertex`
parse test pins the fan winding.

**DONE — wave 6 (commit `fba308d7`, GPU-verified):** the `MeshRange` slice loop
+ the two near-identical structs are unified into a shared
`kataglyphis.vulkan.mesh_range` module (`sliceMeshRange` helper), `export
import`ed by both loaders. **Wave 7 (commit `780bc6bd`, GPU-verified):**
extracted `VulkanRenderer::rebuildObjectDescriptions()` for the identical
cleanUp+recreate pair at the four scene-changed sites.

**REMAINING (deliberately left, with reasons):**

- `VulkanDevice::create_logical_device` (~290 lines): **not decomposing the
  feature setup.** It is an interlinked `pNext` chain of stack locals feeding
  `createDevice`; extracting it into returning helpers dangles those pointers,
  and a dangling `pNext` can pass golden *by luck* (freed stack not yet
  clobbered). Only the queue-create-info build is safely extractable — modest
  gain, low priority.
- The AS-rebuild + descriptor-refresh steps around `rebuildObjectDescriptions()`
  are **intentionally inline, not duplication**: the four scene-changed paths
  genuinely differ (createASForScene vs createTLAS vs none; runs before or after
  the object descriptions; updateAllDescriptorSets vs
  updateTexturesInSharedRenderDescriptorSet). A unified helper would
  misrepresent four sequences as one.

**Owner decision:** `pointShadowMap` allocation was removed as dead; re-add it
(or the whole omni pass) when the point-light shadow feature is built.

## CI and release gaps

- **Latent: a `VulkanEngineCore` global constructor faults in a headless
  process** (found 2026-07-21, unsized). Surfaced by the fuzz SEGV above: some
  engine global ctor null-derefs when it runs without the app's `main()` having
  initialised GLFW/Vulkan first. The shipping app is fine (its init order holds),
  and the fuzz fix above stops *linking* it into the fuzzer, so this is not
  blocking — but a global that assumes app init is a real fragility (it would
  bite any future headless/tool use of the engine). Worth symbolizing once (build
  `scene_config_fuzz_test` the old way in the ASan container and run under
  `llvm-symbolizer`) to name the exact ctor, then either make it lazy or guard it.

- [b] **Windows CI: the `:winamd64` image is 54 GB and exhausts the runner**
  (blocked on the owner building/pushing a slim image)
  (root-caused 2026-07-21). `Build/Test/Package` failed after ~58 min: `docker
  pull` of `:winamd64` died repeatedly with `hcsshim::ImportLayer ... not enough
  space on the disk (0x70)` — it imports 54.4 GB of layers into Docker's data-root
  and the runner runs out of room; it never compiled anything. `cleanup-disk-space`
  runs but `docker system prune` reclaims 0B on a fresh runner. A **disk
  diagnostic** now prints per-drive free space + the data-root before the pull and
  fails fast if the data-root drive can't hold ~54 GB (owner chose "diagnostic
  first").

  **What's in the 54 GB** (ContainerHub `windows/build.ps1`): the chain is
  `base → nvidia (CUDA+cuDNN+TensorRT ~50 GB) → toolchain (clang/cmake) → media
  (ONNX/GenAI+OpenCV+FFmpeg+LiteRT+TVM+GStreamer) → final`. **The graphics engine
  + wgpu renderer need NONE of it** — verified: `RUST_FEATURES=ON` builds the whole
  RustProjectTemplate workspace, but `crates/media` and `crates/inference` both
  have `default = []` (gstreamer/onnx/CUDA are optional, non-default), so the build
  pulls zero media/ML system libs; it needs only clang-cl + cmake + Vulkan + the
  Rust toolchain.

  **Fix (owner builds the image) — command CORRECTED 2026-07-22 against
  ContainerHub `windows/build.ps1`:** the stage chain is
  `base → sdk → toolchain → media → final`, and `toolchain` builds
  `FROM windows-sdk`, so `sdk` cannot be skipped - but on the CPU lane (no
  `-Gpu`) the `sdk` stage is just `docker tag windows-base windows-sdk` (no
  CUDA/nvidia). So the exact command is:

  ```pwsh
  .\windowsuild.ps1 -Stages base,sdk,toolchain   # NO -Gpu
  ```

  That produces `local/kataglyphis:windows-toolchain` (build.ps1:632) - the
  clang-cl + cmake + Vulkan + Rust image with NO media/ML stack. Tag and push
  it as the slim consumer image:

  ```pwsh
  docker tag local/kataglyphis:windows-toolchain ghcr.io/kataglyphis/kataglyphis_beschleuniger:winamd64-toolchain
  docker push ghcr.io/kataglyphis/kataglyphis_beschleuniger:winamd64-toolchain
  ```

  Then repoint `WINDOWS_CONTAINER_IMAGE` in BOTH consumers - the superproject's
  `Windows.yml` and RustProjectTemplate's `rust_windows2025.yml` (and the
  superproject `Build-Windows-Container.ps1` default) - at `:winamd64-toolchain`.
  Mirrors the Linux `:toolchain` split. Relocating Docker data-root only helps
  if a drive has >54 GB free (the diagnostic says); the slim image is the
  reliable fix. **The previously-written `-Stages base,toolchain` would throw
  "requires existing image not found: windows-sdk" - do not use it.**

- **Windows CI runs the CPU-only tests** (since 2026-07-20): 36 tests across
  BuildIntegrity, CameraUnit, SceneConfigUnit, CascadedShadowMapUnit,
  GuiSceneVarsRoundTrip and HelloTestCommit, plus the three fuzz targets'
  seed corpora. Runs in ~14 ms. **The GPU suites (Integration, GoldenRender)
  still do not run anywhere except locally** - they are excluded by name
  rather than left to self-skip, because the container ships the Vulkan
  loader and `SKIP_WITHOUT_GPU` only asks `glfwVulkanSupported()`, which can
  answer yes with no device present and then abort during device creation.
  Closing that gap needs a self-hosted runner with a GPU. **A suite added to
  the repo does not run in CI unless it is added to the filter in
  `Windows.yml`.**

  **And none of it runs by default.** `Windows.yml` is gated on
  `if: contains(github.event.head_commit.message, '[build-win]')`, so the
  whole workflow — build included — is skipped unless a commit message opts
  in. That predates this work and is presumably a runner-cost decision, but
  it means "Windows CI passes" is usually a statement about a workflow that
  never ran. Worth deciding deliberately: run on PRs to `main`, run nightly,
  or keep it opt-in and stop treating a green tick as Windows coverage.
- **Packaging paths are never exercised.** DEB (`linux-release-deb`), WiX
  (`windows-clang-release-wix`) and MSIX are configured but nothing builds
  them in CI, so breakage surfaces at release time.
- **Coverage is clang-only** (Linux). GCC and Windows contribute no
  coverage data, which skews what Codecov reports.
- ~~**Docs builds are unverified.**~~ — **done** (2026-07-31): `docs-build-web.sh`
  now runs `make html` with `SPHINXOPTS="-W --keep-going"` and a follow-up
  `make linkcheck` (also `-W`), so a broken internal link, missing toctree
  page, or malformed rST title fails the step and blocks the FTP deploy after
  it. External URLs are excluded via `linkcheck_ignore` in `conf.py` (they
  flake; this gate is for OUR pages). Local warning count was 6 before the
  fix (below the "don't add `-W` if the count is large" threshold): a
  toctree/xref gap for `graphviz_files` (generator wasn't run locally), the
  same for `api/library_root` (only exists once Exhale runs, which needs
  Doxygen XML nothing currently produces — `conf.py` now writes a placeholder
  page when that XML is absent, mirroring the existing `graphviz_files.rst`
  fallback), a docutils title-underline-too-short warning in
  `wsl2_vulkan.rst`, and a stray `docs/source/_static/VULKAN.md` that the
  `linkcheck`/`latex` builders read as an orphan page even though the `html`
  builder silently excludes `html_static_path` (fixed by adding
  `_static/**` to `exclude_patterns`, so every builder agrees).
- **Docs placement audited** (2026-07-21) - the split is intentional and clean:
  `docs/source/` is the Sphinx site (every source page is in the `index.rst`
  toctree, none orphaned); `docs/*.md` at the repo root are deep dev-reference
  docs, linked FROM the site (e.g. `webgpu_demo.md` -> `webgpu-renderer-roadmap.md`)
  but not built into it. Open CHOICE, not a defect: those root dev-docs
  (roadmaps, cpp-renderer-improvements, shader-*, sRGB audit) are invisible on
  the published site; decide per-doc whether any should move into `source/` +
  the toctree to be surfaced.
- **Golden-image CI** for the Rust renderer: the headless tests already
  render; storing reference images per GPU vendor would catch shader
  regressions that structural assertions miss (they were designed to be
  driver-independent, which is also their blind spot).

## Startup and build-time costs

- **Build transfers dominate (~17 GB/build).** Incremental builds work
  (~230 s vs ~360-480 s cold) but 8.5 GB moves each way. A long-lived build
  container with source-only re-sync would remove both transfers entirely;
  needs lifecycle handling and a way to extract executables for host tests.
- **Outbound `Artifact extraction failed (exit 1)`** is still reported even
  with the cargo subtree excluded. Artifacts do arrive (verified), but a real
  failure here would leave stale host binaries — worth a proper fix.
- **sccache: every write fails, and modules bypass it entirely** (measured
  2026-07-20; corrects the previous "writes nothing (0 bytes)" note, which was
  wrong - the cache holds 981 KiB and simply never grows).

  Two independent problems, worth separating:

  1. **Every attempted write errors.** Reproduced twice: 66 write errors from
     66 misses in one build, then 1 from 1 in a single-file rebuild. The cache
     size does not move between runs. Cause still unknown - `SCCACHE_ERROR_LOG`
     and `SCCACHE_LOG` are now passed to the container (they were not), the
     server was stopped so it would restart and pick them up, and **no error
     log file appeared**. Next thing to try: run sccache by hand inside the
     container against a trivial TU, outside the build orchestration, so the
     failure is not buried in ninja output.
  2. **C++23 module TUs never reach sccache at all.** Module BMI compiles
     invoke `clang-cl.exe` directly rather than through
     `CMAKE_CXX_COMPILER_LAUNCHER`, so most of this build is uncacheable
     regardless of (1). Even a perfect fix to the write errors leaves the hit
     rate bounded by the non-module surface. That reframes the whole item: it
     is worth much less than "20 GiB cache, 0% hit rate" suggests.

  Related gotcha found while trying to force a rebuild: **touching a source
  file usually does NOT cause the container to recompile it.** One touch
  produced a rebuild, three later ones produced none. That makes "touch and
  rebuild" unreliable as a workflow here and is consistent with the tar
  extraction issue already recorded below - worth pinning down, since it also
  means a real edit could in principle be missed.
- **`-FreshContainer` strands the build cache** on the wcifs fallback path:
  the next build takes 367 s instead of 44 s.
- **Module dependency scanning** (`clang-scan-deps`) runs over all 53
  `.ixx` files each configure; measure before assuming it is free.

## Developer-experience papercuts (all hit during the 2026-07 campaign)

- Host `cmake` is 3.29 and **cannot read this repo's `CMakePresets.json`**
  (`version: 10`); only the container's newer CMake can. Anyone running
  `cmake --list-presets` on the host gets a confusing parse error. Host
  `ctest` cannot read the build trees either — run the gtest executables
  directly.
- **LLVM is not on `PATH`** despite being installed — see
  `docs/code-quality.md` for the absolute paths.
- **`run_clangcl_debug.ps1` sets `VK_LAYER_PATH = ''`**, which crashes the
  app at startup with `0xC0000409`. Launch with
  `VK_LAYER_PATH='C:\VulkanSDK\1.4.350.0\Bin'`.
- **Swapchain screenshots read black while the desktop session is
  locked**, with no error — a capture path that silently lies. Always
  take a control capture of a known-good app before believing a black
  frame is a regression. The offscreen capture path used by the golden
  tests does *not* have this problem.
- **Restoring a file from a backup can defeat ninja.** `Move-Item` restores
  the original mtime, so if the backup is older than the compiled object,
  the rebuild is skipped and you test the old binary while believing you
  reverted. Touch the file after restoring.
- **Build containers occasionally survive a successful build**
  (`wcifs teardown lock`); a stale container makes it look like a build is
  still running. Compare the newest `logs/windows/build-summary-*.json`
  timestamp against container start before assuming.
- `Scripts/Windows/Build-Windows-Container.ps1` takes `-Configurations`,
  not `-Preset`; passing the wrong one silently builds **all four**
  configurations.
- ~~A source file deleted on the host keeps building inside the reusable
  container~~ — **done** (`37a7fdbf`, 2026-07-24). Reproduced 2026-07-19: added
  a probe test, built (it ran), deleted the file, rebuilt — the test still
  ran, and the `.cpp` was still present at `C:\ws\...` inside the container.
  `Invoke-TarPipeBuild` in `Build-Windows-Container.ps1` now prunes the
  reusable container's source tree before streaming in fresh sources on every
  reused-container build, keeping only `build`/`build-*`/`build_*`, `logs` and
  `sccache-local`.

## Architecture debt not yet sized

- **The glTF material fetch is duplicated across three fragment shaders** —
  **DONE (ee05b636)**: extracted into `common/material_fetch.glsl`
  (`fetch_object_description(objectIndex)` + `fetch_material(obj_res)`), replacing
  the identical `object_description`/`MaterialIDs`/`Materials` walk in the forward,
  deferred, and shadow fragment shaders. Also dropped the dead Vertices/Indices
  buffer_reference decls the forward + deferred shaders carried but never used.
  GLSL only, behaviour-preserving: all three recompile, 15/15 goldens unchanged,
  validation-clean (no C++ rebuild needed).

- **`VulkanRenderer` is still the hub.** PipelineBuilder (-416 lines) and
  DescriptorSetGroup (-617) shrank it a lot, but it still owns the
  swapchain, sync objects, UBOs, five stages and four foreign pointers
  (`Window*`, `Scene*`, `GUI*`, `Camera*`). Candidate extractions:
  `FrameSync` (fences/semaphores/frame index), `SwapchainTarget`
  (swapchain + framebuffers + recreation), a stage registry so adding a
  pass does not mean editing the renderer.
  - **FrameSync — DONE 2026-07-23 (`e7e7579d`, module `kataglyphis.vulkan.frame_sync`; 22 golden green).** For reference, the state was
    `current_frame`, `image_available` + `in_flight_fences` (sized per
    frame-in-flight / `MAX_FRAME_DRAWS`), and `render_finished_by_image` +
    `images_in_flight_fences` (sized per SWAPCHAIN IMAGE). **The per-image vs
    per-frame split is load-bearing and the whole reason to be careful:**
    render-finished is per-image (a per-frame render-finished semaphore can be
    waited on before it is signalled across a swapchain recreate). A `FrameSync`
    class owns these five members + `MAX_FRAME_DRAWS`, with
    `create(device, image_count)` / `cleanUp()` mirroring today's exact sizing,
    `waitForCurrentFrame()`, `advance()`, and handle accessors for the
    acquire/submit/present path; `drawFrame` keeps its acquire→submit→present
    flow verbatim but reads handles from the instance. Do NOT collapse
    `render_finished_by_image` to per-frame. GPU-verifiable: sync mistakes
    surface as validation errors / device hangs — run all 22 golden tests on the
    RX 9070 XT (`docs/gpu-golden-testing.md`). Touches `VulkanRenderer.ixx` →
    FreshContainer.
  - **SwapchainTarget — investigated 2026-07-23, NOT a clean extraction (skip it).**
    First added the missing recreation coverage (`SwapchainRecreationKeepsRendering`,
    `9b202b68`). Then reading `recreateSwapChain` end to end showed the suggestion
    doesn't hold: the swapchain is *already* a `VulkanSwapChain` class, the
    framebuffers belong to the **stages** (postStage/rasterizer/deferred/skybox
    each own theirs), and `recreateSwapChain` is inherently cross-cutting
    orchestration — it destroys+recreates the stage framebuffers, the GPU-timing
    query pool, the path-tracing accumulation target, and, on an image-count
    change, the UBO vectors + descriptor pools/sets + RT descriptors. A
    "SwapchainTarget" owning "swapchain + framebuffers + recreation" would have
    almost nothing to own and would still need every stage handle, so it does not
    decouple anything. Coordinating recreation IS the hub's job. Better hub-shrink
    targets: the descriptor/UBO re-provisioning could move behind a
    `reprovisionPerImageResources()` helper, but that is a small readability win,
    not a class extraction. (**Done** — verified 2026-07-31: the helper exists at
    `VulkanRenderer.cpp:635-647` and the image-count branch of
    `recreateSwapChain` calls it at `:708-713`; nothing left to extract here.) Aside: the cascade `# cascades` GUI slider still
    advertises 1..8 though the engine clamps to `MAX_CASCADES`(3) (`c49fd8b4`);
    left as-is because GUI has no clean access to `MAX_CASCADES` and the clamp
    already fixes the bug.
- **Device-lost teardown is special-cased in `App.cpp`** (scene/GUI
  cleanup is skipped) — a symptom of ownership living in the wrong place.
  Full RAII up the stack would remove the special case entirely.
- **`GUI*` is a mutable cross-cutting dependency**: both `Scene` and
  `VulkanRenderer` read GUI state each frame. A plain settings struct
  owned by the app, passed by const reference, would decouple them.
- **Multi-object rendering works** (2026-07-20). The shaders index
  `object_description.i[pc_raster.objectIndex]` rather than hard-coding 0,
  `Scene::loadAdditionalModel` / `VulkanRenderer::addModel` can add a model
  without replacing the scene, and `GoldenRender.SecondModelLoadsAndRenders`
  loads a second model at index 1 and asserts it reaches the draw loop and
  changes the frame.

  Still not isolated: the test proves a second model loads, is counted and
  contributes pixels, but does not prove the index ARITHMETIC - two models
  whose materials differ enough to tell apart driver-independently would be
  needed for that. The layout contract is guarded in `pushConstantSuite.cpp`.

  The scene still loads one model by default; a multi-model debug scene is a
  separate decision about what the app should open on.

## Rust renderer ideas (unsized)

- **Render-graph v2**: the current graph validates declared read/write
  wiring but does not schedule or alias resources. Automatic barrier
  placement and transient-resource aliasing are the natural next steps —
  worth it only when pass count grows again.
- **Texture streaming / bindless**: the renderer binds per-primitive sets;
  a bindless array plus streaming would be needed for photogrammetry-scale
  scenes (the Colosseum case).
- ~~**wgpu timestamp queries** to mirror the C++ per-pass GPU timings~~ —
  **done**: `render/gpu_timing.rs` (`TimedPass`, per-pass averaged ms) and the
  `dump_gpu_timings` example + `Scripts/Compare-RendererTimings.ps1` already
  compare timings across renderers, not just pixels.
- ~~**Wasm size budget**~~ — **done**: `Scripts/Linux/wasm-size-budget.sh`
  (CI) / `Scripts/Test-WasmSizeBudget.ps1` (local) build wasm32-unknown-unknown
  release, run `wasm-opt -Oz`, and fail above a 12 MiB budget; wired into
  `Linux.yml` ahead of the docs deploy. The ~3.7 MB figure was stale — measured
  post-opt size is ~8.3 MiB, never previously enforced.

## Housekeeping candidates

- The `x64-Clang-Windows-Release` preset survives only because
  `windows-clang-release-wix` packages from it; if WiX packaging moves to
  ClangCL, that preset can go too.
- `imgui.ini` is tracked and changes whenever a window is dragged — **DONE
  (2026-07-24)**: added to `.gitignore`; `git rm --cached` stopped tracking
  the existing file. It is user state, not a shared layout.

---

## 2026-07-22 deep-dive candidates

Found by a full read of both renderers against the existing backlog (nothing here
duplicates the sections above). Ordered by value-per-effort within each renderer.
Evidence is `file:line` at the time of writing.

### C++ Vulkan engine

**These became CI-testable on 2026-07-22.** The Windows container lane now
builds the engine and runs the CPU test suite in CI (it previously never got past
`docker run`), so items below that were "needs a container build to verify" can
now be proven by pushing with `[build-win]` instead of only on a dev box. That
matters most for #1 (cascade near-plane clipping) and #9 (cascade texel
snapping), which the survey flagged as provable with pure CPU gtests in
`cascadedShadowMapSuite` - they can now go red-then-green in CI like any other
test. Note the fuzz step runs there too, so #14 (cgltf fuzzing) has a home.


1. **Shadow casters in front of a cascade's near plane are clipped away** —
   **DONE (7a1a4ade)**: fixed via DEPTH CLAMP (PipelineBuilder::setDepthClamp
   + guarded depthClamp device feature on the CSM pipeline), not near-plane
   extension - the latter was tried and REGRESSED ShadowsDarkenSomePixels
   because the shader bias is constant in normalized depth, so widening the
   range scaled it in world units and ate contact shadows. Original text: —
   `isVisibleAsShadowCaster` deliberately drops the near plane so tall geometry
   still casts (`Frustum.cpp:85-88`, with a test asserting it), but the cascade
   ortho near plane is fitted from camera-frustum corners with a fixed 10-unit pad
   (`CascadedShadowMap.cpp:225-227`) and the shadow pipeline sets
   `depthClampEnable = VK_FALSE` (`PipelineBuilder.cpp:115`). A ceiling/overhang
   further than the pad casts NO shadow: the CPU keeps it, the rasterizer deletes
   it. Fix: enable `depthClamp` (needs the feature, not requested at
   `VulkanDevice.cpp:487-494`) or extend the near plane to scene bounds along the
   light axis. Test: pure CPU in `cascadedShadowMapSuite` — a point 30 units toward
   the light must transform to NDC `z >= 0`; fails today.
2. **Lit target is `R8G8B8A8_UNORM`** — **DONE (2026-07-22, with the #8
   lighting fix in one unit)**: FP16 offscreen (Rasterizer x2, DeferredRasterizer
   offscreen+finalFormat, rgen+PT storage qualifiers). With diffuse finally
   scaling by the light, the whole scene exceeded 1.0 and the UNORM target
   clamped flat - the two only work together, exactly as the null-result
   sequencing predicted. Post's Reinhard now does real work; PT's 186 ceiling
   is gone (its light golden jumped 0.027 -> 0.751). Historical note below. — implemented and REVERTED after three oracles showed the format
   change is indistinguishable on this scene: whole-frame mean moves ~+0.76 for
   a radiance 2->8 sweep on UNORM and FP16 alike; bright-pixel counts (>200,
   the post-Reinhard UNORM ceiling is ~186) are flat on BOTH at radiance 8 AND
   at radiance 25. Root cause found in the process: `pbrBook.glsl:85` - the
   Lambertian diffuse term is `LambertDiffuse(ambient) * CosTheta(L,N)` and
   NEVER multiplies light_color or light_intensity, so radiance only enters
   via the small specular term and scene luminance never approaches 1.0.
   Sequencing: fix #8 (lighting actually consuming intensity/diffuse/roughness)
   FIRST, then the FP16 targets + rgen rgba16f (sites known: Rasterizer.cpp
   x2, DeferredRasterizer offscreen+finalFormat, raytrace.rgen:26), proven by
   a bright-pixel-delta golden across a radiance sweep that crosses the 186
   ceiling - the whole-frame-mean oracle is measured useless for this.
   Original item: —
   `post.frag:32` applies Reinhard, but the offscreen target is UNORM
   (`Rasterizer.cpp:206`, `:328`, `DeferredRasterizer.cpp:88`) while the G-buffer
   correctly uses `R16G16B16A16Sfloat` (`:89-90`). The radiance slider does nothing
   above the clip point and the whole tonemap/bloom stage is decorative. Test:
   `GoldenRender` at radiance R vs 2R — mean luminance must rise; today it does not.
3. **Only model 0's textures bind, and only the first 24** — **DONE
   (2026-07-22)**: per-model texture_offset in ObjectDescription + all four
   shader fetch sites + flattened binding across all models (warn on cap
   overflow). Golden: sponza-as-second-model must show texture DETAIL in the
   crop (0.045 green vs exactly 0 with the model-0-only binding) - a colour
   oracle was measured blind twice: the dinosaur's mtl ships NO textures at
   all, and sponza's bricks are near-greyscale. En route: untextured .mtl
   materials got textureID 0 (not -1), so the bundled dinosaur rendered its
   Kd colours as flat white for the engine's whole life - fixed in the
   loader with a CPU red/green test. Original text: —
   `updateTexturesInSharedRenderDescriptorSet` hard-codes `getTextures(0)`
   (`VulkanRenderer.cpp:1644`) and clamps to `MAX_TEXTURE_COUNT = 24` (`:1650`).
   The release default scene is Sponza (`SceneConfig.cpp:121`), which has far more
   materials — everything past 23 renders with the wrong texture, and a second
   `addModel` is textured with the first model's array. `runtimeDescriptorArray`
   is already enabled (`VulkanDevice.cpp:591-593`), so a per-scene flat table with
   an offset in `ObjectDescription` is the natural fix.
4. **Moving the model in the GUI never rebuilds the TLAS** — **DONE
   (2026-07-22)**: the transform path rebuilds the TLAS only (BLAS geometry
   untouched) before the descriptor update, which binds the new handle and
   resets PT accumulation. Golden RaytracedWorldFollowsTheModelTransform:
   green 0.397 swung fraction, stale-TLAS red EXACTLY 0 (deterministic RT).
5. **`raytrace.rchit` lights in object space and transforms the normal with `w=1`** (S) —
   ~~`:88` uses `vec4(normal_hit, 1.0)` (picks up translation; a normal needs the
   inverse-transpose), and `:103-104` mix object-space `N`/`hit_pos` with
   world-space `L`/`cam_pos`.~~ **DONE (PT correctness batch, 2026-07-22)** —
   plus the untextured-material clamp-to-slot-0 fetch, same batch. ~~STILL OPEN: `:130-131` hard-codes light colour/intensity~~ **DONE
   (2026-07-22, forward-lighting unit)** - rchit now reads sceneUBO.dirLight;
   the item is fully closed.
6. **glTF is unreachable from the GUI, and `reloadModel` is OBJ-only + null-unsafe** —
   **DONE (2026-07-22)**: the scan accepts .obj/.gltf/.glb case-insensitively
   (red: the list test fails "OBJ-only again"); reloadModel dispatches by
   extension; add_model null-guards (red: SEH 0xC0000005 access violation on
   the shipped binary - the crash was real). Original text: —
   `scanAvailableModels` filters `== ".obj"` (`SceneConfig.cpp:96`, case-sensitive),
   so the in-tree `cube.glb` can never be picked; `Scene::reloadModel` constructs
   `ObjLoader` directly (`Scene.cpp:177`) instead of `loadModelByExtension`, and
   passes the result to `add_model` with no null check (`:179` vs `:153`) — a
   malformed asset is a null-deref. Test: CPU-only, all three behaviours.
7. **Base-colour textures upload as UNORM, then post applies gamma again** —
   **DONE (2026-07-22)**: eR8G8B8A8Srgb for texture uploads (real + default);
   the hardware decodes to linear at sample time. A/B census on the default
   scene: 16.5k pixels shift (3.5%, exactly the textured skeleton), max delta
   243; forward/deferred parity held at 0.20 through the change. Whole-frame
   channel means were BLIND to it (skeleton too small) - the A/B frame dump
   was the instrument. Original text: —
   `Texture.cpp:120`, `:194` use `eR8G8B8A8Unorm` for sRGB-encoded PNG/JPG, then
   `post.frag:34` does `pow(.,1/2.2)`. Albedo is systematically too bright and mips
   average in the wrong space. Narrow fix: `eR8G8B8A8Srgb` for base colour only
   (normal/ORM must stay UNORM).
8. **Forward shading ignores material diffuse and roughness** — **DONE
   (2026-07-22)**: both raster paths consume material.diffuse (untextured
   fallback - the clamp-to-slot-0 defect PT/RT had) and map shininess ->
   roughness (Beckmann), replacing the hard-coded 0.9 that DEFERRED also
   wrote into its own G-buffer (lighting.frag "reading the material" was an
   illusion). Found underneath: tinyobj's -1 no-material face id was cast to
   0xFFFFFFFF, so every shader material fetch on every untextured model was
   an OUT-OF-BOUNDS buffer-device-address read - fixed in ObjLoader with a
   CPU red/green test. Rig lit luminance 166.9 -> 158.1 proves the whole
   loader->slot0->diffuse chain live. Original text: —
   `shader.frag:86-91` builds ambient from the texture alone, leaves `diffuse`
   commented at `:89` and hard-codes `roughness = 0.9` at `:91`, nullifying the
   glTF material mapping in `GltfLoader.cpp:106-129`. The deferred path reads both
   (`lighting.frag:48-49`), so the two raster paths disagree on materials.
9. **Cascades refit per frame with no texel snapping — shadow edges crawl** —
   **DONE (5aca1c2b)**: world-fixed light basis + box sized from the slice
   bounding radius + centre snapped to whole texels + one-texel pad; opt-in
   via a shadowMapResolution param that updateCascades passes. Three tests
   incl. a legacy red control (the old path must crawl or the assertions are
   vacuous). Shadow golden IMPROVED to 9.78% darkened. Original text: —
   `computeCascadeData` derives the box from exact frustum corners each frame
   (`CascadedShadowMap.cpp:201-216`) so it translates AND resizes; the
   stabilisation ingredient (`radius`, `:187-190`) is already computed but only
   used for eye placement. Fix: size from `radius`, snap origin to whole texels.
   Test: two camera positions a fraction of a texel apart — origins must differ by
   an exact texel multiple and box width must be identical. Both fail today.
10. **A `Model` can hold exactly one `Mesh`** (L) — **✅ DONE (loader split 1d40e176,
    render golden f008b501)**: a multi-primitive glTF now loads as one Model with
    one Mesh per primitive, end-to-end. `GltfLoader::parseCpu` records a per-primitive
    `MeshRange` (turnkey approach below — flat getters + GltfParseUnit untouched);
    `uploadParsed` slices each range into its own `add_new_mesh`, and `adoptParsed`
    moves the ranges so the async path builds the meshes too. Verified: the
    `MultiPrimitiveGltfLoadsAsMultipleMeshes` render golden loads two_primitives.gltf
    and asserts `visibility.meshes_total == 2` through the real render loop, plus
    `MultiPrimitiveGltfRecordsPerPrimitiveMeshRanges` covers the CPU parse; 14/14
    goldens + 13/13 GltfParseUnit green, validation-clean. The RT/PT
    gl_GeometryIndexEXT>0 case is correct-by-inspection (raster golden exercises the
    flat objectIndex; an RT-mode colour oracle is a possible future hardening). The
    remaining optional refinement is per-mesh material subsets (each mesh currently
    shares the full materials array — correct, just not minimal). Historical detail
    below.

    **FOUNDATION DONE (f20b1234)**:
    `Model` now holds `std::vector<Mesh>` (getMeshCount → size, getMesh(i) →
    meshes[i], add_new_mesh appends, cleanUp/getPrimitiveCount iterate), and
    `Scene::add_model` flattens one object description PER MESH. Deliberate no-op
    today (loaders make one mesh/model, so flat mesh index == model index);
    102/102 CPU + 15/15 goldens unchanged, validation-clean. Model is now
    multi-mesh-CAPABLE. **objectIndex now the FLAT mesh index (84e4fd45)**: the
    forward/deferred/shadow record loops push it per mesh (running count, advancing
    for culled meshes too, push moved after the visibility test), so the RASTER
    render path is fully multi-mesh-correct; 15/15 goldens unchanged, no-op today.
    Per-mesh culling already falls out (the loops iterate getMeshCount + per-mesh
    AABBs). **REMAINING**: split a loader's primitives into separate meshes - the
    load-bearing step (`GltfLoader::parseCpu` flattens all primitives, `loadModel:79`
    calls add_new_mesh once → restructure to per-primitive geometry + loop
    add_new_mesh; ripples to the loader API + the GltfParseUnit tests asserting on
    the flat getVertices/getIndices + the async loader). **AS + RT/PT DONE**: the
    AS already built one BLAS per MODEL with one GEOMETRY per mesh (`:53-70`,
    iterates getMeshCount), and the RT/PT kernel material fetch now uses
    `instanceCustomIndex (= model's first-mesh flat index, a running base in
    createTLAS) + gl_GeometryIndexEXT` (1e5d0a35), so the WHOLE render path (raster
    + AS + RT/PT) is multi-mesh-correct; 15/15 goldens unchanged. **So the ONLY
    remaining #10 work is the loader split** - once a loader emits >1 mesh per
    Model, everything downstream already handles it (and it is what finally
    EXERCISES the multi-mesh path - today it is verified no-op + correct-by-
    inspection, since two separate models drive the flat objectIndex but never the
    RT/PT gl_GeometryIndexEXT != 0 case).

    **TURNKEY loader-split approach (2026-07-23, cleaner than the "API/test ripple"
    fear - it does NOT touch the flat getters, so GltfParseUnit is unaffected):**
    `parseCpu`'s primitive loop already has `base` (the primitive's vertex-base
    offset) and `primIndexStart` (`GltfLoader.cpp:314,335`). Record a per-primitive
    range `{vertex_base=base, vertex_count, index_start=primIndexStart, index_count,
    tri_start}` in a new member vector; keep the flat vertices/indices/materialIndex/
    materials exactly as they are (getters + tests unchanged). Then in `loadModel`
    (and the async `uploadParsed`/`adoptParsed`) loop the ranges: per primitive,
    slice `vertices[vb:vb+vc]`, `indices[is:is+ic]` re-based by `-vb`,
    `materialIndex[ts:ts+tc]`, share the full `materials`, and call `add_new_mesh`
    per primitive. Add a 2-primitive test glTF (two quads, two materials) + a golden
    asserting getMeshCount()==2 and both render (distinct materials, and one in
    RT/PT mode to hit gl_GeometryIndexEXT=1). ABI-skew only if the range vector goes
    in a .ixx-exposed type. Original state: `getMeshCount()` returned literal
    `1` and `getMesh()` ignored its index (`Model.ixx:38-39`); `add_new_mesh`
    overwrote (`Model.cpp:47`). Culling is all-or-nothing on one scene-sized AABB
    (`Rasterizer.cpp:122-141`), and there is nothing to attach LOD to.

    **DESIGN NOTE (2026-07-22, prepared so the L does not start cold):** the
    2026-07-22 texture_offset work already established the pattern this needs -
    per-DRAW identity flows through ObjectDescription + pc_raster.objectIndex,
    and the flattened-resource binding (textures) generalizes to meshes. Plan:
    (1) Model holds `std::vector<Mesh>`; add_new_mesh appends;
    object descriptions become one PER MESH (objectIndex = flat mesh index,
    offsets computed exactly like texture_offset in
    create_object_description_buffer); (2) per-mesh AABB from the loader,
    culling iterates meshes not models - the all-or-nothing cull falls out
    immediately; (3) AS: one BLAS per mesh (ASManager already loops a blas
    vector; feed it meshes), instances keep model transform; (4) LOD attaches
    per mesh afterwards. Biggest ripple: everything indexing model_list[i]
    1:1 with object_descriptions[i] (the texture_offset loop among them) -
    grep `getObjectDescriptions` consumers first. Suite guards: parity +
    multi-model + transform-follow goldens all exercise the flattening
    invariants already. This is the
    enabling change for several already-wanted features.

    **CALLER/CONSUMER MAP (2026-07-23, makes it mechanical):** (1) `Model` holds
    a single `Mesh mesh` (`Model.ixx`); `getMeshCount()` hardcodes 1, `getMesh()`
    ignores its index, `getObjectDescription()` returns `mesh.getObjectDescription()`.
    ABI skew (Model is a module-interface class embedded in Scene) -> delete-build +
    -FreshContainer. (2) `add_new_mesh` is called EXACTLY ONCE per Model - only from
    `GltfLoader.cpp:79` and `ObjLoader.cpp:129` - so switching it from overwrite to
    append is safe (no existing double-call to break). (3) The object-description
    flattening lives in `Scene::add_model` (`Scene.cpp:159`):
    `object_descriptions.push_back(model->getObjectDescription())` - change to loop
    the model's meshes and push each mesh's OD, so `Scene::object_descriptions`
    becomes one-per-MESH. (4) `VulkanRenderer::create_object_description_buffer`
    (`:1463`) consumes `scene->getObjectDescriptions()` unchanged; what changes is
    the meaning of `objectIndex` - it must become the FLAT mesh index (sum of prior
    models' mesh counts + local k), pushed per draw in the forward/deferred/shadow
    record loops (`Rasterizer.cpp:115` `pushConstant.objectIndex = m`,
    `DeferredRasterizer.cpp:468`, and CSM's `makeShadowPush(..., m)`). (5) AS: one
    BLAS per mesh (ASManager already loops a blas vector). Do it as: data structure
    + per-mesh OD first (goldens must stay green - still 1 mesh/model so it is a
    no-op), THEN split a loader's primitives into multiple meshes to actually
    exercise it.

    **OBJ PARALLEL — ✅ DONE (09dadaa5, 2026-07-23):** `ObjLoader` used to flatten
    every OBJ shape (`o`/`g` group) into ONE mesh (the pre-#10 glTF limitation);
    now each shape becomes its own Mesh, so #10's whole downstream
    (objectIndex/culling/AS/RT-PT) applies to OBJ too. The glTF turnkey
    `MeshRange {vertexBase, vertexCount}` slice did NOT transfer directly: glTF
    primitives parse into disjoint contiguous vertex ranges, whereas ObjLoader's
    `vertices_map` dedup was declared ONCE OUTSIDE the shape loop, so vertices
    were deduped GLOBALLY - a shape's indices could reference a vertex first
    emitted by an earlier shape. Fix taken (approach a): reset `vertices_map` per
    shape so each shape's vertices form a contiguous block, at the cost of
    duplicating any vertex shared across distinct shapes (rare between separate
    objects). KEY INSIGHT that de-risked it (the original "GPU goldens must be
    re-baselined" fear was WRONG): per-shape dedup produces PIXEL-IDENTICAL
    geometry (same triangles, vertices just stored per-shape not globally shared),
    so pixel goldens do NOT shift from the vertex-count change - only CPU
    count-assertions (which use the preserved flat getters, so unaffected) and
    per-mesh culling could, and the sponza golden (373 shapes) held at detail
    0.031 > 0.02. parseCpu keeps the flat arrays; uploadParsed slices one
    add_new_mesh per range; adoptParsed moves the ranges (async path). Verified:
    ObjParseUnit multi-shape (dinosaurs, contiguity/coverage/in-block invariants)
    + single-shape (shadow_rig), 14/14 goldens, 106/106 suite, 12s validation- +
    ASan-clean run loading 3-shape dinosaurs as a multi-mesh model. Optional
    future: per-mesh material subsets (shared with the glTF split's follow-up).
11. **glTF loader gaps** (M) — skinned-node transforms are applied though the spec
    says ignore them (`GltfLoader.cpp:231`); ~~missing `NORMAL` becomes a
    constant `(0,1,0)`~~ **NORMAL fallback DONE (2026-07-22)**: absent normals
    now get per-triangle flat normals from world-space positions (matching the
    OBJ path), degenerate triangles skipped; CPU red/green with an XY-plane
    triangle whose true normal is +/-Z. ~~non-triangle primitives silently
    skipped~~ **strip/fan triangulation DONE (2026-07-22)** - triangle strips
    and fans are now triangulated (points/lines stay skipped, undrawable
    here); CPU test on a 4-vertex strip -> 2 triangles. STILL OPEN in this
    item: `alphaMode`/`doubleSided`/`KHR_texture_transform`/texcoord index all
    ignored, so transparent glTF renders opaque.

    **SCOPING NOTE (2026-07-22, so this does not start cold — re-sized M -> the
    MASK slice is S/M, the full item is L):** the four gaps split cleanly by cost.
    - *Infrastructure already present:* `PipelineBuilder` has a working
      src-alpha/one-minus-src-alpha blend state (`PipelineBuilder.cpp:144-149`,
      gated by its blend flag), so a BLEND pipeline variant is a builder call, not
      new Vulkan. What is missing is entirely material + shader + loader side.
    - *`ObjMaterial` is a SHARED C++/GLSL struct* (`Src/shared/scene/ObjMaterial.hpp`,
      `#ifdef __cplusplus`) read directly by the fragment shaders. It already carries
      `dissolve` (OBJ `d`/opacity, default 1.0). Adding any field (an `alphaCutoff`
      float + a mode int, or reuse `dissolve`+`illum`) crosses the module boundary =
      **ABI skew: delete build-clangcl-debug + `-FreshContainer`** (see
      [[cpp-renderer-fix-campaign]]).
    - *Increment 1 — alphaMode MASK cutoff (S/M)* — **SHADING DONE, verified
      (5d8523f8)**: `ObjMaterial` got a trailing `float alphaCutoff` (-1 = not MASK);
      `GltfLoader` sets it from `cgltf_alpha_mode_mask`/`alpha_cutoff`; the forward
      (`shader.frag`) and deferred (`geometry.frag`) shading passes `discard` when
      `alphaCutoff >= 0` and the sampled base-colour alpha is below it. Safe-by-default
      (every OBJ/OPAQUE material is -1, so bit-unchanged). CPU-tested
      (`GltfParseUnit.{MaskAlphaModeSetsTheCutoff,OpaqueMaterialHasNoCutoff}`, red
      without the loader change) + that suite added to the Windows CI filter (it was
      missing). **1b (shadow-pass MASK) DONE (6b7350b4)**: the directional cascade
      pass now alpha-tests MASK casters (VS forwards UV + moves light matrices to
      set 1 + repurposes the push slot as objectIndex; the empty FS replicates the
      forward material walk and discards; CSM binds the shared set at set 0). ABI
      skew (a CascadedShadowMap.ixx member) - verified with a fresh rebuild after
      the incremental build tripped the documented stale-BMI ASan overflow; 15/15
      goldens unchanged + validation-clean (the material fetch runs for every caster,
      proving the path end to end). ~~and a GPU-host visual golden of the cut-out~~
      **VISUAL GOLDEN DONE (MaskCardDiscardsCutoutTexelsVisually)**: differential
      oracle (base scene A vs +card B, changed-fraction in the bbox of changed pixels,
      scanned only in the GUI-free upper-right so the ImGui panel/FPS text/floor shadow
      are excluded); discard ON measured 0.37 (checkerboard, holes leave background),
      discard OFF 0.78 (red-proven by disabling the FS discard), 0.55 gate between.
      REMAINING for increment 1: only the RT/PT kernels (1c) still trace the solid quad.
      (Attempted the visual
      check via `KATAGLYPHIS_MODEL_OVERRIDE=Models/GltfTest/mask_card.gltf` +
      `DISABLED_DumpsFrameToPng`: the default camera frames the ImGui panel over a
      tiny/edge-on 1x1 card, so it shows nothing useful — the visual golden needs a
      controlled camera + a scaled card rig like `shadow_rig`, same lesson as the
      shadow golden. The shading logic is already CPU- + regression-verified, so this
      is confirmation-only.) The Rust renderer's MASK path (RPT `forward.wgsl`) is
      the reference.

      *ORACLE PARTLY DE-RISKED (2026-07-23): fixture ready + rig mechanism found, but
      the OBVIOUS metric is a trap.* Decoded `mask_card.gltf`'s embedded 8x8 PNG: a
      PERFECT 50/50 CHECKERBOARD (32/64 texels opaque, 32 alpha=0 cut-out,
      nearest-filtered). Rig mechanism: no new fixture needed - reuse
      `EngineHarness::renderer->addModel(path, placement)` (as the sponza golden does)
      to add the card over the default scene with a scale+translate placement that
      faces it to the camera and fills a known crop (the earlier tiny/edge-on failure
      was the DEFAULT placement; an explicit matrix fixes it), then LOOK at a
      DumpToPng before trusting the crop bounds.
      **METRIC TRAP (do NOT use detail/edge-fraction here):** with discard OFF the
      cut-out texels still carry RGB and render, so the card is a checkerboard in BOTH
      states - opaque-vs-background with discard ON, opaque-vs-cutoutRGB with discard
      OFF - and an edge/detail-fraction metric stays HIGH either way, so the red state
      PASSES (vacuous, exactly the "swung-pixel counts do not discriminate" lesson).
      **BEST ORACLE — DIFFERENTIAL, needs NO uniform background (2026-07-23, this is the
      one to build):** render the scene WITHOUT the card (capture A), `addModel` the
      card (capture B), and measure the fraction of CHANGED pixels *within the bounding
      box of the changed pixels*. Discard ON -> only the ~32 opaque texels change (holes
      leave the background = A, unchanged) -> ~50% inside the box. Discard OFF -> the
      solid card covers everything -> ~100% inside the box. The bounding box
      auto-locates the card, so this is FRAMING-INDEPENDENT (no exact crop to tune) and
      needs no uniform background (it compares B against the SAME background in A), which
      dissolves both the metric trap above and most of the framing risk. Assert e.g.
      `0.25 < changed_fraction < 0.80`; red proof (remove the forward FS `discard`,
      recompile spv) pushes it to ~1.0 and fails high. The only real setup is a
      placement that makes the card VISIBLE and not edge-on: the default camera is at
      `(0,6,26)` looking down -Z (Camera.cpp ctor), and the card's +Z normal faces it,
      so `translate({0,~5,~8}) * scale(~12)` in the card's own XY plane is a good first
      guess - LOOK at a DumpToPng once to confirm it fills a reasonable region and isn't
      back-face-culled (doubleSided is not honoured yet, increment 2, so the +Z side
      must face the camera). Estimate: one golden + ~1-2 look-and-tune cycles. 1c reuses
      this exact rig in PT/RT mode (its differential is even cleaner: background geometry
      visible through the holes vs occluded).

      *1b design note (M, so it starts warm — the shadow pass is a CORE system,
      give it a dedicated cycle):* the CSM shadow pass
      (`scene/light/directional_light/CascadedShadowMap.cpp`) is deliberately
      minimal — its own descriptor set holds ONLY the light-space matrices uniform
      (`recordCommands` binds `{descriptorSet}`, not the forward pass's material/
      texture set), the push constant is just `{model, cascadeIndex}`, and the VS
      (`directional_shadow_map.vert`) reads only `pos` (location 0). To alpha-test
      MASK casters: (1) VS — add `layout(location=N) in vec2 uv` (N = the UV slot in
      the engine `Vertex` layout) + pass it through; (2) push constant — add
      `material_address`, `material_index_address`, `texture_offset` (the forward
      pass already pushes these via ObjectDescription); (3) descriptor set — add the
      texture array + sampler bindings the forward set has (or bind the shared
      forward material/texture set as set 1); (4) the empty shadow FS
      (`directional_shadow_map.frag`) — sample base-colour alpha, `discard` when
      `alphaCutoff >= 0 && alpha < alphaCutoff`. Simplest first cut: ONE pipeline,
      discard for all (OPAQUE = -1 skips), accepting a wasted texture sample per
      opaque shadow fragment; the RPT-style SECOND alpha-testing pipeline with
      per-primitive routing (avoids that cost) is the optimisation after. Multiview
      is orthogonal (gl_ViewIndex stays in the VS). Golden: a MASK card caster over a
      plane — shadowed-pixel count drops vs a solid-quad control, red-proven by
      removing the discard (mirror the RPT alpha-shadow test's oracle). The
      `mask_card.gltf` fixture (a cut-out card, `GltfParseUnit.MaskCardFixture…`) is
      the caster.

      *Tractability found while scoping (makes it cheaper than the "M, core surgery"
      warning suggests, and NOT an ABI-skew change — `alphaCutoff` already exists):*
      (a) the shared material/texture set is a REUSABLE layout object
      `sharedRenderDescriptors.getLayout()` — the forward/deferred/clouds/skybox/RT
      pipelines all consume it — so the shadow pipeline layout just adds it as a
      second set; no new layout, and `recordCommands` ALREADY receives
      `sharedRenderDescriptors.sets()` (as `rasterizer_descriptor_sets`), it simply
      binds only its own today. (b) The shadow push constant's second field is
      `cascadeIndex`, explicitly "retained for layout stability; unused" (the VS uses
      `gl_ViewIndex`) — REPURPOSE it as `objectIndex` (same mat4+uint32 size, no
      push-constant resize) to reach `ObjectDescription[objectIndex].material_address`
      exactly as the forward FS does. So the real work is: shadow VS reads UV (Vertex
      loc 3) + passes it; the empty shadow FS #includes the forward binding defs +
      samples base colour + discards; set-index juggling (put the shared set at set 0
      to match the forward binding decorations, move light matrices to set 1 —
      currently set 0 binding 1); `recordCommands` binds the shared set + pushes the
      object index. Not ABI-skew, so a normal incremental build verifies it. The one
      genuinely delicate part is the set-index juggling — a wrong descriptor-set
      layout on this CORE pass means validation storms, so do it with the
      `ShadowsDarkenSomePixels` golden watching.

      *Turnkey numbers (investigation done — implementation is now mechanical):* the
      forward shared set is SET 0 with `OBJECT_DESCRIPTION_BINDING 2` /
      `TEXTURES_BINDING 3` / `SAMPLER_BINDING 4` (`hostDevice/host_device_shared_vars.hpp`);
      `objectIndex` in the forward path is just the mesh-loop index `m`
      (`Rasterizer.cpp:115`, `DeferredRasterizer.cpp:468`), and the CSM draw loop
      already iterates the same `m`. So: (VS) `directional_shadow_map.vert` — add
      `layout(location=3) in vec2 tex_coords;` + `layout(location=0) out vec2 fragUV`,
      change the light-matrices UBO to `set = 1, binding = UNIFORM_LIGHT_MATRICES_BINDING`,
      rename the push field cascadeIndex→objectIndex (same slot); (FS)
      `directional_shadow_map.frag` — `#include` host_device_shared_vars.hpp +
      ObjectDescription.hpp + ObjMaterial.hpp + Vertex.hpp + PushConstantRasterizer.hpp,
      declare the SET 0 bindings 2/3/4, do `obj_res = objDesc.i[objectIndex]` →
      `materials.m[materialIDs.i[gl_PrimitiveID]]` → sample `tex[textureID]` at fragUV
      → `if (alphaCutoff >= 0 && a < alphaCutoff) discard`; (C++) CSM pipeline layout
      becomes `{ sharedRenderDescriptors.getLayout(), lightMatricesLayout }`, plumb the
      shared layout through `CascadedShadowMap::init`, and in `recordCommands` bind the
      shared set (already received as `rasterizer_descriptor_sets`) at set 0 + push
      `objectIndex = m`. NOT ABI-skew, so a normal incremental build + the mask_card
      shadow golden verify it.

      *1c — RT/PT MASK any-hit (M/L, dedicated cycle; design done 2026-07-23 by reading
      the kernels).* Today both trace the SOLID quad for MASK: `path_tracing.comp` opens
      the ray query with `gl_RayFlagsOpaqueEXT` (line ~218) and an EMPTY
      `while(rayQueryProceedEXT)` loop, and the RT pipeline has no any-hit shader.

      **ATTEMPTED 2026-07-23 — PT shader half written + STRUCTURALLY verified, then
      REVERTED because the positive proof is BLOCKED by a separate limitation (see the
      new "added models absent from the RT/PT AS" item below).** The PT candidate
      alpha-test (a `candidatePasses(rayQuery)` helper: fetch the candidate material via
      the committed=false buffer-reference walk, OPAQUE/untextured pass immediately, MASK
      samples base-colour alpha at the barycentric UV vs the cutoff; the primary + NEE
      shadow queries drop `gl_RayFlagsOpaqueEXT` and confirm passing candidates) was
      IMPLEMENTED and is no-regression clean: all 4 PT goldens pass and the white FURNACE
      stays 186.005 (ideal 186) unbiased, proving the confirm logic is correct for opaque
      hits. The POSITIVE MASK-through-holes proof could not be built at the time because a
      mask_card `addModel`'d in PT was INVISIBLE even with the opaque flag forced (RED ==
      GREEN to the digit, 0.0353734) - which turned out to be the SEPARATE addModel-AS
      bug, NOW FIXED (see the ✅ item above). Reverted the 1c shader to stay at the
      "red/green proven" bar. **NOW UNBLOCKED**: with the addModel AS rebuild landed, an
      addModel'd card DOES trace in PT (`AddedModelAppearsInPathTracing` proves it). To
      finish 1c PT next: re-apply the `candidatePasses` shader change (from this session's
      transcript), addModel the mask_card in PT, and detail-fraction the card crop - with
      1c a checkerboard of opaque texels vs scene through the holes (high detail), without
      it a solid quad (low). ONE oracle caveat: mask_card's opaque RGB is uniform WHITE,
      so for a crisp signal use a HIGH-CONTRAST fixture (dark opaque texels) or a dark
      background - white-opaque vs grey-sky is weak. The shader change is written-and-correct.
      **2ND ATTEMPT (2026-07-23) hit a NEW blocker, also reverted:** re-applied the shader +
      built a high-contrast `mask_card_hc.gltf` (BLACK opaque texels + alpha checkerboard,
      valid 8x8 RGBA PNG). With the AS fix the added card now traces, BUT it renders SOLID
      WHITE, not a black checkerboard - i.e. `textureID == -1` (the color path falls back to
      diffuse/baseColorFactor white) AND `candidatePasses` returns true for everything
      (untextured MASK passes). So the ADDED MASK card's base-colour texture is not being
      sampled in PT even though the same-shaped `uv_transform_card` (opaque, added) DID
      sample its texture. Debug that first (why does an added MASK card get textureID -1 /
      wrong texture_offset in the traced path, when an added opaque card does not?) before
      the oracle. BISECT STEP 1 DONE (2026-07-23): the HC PNG is STRUCTURALLY VALID
      (decoded: colortype 6 RGBA, alpha checkerboard 32/64 opaque, RGB all-black) and the
      HC gltf is structurally identical to the golden-tested mask_card.gltf (only the
      material name + image bytes differ), so the FIXTURE is ruled out - this is very
      likely a real ENGINE bug in the added-textured-model path, NARROWER than the AS bug
      (only a second added TEXTURED model, seen in PT). Next bisect: a CPU parse test on
      the HC fixture to check textureID extraction. BISECT STEP 2 DONE (2026-07-23):
      `GltfParseUnit.HighContrastMaskCardExtractsItsTexture` PASSES - CPU extraction is
      clean (textureID >= 0, one image, cutoff 0.5). So the blocker is GPU-side: the
      texture extracts but is not sampled in the TRACED path for the added MASK card (it
      renders diffuse-fallback white). The opaque `uv_transform_card` added the same way
      DID sample its texture in PT, so the variable is MASK-material or RGBA-texture. NEXT
      bisect: render mask_card_hc in FORWARD/raster via the harness - black checkerboard
      there means upload is fine and the bug is PT-sample-specific; white there too means
      the GPU upload/`createFromMemory` of this RGBA texture failed. Then compare
      `uploadParsed`'s texture append / `texture_offset` for the added model against the PT
      `texture_id = texture_offset + textureID` read.
      CLEAN SUMMARY (2026-07-23, superseding my earlier flip-flopping notes above - the
      raster bisect was INCONCLUSIVE, default-camera framing on the 1x1 card; disregard the
      "raster renders black" and "likely candidatePasses" claims):
      - PROVEN: the CPU parse sets `textureID >= 0` for mask_card_hc
        (`GltfParseUnit.HighContrastMaskCardExtractsItsTexture` passes), and the added
        OPAQUE uv_transform_card samples its texture correctly in PT
        (`AddedModelAppearsInPathTracing`, committed path).
      - SYMPTOM: with the mask card added in PT the card renders SOLID WHITE (not
        transparent) = the diffuse fallback, i.e. the PT shader reads `textureID` as -1 for
        the added card even though the CPU material has it >= 0. A CPU<->GPU mismatch for the
        ADDED textured model in the traced path (upload or the traced material/texture-offset
        read), NOT a shader-logic bug I can pin by inspection.
      - NEEDS GPU DEBUG (RenderDoc / shader printf): dump the actual GPU ObjMaterial
        `textureID` + `texture_offset` for the added card as the PT kernel reads them, vs
        what the raster path reads. Extraction is clean; the mismatch is upload- or
        descriptor-side for a runtime-added textured model under RT/PT. Possibly related to
        the addModel path (which the AS fix just touched) not fully re-flattening the
        material/texture arrays the RT descriptors read.

      Two code paths, shared alpha-test logic:
      - *PT (ray_query, the easier half — NO new shader/SBT):* drop `gl_RayFlagsOpaqueEXT`
        from BOTH the primary query (~line 218) and the NEE shadow query (~line 263).
        In each `rayQueryProceedEXT` loop body, when
        `rayQueryGetIntersectionTypeEXT(rayQuery,false)==gl_RayQueryCandidateIntersectionTriangleEXT`,
        fetch the CANDIDATE's material+UV — same buffer-reference walk `getObjectHitInfo`
        already does but with the `committed=false` variants
        (`...InstanceCustomIndexEXT/GeometryIndexEXT/PrimitiveIndexEXT/BarycentricsEXT(rayQuery,false)`),
        interpolate `tex_coords` via barycentrics (copy lines ~96-98 of `raytrace.rchit`),
        sample base-colour alpha; call `rayQueryConfirmIntersectionEXT(rayQuery)` when
        `alphaCutoff < 0` (OPAQUE) OR `alpha >= alphaCutoff`, else skip (leave it a
        candidate). The primary query keeps the closest CONFIRMED hit; the shadow query
        (terminate-on-first-hit) then correctly passes through holes.
      - *RT pipeline (rgen/rchit — needs a new any-hit shader):* add `raytrace.rahit`
        bound to the hit group, replicating rchit's ObjectDescription/Vertices/Indices/
        MaterialIDs/Materials setup + the UV interpolation it ALREADY computes (rchit
        lines 69-98 are copy-ready), sample alpha, `ignoreIntersectionEXT()` on MASK
        fail (OPAQUE returns immediately = accept). Drop the per-geometry/instance opaque
        flag in `ASManager` (currently set so any-hit is skipped) and wire the rahit into
        the SBT/hit-group in the RT pipeline builder. rchit is unchanged (only committed
        hits reach it).
      - *Oracle (NOISE-ROBUST refinement 2026-07-23 — the rig now EXISTS):* the
        `addModel(mask_card, placement)` rig that `MaskCardDiscardsCutoutTexelsVisually`
        and the doubleSided goldens already use is directly reusable in PT mode
        (`renderer_vars.pathTracing = true`). Prefer a DETAIL-fraction over a differential
        here — PT is noisy and a base-vs-card differential is polluted by per-frame noise
        in the holes (separate accumulation after the AS-rebuild reset). Instead render the
        card over the SKY and measure the detail-fraction in the card's screen box after
        enough accumulation frames (the accumulation golden converges ~45): with 1c the
        card is a high-contrast checkerboard of white opaque texels vs sky through the
        holes → HIGH detail; without 1c the card is a SOLID uniform white quad (mask_card's
        RGB is uniform white — verified while building the KHR fixture) → LOW detail.
        Red-proven by restoring `gl_RayFlagsOpaqueEXT`. Same detail-fraction metric as the
        forward MASK + KHR goldens (`goldenRenderSuite.cpp`), so no new oracle machinery -
        just point it at the PT capture and pick the accumulation depth. The card-over-sky
        framing (env-tunable MASK_X/Y/Z/SCALE) is already dialled in from the forward golden.
      - ABI: shader-only for PT; the RT half adds a shader file + SBT wiring (no C++
        struct change, so not ABI-skew) but DOES touch `ASManager` geometry flags.
    - *Increment 2 — doubleSided — ✅ FORWARD DONE (2026-07-23):* forward pass now
      honours `material.doubleSided` via dynamic cull state exactly as the plan below
      described - `PipelineBuilder::setDynamicCullMode` (opt-in `VK_DYNAMIC_STATE_CULL_MODE`,
      only the forward Rasterizer enables it) + per-draw `setCullMode(eNone/eBack)`;
      the flag rides glTF material.double_sided -> MeshRange -> Mesh via add_new_mesh,
      exposed by `Scene::isMeshDoubleSided`. Verified: `MaskCardDoubleSidedRendersFromBehind`
      (card rotated 180deg, back to camera - visible 12779px ONLY with doubleSided,
      RED-proven 0px forced single-sided), 16/16 goldens unchanged, validation-clean.
      **DEFERRED ALSO DONE**: the G-buffer geometry pass got the same opt-in dynamic
      cull + per-draw setCullMode (`.cpp`-only, no ABI-skew);
      `MaskCardDoubleSidedRendersFromBehindDeferred` confirms the back-facing card
      reaches the G-buffer (12779px), 17/17 goldens, validation-clean. **SHADOW PASS
      NEEDS NOTHING**: the CSM pipeline already builds with `setCullMode(eNone)`
      (`CascadedShadowMap.cpp:644`), so it renders both faces and a doubleSided caster
      already casts correctly - no back-face culling to disable. So doubleSided is now
      COMPLETE across every pass (forward + deferred honour it, shadow never culled).
      The design below is retained as reference. *Original plan:* per-material cull-mode. *Concrete plan
      (2026-07-23): use DYNAMIC cull state, not a second pipeline.* `vkCmdSetCullMode`
      is core Vulkan 1.3 (the engine targets 1.4), so add `VK_DYNAMIC_STATE_CULL_MODE`
      to the raster PipelineBuilder and, per mesh in the draw loop,
      `vkCmdSetCullMode(cmd, mesh.double_sided ? eNone : eBack)` - no pipeline variant,
      no per-draw pipeline switch. Per-MESH cull works exactly post-#10 because each
      glTF mesh is now one primitive = one material (OBJ has no doubleSided concept, so
      always cull). Plumbing: add `bool doubleSided` to `GltfLoader::MeshRange` (parseCpu
      reads `cgltf primitive->material->double_sided`), carry it to a new `bool
      double_sided` on `Mesh` via an `add_new_mesh` param (ABI-skew: Model.ixx +
      GltfLoader.ixx -> fresh container), draw loop reads `getMesh(k)->double_sided`.
      Keeps doubleSided OUT of the GPU ObjMaterial struct. Oracle is DETERMINISTIC (raster,
      not the noisy PT of 1c): reuse the mask_card rig but rotate the card 180deg (back to
      camera) - INVISIBLE today (culled), VISIBLE once doubleSided is honoured; the
      differential-in-bbox metric from `MaskCardDiscardsCutoutTexelsVisually` transfers
      directly, red-proven by forcing single-sided. ~8 files, one fresh build.
    - *Increment 3 — BLEND + sorting (M/L):* sorted transparent pass through the
      blend pipeline above; this is the genuinely L part (back-to-front ordering,
      a second draw list). Defer until a real transparent asset needs it.
      *(Parity note found 2026-07-23:* the DEFERRED geometry pass discards any
      textured texel with `alpha < 0.1` for NON-MASK materials too
      (`deferred/geometry.frag:51`, `alpha_cull = alphaCutoff>=0 ? alphaCutoff : 0.1`),
      whereas the FORWARD pass only discards for MASK (`rasterizer/shader.frag:78`).
      So an OPAQUE material whose texture carries a real low-alpha channel would drop
      those texels in deferred but keep them in forward - a low-impact but real
      forward/deferred inconsistency, and a landmine to reconcile when BLEND lands
      since deferred cannot blend and would silently cull instead. Confirm intent
      before touching: the deferred 0.1 default may be a deliberate "kill fully
      transparent texels the G-buffer can't blend" guard.)*
    - *Increment 4 — KHR_texture_transform ✅ DONE (2026-07-23) + texcoord index (M/L, NOT S
      — corrected 2026-07-23):* **KHR_texture_transform DONE**: ObjMaterial gained trailing
      uv_scale/uv_offset (identity default, scalar-layout-safe), the shared
      `transform_uv` helper applies `uv*scale+offset` in the forward/deferred/shadow FS,
      and GltfLoader reads the extension's scale/offset from the base-colour texture
      (rotation not yet applied). Verified: `ReadsKhrTextureTransformScale` +
      `MaterialWithoutTextureTransformIsIdentity` CPU tests + `KhrTextureTransformTilesTheTexture`
      golden (8x8 RGB checkerboard scaled 4x tiles to 32x32, detail 0.552 vs 0.079
      red-proven), 18/18 goldens, validation-clean. uv scale/offset uniform and a per-slot
      uv-set selector — RPT does both via `uv_set_mask`. Cost split re-checked in the C++
      engine: *KHR_texture_transform* is the genuinely small half — a per-texture scale/offset
      (a material-struct field, ABI-skew) applied to the UV in the fragment shaders, no vertex change. *texcoord index*
      is NOT small: the engine `Vertex` (`scene/Vertex.hpp`, `getVertexInputAttributeDesc`
      returns 4 attributes) carries a SINGLE `texture_coords`, so a second UV set means
      adding `TEXCOORD_1` to the Vertex layout (5th attribute) + every mesh vertex buffer +
      every shader that reads UV + the loaders — a Vertex-layout ABI change touching the
      whole pipeline, M/L not S. Do KHR_texture_transform first; defer texcoord index until
      a real dual-UV asset needs it.
**✅ FIXED (2026-07-23): a model added via `addModel` after the initial load was ABSENT
from the RT/PT acceleration structure** — CONFIRMED by code (addModel rebuilt the
object-description buffer + RT descriptor sets but never the AS) and FIXED (addModel now
calls `createASForScene` when hardware RT is supported). Red/green-proven by
`GoldenRender.AddedModelAppearsInPathTracing` (added card visible in PT: detail 0.108 with
the rebuild vs 0.035 without). *Follow-up optimisation (not a bug, correct as-is):* the fix
(and the existing `reloadModel` path) call `createASForScene`, a FULL rebuild that clears and
re-builds EVERY model's BLAS - O(N) BLAS builds to add ONE model. Fine for the few-model
scenes today, a hitch for many-model or interactive add. Incremental version: an ASManager
`appendBlas(model)` that builds only the new model's BLAS and pushes it, then `createTLAS`
(the TLAS rebuild is unavoidable and cheap, it just re-lists instances). Guard the RT
goldens + `AddedModelAppearsInPathTracing`. NOTE (assessed 2026-07-23): `createBLAS` is a
BATCHED full build - one shared scratch buffer sized across ALL models, compaction across
all, and it clears+rebuilds the whole `blas` vector - so `appendBlas` means carefully
extracting the single-model build path (its own scratch + compaction, append not clear);
it is a real refactor of the AS build, not a 10-line change. Budget for it + verify RT
carefully (AS bugs are the delicate kind). Original report: a `mask_card.gltf` added with `renderer->addModel(path, placement)`
    while in PATH-TRACING mode does not appear in the traced image at all: forcing
    `gl_RayFlagsOpaqueEXT` (a solid quad that MUST occlude if present) leaves the render
    bit-identical (RED == GREEN, detail 0.0353734), i.e. no rays hit it. The same
    `addModel` renders fine in the FORWARD raster goldens (`SecondModelShadesWithItsOwnTextures`),
    so the geometry loads - it just isn't traced. Likely the same class as the
    TLAS-follows-transforms fix (`99c62471`) which was TLAS-only "BLAS untouched": adding
    a NEW model needs a new BLAS built AND the TLAS rebuilt to reference it, and the
    add-model path may only update object descriptions / the TLAS instance list without
    the new BLAS. Confirm by tracing a scene where a second model is added post-load (RT
    and PT) and checking it appears; if real, the fix is to build the added model's BLAS
    and rebuild the TLAS in `ASManager` on add. This BLOCKS positively testing 1c PT via
    addModel (use a default-model MASK fixture meanwhile). If confirmed it is a real
    correctness bug for any runtime-added geometry under RT/PT, not just MASK.
12. **Point lights are wired on the GPU but never fed; `OmniDirShadowMap` renders
    nothing** (M) — **DONE (Option A — DELETE, 2026-07-24)**: removed
    OmniDirShadowMap (.ixx + .cpp, 176 lines), `Light.ixx` (entire file —
    Light/DirectionalLight/PointLight all unreferenced), the
    `omni_shadow_map.*` shader trio+spv (6 files), `point_light.glsl`
    (never included), and the pointLight fields from SceneUBO +
    `host_device_shared.hpp` + `bindings.hpp` + `lighting.frag` (deferred
    loop). Frees the 1024x1024 cube depth allocation per run, shrinks
    SceneUBO, kills the parity trap. Re-adding later costs the same M as
    finishing now — nothing rots.
13. **Cascades cost 3 render passes + a pass-through geometry shader** —
    **DONE (2026-07-22)**: single multiview pass (viewMask over all cascades,
    one full-array framebuffer), gl_ViewIndex selects the light matrix in the
    vertex shader, geometry stage deleted (file + spv). Union caster culling
    preserves the old test's safety property. Measured: shadow coverage
    identical (12.276% vs 12.267% darkened), ShadowCascades 0.0477 ->
    0.0408 ms (-14%) on the rig run. 94/94, validation-clean.
14. **cgltf is an unfuzzed untrusted-input surface** — **DONE (2026-07-22)**:
    all three hardened - buffer-view fit checked against buffer->size, base64
    length rejected unless a positive multiple of 4 (the underflow source),
    cgltf_validate() gates the walk. New gltf_parsing_fuzz_test (self-
    contained cgltf, wired into both CI lanes) plus two CPU regression tests
    (malformed JSON rejected; sub-quad base64 URI yields no texture instead of
    an underflowed read). 96/96, validation-clean.
15. **Swapchain recreate destroys before creating; surface-lost unhandled** —
    **DONE (2026-07-22, the two live defects)**: recreate now keeps the old
    swapchain alive as the oldSwapchain handoff (destroyImageViews split out
    of cleanUp; old handle destroyed AFTER the new one is created), and
    createSwapchainKHR's result is checked (ASSERT_VULKAN - it silently stored
    null before). Surface-lost was already distinct in the current code: acquire/
    present route eErrorOutOfDate -> recreate and everything else (incl.
    eErrorSurfaceLost) -> abort_frame_with_fatal_error, so no change needed
    there. The resize path is not headless-testable; the always-run half (init
    + the result check) is validation-clean every launch.
16. **Dynamic rendering + synchronization2 are hard-disabled** (L) —
    `VulkanDevice.cpp:451`, `:454`. Already in use: RT pipelines, ray query, AS,
    BDA, descriptor indexing, scalar block layout, multiview, pipeline cache, VMA,
    timestamps. Dynamic rendering would delete the framebuffer rebuild in
    `recreateSwapChain` (`VulkanRenderer.cpp:592-621`) where the image-count edge
    cases live. Test: run the whole golden suite under both paths behind a toggle.

### Path tracing survey (2026-07-22)

Full read of PathTracing.cpp/.ixx, all 241 lines of path_tracing.comp, the
dispatch path and the RT pipeline. The kernel is an RTIOW/nvpro-style port.
Two load-bearing facts drive most items: the RNG seed is `res.x*y + x` with NO
frame dimension (`path_tracing.comp:150`) - every frame is bit-identical, so
nothing ever converges; and the environment-radiance line is COMMENTED OUT
(`:225`) with clearColor black (`PathTracing.cpp:92`) - the scene is lit by an
accidental constant-white furnace and the GUI light provably does nothing.

1. **Use the precomputed inverse matrices** (S, top value/effort) - the sample
   loop calls `inverse(view)`/`inverse(projection)` 24x per pixel per frame
   (`:162,:168,:173`) while GlobalUBO ALREADY carries inv_view/inv_projection
   (`GlobalUBO.hpp:26-29` - added for the clouds pass with a comment calling
   inverse() "ruinously expensive"). raytrace.rgen:41-44 has the same waste.
   Verify: GpuTimedPass::Main JSON before/after.
2. **Temporal accumulation + camera-move reset + per-frame RNG** (M, headline)
   - **DONE (2026-07-22)**: rgba32f history image (one, persistent), running
   mean in the kernel, frame index folded into the seed, resets on camera
   move / resize / AS rebuild (model load). Golden proves differ+converge
   with an exact red (frame term removed -> changed fraction exactly 0).

   NEW items found while proving it:
   - ~~**PT dispatches before the TLAS exists** (S)~~ **DONE**: the RT/PT record
     branch is now guarded on `asManager.getTLAS()` (plus the descriptor-set index
     bound) in `VulkanRenderer.cpp:872-873`, so the async model-load window no
     longer dispatches against never-written TLAS/output/accumulation descriptors.
     Original text: with PT enabled during the async model load, the kernel
     dispatches against never-written descriptor sets (20 validation errors in the
     pre-load window of the golden run). Guard the PT/RT record branch on a built TLAS.
   - ~~**Pipelines consume the PREVIOUS run's SPIR-V** (S/M)~~ **DONE
     (2026-07-22)**: PathTracing/PostStage reordered to compile-then-read
     (the other stages already had the right order; Clouds deliberately
     consumes prebuilt spv only). BUT the reorder exposed a deeper layer,
     still OPEN:
   - ~~**Runtime shader compilation is a silent no-op for container-built
     binaries** (M)~~ **DONE (2026-07-22)**: glslc resolves at call time
     (baked path when it exists -> VULKAN_SDK/Bin -> PATH), and the system()
     return is checked with a loud error naming the stale spv that will be
     served. Proven on the host: touching a kernel source and running a
     golden regenerates the spv mid-run (mtime flip) - GLSL iteration no
     longer needs manual compile-shaders.ps1 (the script remains the bulk /
     CI path; ShaderIncludes already had its own runtime fallback).
3. **Wire actual light transport** (M) - **DONE (2026-07-22)**: NEE toward
   the directional light (one shadow ray per bounce) + deliberate soft
   gradient sky on miss (the accidental radiance-1 furnace is gone). Golden
   `PathTracingRespondsToTheDirectionalLight` (shadow rig scene, swung-pixel
   fraction in the panel-free crop): green 0.027, pre-NEE kernel exactly 0.
   Estimator constants (1/pi, PDFs) still item 9.
4. **Degenerate scatter guard** (S) - **DONE (2026-07-22)** near-zero scatter
   falls back to the normal, RTIOW 9.4 style.
5. **Hit normal transformed with w=1, no inverse-transpose** (S) - **DONE
   (2026-07-22)** row-multiply by `worldToObject` (inverse-transpose, handles
   non-uniform scale); same fix applied to `raytrace.rchit` which shared the
   defect verbatim, plus its object-space `N`/`V` BRDF inputs.
6. **Material diffuse fallback commented out** (S) - **DONE (2026-07-22)**
   `textureID < 0` now uses `material.diffuse` in BOTH kernels (the old
   clamp sent -1 to texture slot 0, not black as first written here).
7. **Russian roulette + GUI spp/depth** (S/M) - **DONE (2026-07-22)**: GUI
   sliders (spp 1-64, bounces 1-16) through new push-constant fields; RR from
   the 4th segment (survivors reweighted, unbiased); a quality change resets
   the accumulation (mean over two estimators is biased). Golden: bounces
   8-vs-1 swung fraction 0.132 green, hardcoded-bounds kernel 7.3e-5 (fails).
8. **Self-intersection epsilon** (S) - **DONE (2026-07-22)** t_min raised
   0.0 -> 0.001 to match the rgen; the 1e-4 normal offset stays as the
   secondary guard.
9. **Estimator bias** - **DONE (2026-07-22)**: the NEE term now carries the
   Lambertian 1/pi (measured: rig lit crop 208.5 -> 188.2; forward on the
   same rig is 158.1, remaining gap = PT's indirect sky which forward
   lacks). The BOUNCE path needed nothing: cosine-weighted sampling of a
   Lambertian cancels pi and cosine exactly - the "no PDF division" reading
   was wrong for that half. The furnace golden is DONE (2026-07-22 later
   the same day): KATAGLYPHIS_PT_FURNACE uniform-env + albedo-1 mode;
   green mean 186.005 vs ideal 186 at uniformity 1.0; red (spurious
   bounce 1/pi) crashes to 136.9/0.25.
10. **PT goldens** - **DONE by accumulation of shipped units (2026-07-22)**:
    non-black + variance-decreases live in PathTracingAccumulatesAndConverges,
    light response in PathTracingRespondsToTheDirectionalLight, quality
    wiring in PathTracingHonorsTheQualityControls, transform-follow in
    RaytracedWorldFollowsTheModelTransform, and the furnace in
    PathTracingPassesTheWhiteFurnaceTest - five red-proven goldens where the
    survey found only device-not-lost.
11. **Decorrelate the RNG seed** - **CLOSED as a measured NULL RESULT
    (2026-07-22)**: lag-1 autocorrelation of the depth-2 noise field is
    -0.012 (lag-16: +0.015) - no neighbour correlation exists; the LCG
    pre-step + PCG output hash already decorrelate adjacent linear seeds.
    The survey's claim was an assumption. The instrument stays as a logged
    diagnostic in the accumulation golden.

Trivial rider: ~~path_tracing.comp includes the BRDF headers (`:15-19`) and
never calls them - delete the dead includes.~~ **DONE (2026-07-22).**

### C++ Vulkan engine — second survey (2026-07-22, app/GUI/RT/deferred internals)

**CORRECTION (2026-07-22, after implementation):** second-survey items 1-3 cited
`rasterizer/g_buffer_{geometry,lighting}_pass.frag` - those files are DEAD: the
DeferredRasterizer loads `Resources/Shaders/deferred/{geometry,lighting}.*`
(cwd + RELATIVE_RESOURCE_PATH, DeferredRasterizer.cpp:315-323), and the live
pair has none of the three defects (no tonemapping in lighting - raw linear
out; bindless texture sampling in geometry; subpass-input albedo/material).
`clouds/CloudsRectangle.frag` is likewise referenced by nothing. The real
defects found instead while proving this: the GUI mode radios stomped
programmatic mode changes every frame (item #11, FIXED), and the post-pass
input descriptor was written once at init so a mode switch presented a stale
forward image (NEW, FIXED with a rebind on mode change). Items #2/#8's
G-buffer-format concerns apply to the LIVE pass's attachments only where they
actually exist there.

**DONE 2026-07-22 - dead shader set deleted** (20 files: g_buffer_* pair+spv, CloudsRectangle, noise_texture_{32,128}_res, loading_screen/ entire). `omni_shadow_map.*` were also deleted 2026-07-24 with the point-light system (item #12). `generated/` is NOT dead - those are the Rust renderer WGSL->GLSL exports. Original item text: `rasterizer/g_buffer_*`,
`clouds/CloudsRectangle.frag` (+ audit for further unreferenced shaders by
grepping each Resources/Shaders file against Src). They cost this survey its
three headline findings and several verification cycles; BuildIntegrity also
recompiles them forever. Deleting is safe only after a liveness grep per file -
the loader resolves paths at runtime, so a filename appearing in NO source
file is the deletion criterion.


A second deep pass over the subsystems the first survey covered least. Verified
against source; nothing duplicates the first list. Ruled out on inspection (so
nobody re-chases them): the rgen "missing Y-flip" comment is stale, not a bug
(the flip is baked into the projection at VulkanRenderer.cpp:179); the async
model loader is race-clean; GUI/renderer state is single-threaded.

**The deferred path is broken three independent ways** - do these together:

1. **Deferred lighting tonemaps + gamma-corrects, then post.frag does BOTH again**
   — **PHANTOM (see the CORRECTION above): `g_buffer_lighting_pass.frag` is a
   DEAD file referenced by no code; the LIVE `deferred/lighting.frag` writes
   raw linear and is healthy. No fix needed.** Original text:
   (S) - `g_buffer_lighting_pass.frag:215-216` ends with Reinhard + gamma, then
   `post.frag:32-34` re-applies both. Forward writes raw color and is correct,
   so deferred renders crushed/washed vs forward. Fix: delete the two lines.
   Test: tighten `GoldenRender.DeferredMatchesForwardRoughly` to mean-luminance
   tolerance; fails today.
2. **G-buffer material-id is UNORM, every index collapses to 0/1** — **PHANTOM
   (dead `g_buffer_geometry_pass.frag`; the live geometry pass has no material-
   id attachment). No fix needed.** Original text: (S) - the
   geometry pass writes `g_material_id = vec3(mat_ID)` into `eR8G8B8A8Unorm`
   (`DeferredRasterizer.cpp:92,:209`), so `mat_ID >= 1` clamps to 1.0 and
   `SKYBOX_MATERIAL_ID = 35` / `CLOUDS_MATERIAL_ID = 36` can never round-trip -
   sky and cloud pixels get lit as geometry in deferred mode. Fix: `eR8Uint`/
   `usampler2D` (or normalize by MAX_MATERIALS+2).
3. **G-buffer never samples albedo textures** — **PHANTOM (dead
   `g_buffer_geometry_pass.frag`; the live geometry pass samples the base
   colour - see the materials unit 2840cc9a). No fix needed.** Original text:
   (S) - the texture fetch is
   commented out on the assignment line in `g_buffer_geometry_pass.frag`
   (`g_albedo = materials[mat_ID].diffuse;//texture(...)`), so Sponza renders
   flat per-material color in deferred while forward shows textures.

**CPU-testable robustness/coverage (the now-green Windows CI can gate these):**

4. **First-frame delta_time is unbounded** — **DONE (c8d3e26e)**: `FrameInput.ixx`
   now has a pure `clamp_frame_delta` (caps at 0.1 s / 10 FPS, negatives → 0) that
   `update_frame_timing` applies, so the first frame's whole-startup-wallclock
   delta — and any later hitch — becomes slow motion instead of a camera teleport.
   Guarded by `FrameInputUnit.{FirstFrameStartupSpikeIsClamped,OrdinaryFrameDeltasPassThroughUnchanged,NegativeDeltasBecomeZero}`
   in `frontendInputSuite.cpp`, wired into the Windows CI filter (`Windows.yml:211`).
   Original text: — `last_time` starts at 0.0 (`App.cpp:32-33`) so the first
   `update_frame_timing` returns the whole startup wall-clock (seconds); a key held
   during load lurches the camera. Seed or clamp; pure gtest.
5. **Single-time command buffers are never freed** — **DONE**:
   `endAndSubmitCommandBuffer` now calls `device.freeCommandBuffers` after the
   submission is fence-waited (or `queue.waitIdle()` in the fallback paths), so the
   buffer is provably not pending when freed (`CommandBufferManager.cpp:124`), plus
   a matching free on the submit-failure error path (`:96`). This stops every
   texture/buffer upload, layout transition and AS build from leaking a command
   buffer for the process lifetime (GUI model reloads leaked dozens more). The
   per-submit fence create/destroy is a separate, smaller inefficiency left as-is.
6. **Input handling + frame timing have ZERO tests** — **DONE (c8d3e26e)**:
   `frontendInputSuite.cpp` adds 8 device-free gtests — `FrameInputUnit.*` (the #4
   clamp) and `WindowInputUnit.*` (key press/release tracking, out-of-range keys
   ignored, first-mouse-move does not jump the camera, axis-delta consume+reset,
   and the ImGui-capture gate swallowing input). All eight run in Windows CI
   (`Windows.yml:211-212`). Original text: — `WindowInputCallbacks.ixx:24-83` and
   `FrameInput.ixx:9-21` are pure, device-free, and route all input into the camera;
   no suite in Test/commit references them. This is also where #4 gets its
   regression guard.

**Build hygiene / perf / RT:**

7. **Kompute sandbox target with exceptions enabled** — **DONE (2026-07-22,
   the gate option)**: KATAGLYPHIS_BUILD_KOMPUTE_PLAYGROUND (default OFF)
   wraps both the playground target AND kompute's configure/subdirectory -
   default builds ship no exceptions-enabled binary and skip the kompute
   dependency entirely (its headers throw, so it can never link the
   project's no-exceptions options; conforming it was measured impossible,
   gating is the honest park). Verified: zero kompute lines in the gated
   configure; 93/93 unchanged. The ON path re-enables exactly the previous
   add_subdirectory wiring.
8. **G-buffer stores full world position + an RGBA8 for a scalar id** -
   **DONE (2026-07-22, position half)**: the rgba16f world-position target is
   gone; the lighting subpass reconstructs position from the DEPTH input
   attachment it already bound (inv_view * inv_projection * (uv*2-1, depth),
   background = depth >= 1.0). Correctness: deferred-vs-forward parity 0.200
   (was 0.205 - the reconstruction agrees with the stored positions). Timing
   on the parity run: Main 0.0647 -> 0.0639 ms - the test scene is too small
   for bandwidth wins to register; the win is one full-res 8-byte/px
   write+read removed per frame. The material-id RGBA8 packing remains open
   (S) if a real scene ever measures it.
9. **Acceleration structures are never compacted** - **DONE (88a3d4fa)**:
   BLAS built with eAllowCompaction, compacted-size query + copy after the
   synchronous build, originals destroyed; TLAS built after picks up the new
   addresses. Default scene: 15.98 MB post-compaction. (TLAS-only compaction
   deliberately skipped - it gains little.)
10. **RT output image is `rgba8`** - **DONE (2026-07-22, with the HDR unit
    e25eca80)**: raytrace.rgen + path_tracing.comp storage images are rgba16f
    and the offscreen targets R16G16B16A16Sfloat, so the traced result reaches
    post's Reinhard un-clamped (the RT analogue of the UNORM-lit-target fix).
11. **GUI render-mode radios live in function-local statics** - **DONE
    (961a9a7a)**: the radios derive from the shared vars each frame instead of
    holding their own static int, so a programmatic mode change is no longer
    stomped (the defect that made the deferred path unreachable end-to-end).

### Rust WebGPU renderer

**Bake wasm32-unknown-unknown into the :latest-cross image.** ~~The CI lane added
2026-07-22 cannot add the target itself~~ **FIX UPSTREAM (2026-07-22,
ContainerHub `3cff632`)**: install-rust.sh now adds the wasm target on the
STABLE toolchain (it only had it on the pinned nightly), not behind try_ so a
regression fails the image build. REMAINING: once the rebuilt :latest-cross
publishes, flip the RPT wasm step from skip-if-missing back into a hard gate.

**Add a wasm32 CI lane (found the hard way 2026-07-22).** `wasm_demo.rs` is
entirely behind `#[cfg(target_arch = "wasm32")]`, and nothing in the loop builds
that target - cargo test, clippy and every CI lane are native - so it had not
compiled since the wgpu 29 migration (it still used `wgpu::SurfaceError`, which
no longer exists). The web demo was simply broken and no check could see it.
A `cargo check --target wasm32-unknown-unknown` is seconds of compute and would
have caught it at the migration commit.

The recurring bug class behind many of these is written up in
`docs/renderer-bounds-invariant.md` - read it before touching anything that
moves geometry.

**Status 2026-07-31 (supersedes the 2026-07-24 status) — re-verified against the
crate source:**
  - #4 texture dedup: DONE (a `texture_cache` HashMap keyed by `Arc::as_ptr`
    dedupes at upload, forward.rs:1053-1129).
  - #12 uniform split: **the split itself is DONE** — `FrameUniforms`
    (forward.rs:55-64, written once per frame at :1780-1799) vs `PrimUniforms`
    (:68-78), punctual lights in a storage buffer (:1801-1805). STILL OPEN: the
    per-primitive `write_buffer` of the full `PrimUniforms` every frame with no
    dirty gate (:1844-1867) and the per-frame `model.inverse().transpose()`
    recompute (`normal_matrix_of` at :3080-3087) — see the 2026-07-31 batch task.
  - #13 render bundles: **DONE in shape** — the caster list records into a
    `wgpu::RenderBundle` (:1891-1944) executed per cascade (:1968-1970). STILL
    OPEN: the bundle is rebuilt every frame (a local, never cached) and
    per-cascade caster culling was disabled to make the bundle possible
    (`_light_frustum` discarded at :1952, stats fudged at :1939/:1972-1976) —
    see the 2026-07-31 batch task.
  - #15 lower-value trio: strip/fan triangulation DONE
    (gltf_loader.rs:206-227, :387-397), 16-bit PNG down-conversion DONE
    (:262-268). STILL OPEN: orthographic cameras are warn-and-dropped
    (:155-160) — see the 2026-07-31 batch task.
  - #8 MSAA remains open and design-heavy (the depth-resolve blocker above).


1. **Occlusion culling deletes, then flickers, any primitive the camera is inside** (S) —
   when the eye is inside a primitive's AABB the proxy box's front faces are
   near-plane clipped and only back faces rasterise, which fail `LessEqual`
   (`occlusion.rs:171-177`, `occlusion_bbox.wgsl:29-69`), so the query returns 0 →
   skipped next frame → depth empty → passes → returns. A ~30 Hz strobe on the
   object filling the screen. `cull_mode: None` and the 2% margin do not address
   near-plane clipping. Fix: force-visible when the expanded AABB contains the eye.
2. **`set_instances` never widens `scene_bounds`, so cascades stay fitted to the
   un-instanced scene** (S) — the 8th case of the stale-bookkeeping pattern.
   `set_instances` updates `aabb_min/max` and `world_center` but `scene_bounds` is
   written only in `upload_scene` (`forward.rs:923`) and `set_animation_time`
   (`:1913`), and it is the ONLY input to cascade fitting (`:1920-1924`). Instances
   scattered over ±50 units fall outside every cascade, so they neither receive nor
   cast shadows.
3. **`world_center` silently changes metric on first animation** (S) — at upload it
   is the vertex centroid (`forward.rs:1183`), afterwards the AABB centre (`:696`,
   `:725`, `:1906`). So `set_animation_time(0.0)` with NO movement can flip the LOD
   level across a switch distance and reorder the transparent draw list. Consumers
   at `:145` (LOD) and `:1489-1491` (blend sort) are documented as needing one
   agreed metric — they agree, but the value changes definition underneath them.
4. **Every primitive uploads its own copy of every material texture** (M) —
   `create_material_texture` is called inside the per-primitive loop
   (`forward.rs:997-1013`), so 200 primitives sharing one atlas do 200 CPU mip
   chains (`generate_mips` does a per-texel `powf`, `:2620-2657`) and 200 GPU
   uploads. The CPU side is already `Arc<CpuTexture>`, so the cache key is free.
   This is the VRAM ceiling blocking the Colosseum scene.
5. **Instanced normals use the instance matrix, not its inverse-transpose** (S/M) —
   `forward.wgsl:136-140` applies the raw instance matrix on top of a normal matrix
   built from `prim.model` alone (`forward.rs:1296`). Wrong for any non-uniform or
   mirrored instance scale — i.e. exactly the scattered/squashed instances that
   instancing exists for. The tangent path is correct, which hides the asymmetry.
6. **`COLOR_0` vertex colours are silently dropped** — **DONE (2026-07-22, RPT
   abb46c1, push held for the Windows lane)**: Vertex gains a linear-RGBA
   colour (white default), the loader reads COLOR_0, the shader multiplies it
   into albedo per spec, QEM blend interpolates it. Attribute location 6 for
   colour shifted the instance buffer to 7-10. Green-vertex-quad red/green
   test (r=g=b=177 white in the red state).
7. **Every texture slot is forced onto TEXCOORD_0** — **DONE (2026-07-22, RPT
   0f715e1, push held)**: Vertex carries uv1 (TEXCOORD_1); the loader builds a
   per-material uv_set_mask (bit per slot) from each textureInfo.texCoord;
   packed into material_flags.y, the shader selects uv0/uv1 per slot;
   KHR_texture_transform applies to the base slot's chosen set; QEM blend
   interpolates uv1. texCoord >= 2 falls back to UV0 with a warning. Base-on-
   UV1 red/green test (flat red at mask 0, red/blue split at mask 1). Rider
   (KHR_texture_transform on non-base slots) still open.
8. **No anti-aliasing anywhere** (M — **bigger than it looks; DESIGN NOTE
   2026-07-22**) — `MultisampleState::default()` on all four forward pipelines
   and `sample_count: 1` on the HDR target. The most visible quality defect in
   the browser demo. Color MSAA + resolve to the HDR view is easy; the BLOCKER
   is DEPTH. A render pass requires its color and depth attachments to share
   sample_count, so MSAA colour forces a MSAA depth buffer - but `self.depth`
   is read as a plain single-sample texture by BOTH SSAO (position
   reconstruction) AND the occlusion-cull bbox pass, and **wgpu has no native
   depth resolve** (only color `resolve_target`). So the real work is one of:
   (a) a manual depth-resolve pass (MSAA depth -> single-sample depth via a
   fullscreen min/max blit) feeding SSAO + occlusion, or (b) make SSAO and the
   occlusion pass MSAA-depth-aware (per-sample loads). Not a mechanical
   sample_count bump - budget a dedicated session. A clean test exists: a
   diagonal edge produces intermediate-colour "partial" pixels under MSAA and
   only hard fg/bg pixels without.
9. **Degenerate/NaN input poisons the whole frame** (S/M) — **DONE (2026-07-24)**:
    `compute_world_transforms` now guards non-finite node scale/translation/rotation
    and the resulting matrix (falling back to identity), catching zero-scale
    hide-nodes and NaN-export bugs before they propagate through the scene graph.
    `update_cascades` guards scene_radius against NaN (belt-and-suspenders behind
    the vertex-level is_finite skips in `primitive_local_aabb`/`primitive_world_aabb`).
    Three new CPU tests: `a_zero_scale_node_produces_a_finite_identity_transform`,
    `a_nan_translation_produces_a_finite_identity_transform`,
    `a_non_finite_scale_produces_a_finite_transform`. (Items #2, #3, #5 were already
    fixed by the 2026-07-22 batch; the compute_world_transforms guard was the last
    remaining NaN vector.)
10. **`KHR_materials_unlit` is ignored** (S) — the `gltf` crate exposes
    `material.unlit()`; the loader never asks (`gltf_loader.rs:435-496`). Every
    Sketchfab flat-colour export and most mobile/AR assets get a full GGX response
    with IBL and shadows — exactly what the extension exists to prevent.
11. **Anisotropic filtering is never requested** (S) — `create_sampler` leaves
    `anisotropy_clamp` at 1 (`forward.rs:2589-2597`). The mip chain is already
    correct, so this is the cheapest visible win: grazing-angle floors/walls are
    over-blurred by several mip levels. Must fall back to 1 when the glTF sampler
    asked for `Nearest` (wgpu validates this).
12. **~784 bytes of identical uniform data + a bind group rewritten per primitive
    per frame** (M) — `Uniforms` mixes per-frame data (view_proj, 3 cascade
    matrices, 16 vec4 of lights ~700 B) with per-primitive data, and the whole
    struct is rewritten for every primitive every frame (`forward.rs:1292-1323`);
    at 1000 primitives that is ~780 KB/frame plus 1000 buffers and bind groups.
    Same loop recomputes `model.inverse().transpose()` per frame though `model`
    only changes in `set_animation_time`; and `VsOut.light_space_pos` is
    interpolated but never read (`forward.wgsl:116`, `:146` vs `:364-473`).
13. **Render bundles for the three shadow cascades** (M) — the identical draw list
    is re-recorded three times per frame (`forward.rs:1349-1397`). `RenderBundle` is
    WebGPU-core (works on the web, unlike the parked indirect-draw item). Per-cascade
    culling changes the set, so invalidation is the real design question.
14. **Bloom and SSAO run at full cost when their strength is 0** (S) — `encode` is
    unconditional (`forward.rs:1539-1542`); the strengths are only consulted by the
    tonemap composite, so a slider at 0 still pays for a half-res depth pass, a 3x3
    blur, a brightpass and a separable Gaussian.
15. Lower value, noted: orthographic glTF cameras are dropped
    (`gltf_loader.rs:139`); `TriangleStrip`/`TriangleFan` primitives are skipped
    entirely (`:290-297`); one 16-bit PNG aborts the whole file instead of
    down-converting (`:197`).


## 2026-07-24 batch — sized, unblocked

### C++ Vulkan engine

### Rust WebGPU renderer

### Cross-renderer

### Container / build


## 2026-07-28 batch — refactor (dead code, duplication, test gap)

Found by a full read of the largest `Src/GraphicsEngineVulkan/` files against the
current source (nothing here duplicates the surveys above; all verified against
the code at the time of writing). Ordered non-ABI-skew first so the executor can
drain the cheap wins on an incremental build before the ABI-skew one needs a
`-FreshContainer`.

## 2026-07-28 batch II — subsystem extraction & CI coverage

Found by a structural read of `VulkanRenderer.cpp` (still the 78 KB / 1771-line
hub) and the test/CI wiring. None duplicate the dead-code batch above or the
sized items in the 2026-07-24 batch. Ordered non-ABI-skew first.

## 2026-07-28 batch IV — dead code, dead params, naming, test gap

Found by reading `SkyBox.cpp`, `Scene.ixx`/`Scene.cpp`, `Clouds.cpp`/`.ixx`,
and `ASManager.cpp`/`.ixx` against the current source. None duplicate the
batches above; all verified against the code at the time of writing (every
"dead" claim is backed by a repo-wide grep returning only the declaration
and/or definition). Ordered non-ABI-skew first so the executor drains the
cheap wins on an incremental build before the three ABI-skew tasks share one
`-FreshContainer`.

## 2026-07-30 batch — planner (validation conformance, dead plumbing, Slang follow-ups)

All five verified against the tree on 2026-07-30 (post-Slang-migration state,
`86ee9532`). The Rust deep-dive candidates were re-checked first: occlusion
eye-inside guard, bloom/SSAO zero-strength skip, MSAA, `KHR_materials_unlit`
and anisotropic filtering are ALL already implemented in the crate — do not
pick them up from the older prose above.

### C++ Vulkan engine

### Build / scripts

### Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate`)

### Cross-renderer / docs

## 2026-07-30 batch II — planner (Slang-migration test fallout, push-constant budget, GPU-verification blocker)

Originally three tasks, verified against the tree on 2026-07-30, from the
verification notes of the "`GUI*` mutable cross-cutting dependency" task
above. The BuildIntegrity rewrite and the push-constant budget shrink
(`PushConstantRasterizer` now 116 bytes, `PushConstantRasterizerUnit.*` all
green) are both done; the GPU-verification blocker below is what remains.

### GPU host verification

**ROOT CAUSE FOUND AND FIXED (2026-07-31).** The GPU-golden host run is
restored. Two independent in-repo bugs were causing the reported abort, not
an environmental/driver regression:

1. `VulkanDevice.cpp:465-466` set `features2.pNext = nullptr;` instead of
   `&features11`, silently severing the entire Vulkan11/12/13 + ray-tracing
   feature chain (`features11 -> features12 -> features13 ->
   acceleration_structure_features -> ray_tracing_pipeline_features`) from
   ever reaching `vkCreateDevice` (`device_create_info.pNext = &features2` at
   `:619`). Every feature the code carefully queried and conditionally
   enabled on those structs - `multiview`, `bufferDeviceAddress`,
   `scalarBlockLayout`, `shaderDrawParameters`, `accelerationStructure`,
   `rayTracingPipeline`, `rayQuery` - was never actually requested from the
   driver, despite the startup log claiming otherwise. `git blame` traces
   this to commit `f9bdeb15` (2026-02-18), five months before the Slang
   migration that was originally suspected. Forward/deferred/shadow goldens
   still reported `[ OK ]` before the fix only because the AMD driver renders
   anyway despite the VUID spam; RT/PT hard-aborted because losing
   `bufferDeviceAddress` is fatal there. Fixed: `features2.pNext =
   &features11;`.
2. `Resources/ShadersSlang/raytracing/raytrace.rchit.slang:73` used
   `.Sample(...)` (implicit LOD) in the closest-hit shader. `closesthit` maps
   to the `RayClosestHitKHR` execution model, which has no fragment
   derivatives, so `spirv-val` rejects `OpImageSampleImplicitLod` there
   (`VUID-VkShaderModuleCreateInfo-pCode-08737`, "ImplicitLod instructions
   require Fragment, GLCompute, MeshEXT or TaskEXT"). Only surfaced once (1)
   was fixed and the device stopped rejecting `rayTracingPipeline` outright.
   The old pre-Slang GLSL shader used the same implicit `texture()` call and
   never hit this because glslang auto-lowers `texture()` to explicit-LOD
   outside fragment/compute stages; Slang does not. Fixed:
   `.SampleLevel(textureSamplers[textureId], texCoords, 0.0)`. Shaders
   recompiled via `Scripts/Windows/compile-slang-shaders.ps1`.
   `path_tracing.slang`'s identical-looking `.Sample()` calls are unaffected -
   that shader is `[shader("compute")]`, which IS in the allowed list.

   **Verified on the RX 9070 XT** (container-built `clangcl-debug`,
   `commitTestSuite.exe --gtest_filter=GoldenRender.*`): the
   `multiview`/`bufferDeviceAddress`/`ImplicitLod` VUIDs are completely gone
   from the log (previously every frame logged them). 15 of 17 non-PT-
   accumulation goldens pass clean, including real ray-tracing ones that
   previously never got the chance to run:
   `RaytracedWorldFollowsTheModelTransform`, `AddedModelAppearsInPathTracing`,
   `ForwardLightingRespondsToTheDirectionalLight`, both mask-card and
   KHR-texture-transform tests, both multi-model tests.

**NEW, narrower blocker isolated: path-tracing compute dispatch hits
`VK_ERROR_DEVICE_LOST` with zero preceding validation errors.**
`GoldenRender.PathTracingAccumulatesAndConverges` still hard-aborts
(`ASSERT_VULKAN`) on `vkQueueSubmit` returning `-4` (`ErrorDeviceLost`) at
`frame=2`; `GoldenRender.GuiInputSweepNeverCrashesOrLosesTheDevice` hits the
identical failure (`renderMode=path_tracing`, `vk::Result=-4`) at `frame=0` -
so it is not timing-dependent, it is the `path_tracing.slang` compute kernel
itself (RayQuery-based inline ray tracing, `Src/.../renderer/PathTracing.cpp`)
faulting on the GPU. Because the process aborts on the first hit, every test
ordered after the first PT-exercising one in `goldenRenderSuite.cpp` never
runs - this still includes the GUI-sweep SEH-crash concern from the original
report, which remains unevaluated. Not attempted here: this needs a GPU
capture (RenderDoc/PIX or AMD's crash-dump tooling) or a bisection of
`path_tracing.slang` (RayQuery flags, SBT/accumulation-image binding,
buffer-device-address indexing) to localize - out of scope for this pass.
`docs/gpu-golden-testing.md` should be updated once that lands.

## 2026-07-30 batch III — planner (shader-load consolidation, per-frame copies, docs drift)

Found by a refactor-focused read of the shader-loading call sites, the GUI
frame path, and the agentic-loop docs. Deliberately only three tasks: the
open queue is already deep, and GPU-golden host verification is blocked (see
the diagnosis task above), so nothing here needs a golden run to verify.
None duplicate the 2026-07-24/28/30 batches.

### C++ Vulkan engine

### Docs

  **Test:** None (docs-only). Verification is the grep in step 4 plus
  checking every path named in the rewritten section exists in the tree.

  **Build:** None (docs-only change; no source touched).

  **Context:** Documentation-drift item from the refactor mandate. AGENTS.md
  is the file every agent session loads, so a wrong prompt-file path there
  multiplies into wrong edits. Coordinate with the in-flight working-tree
  changes to `Scripts/AgenticLoop/*` — base the wording on the files as they
  are at execution time, and keep AGENTS.md the summary, README the detail.

## 2026-07-31 batch — planner (perf coverage, resurrected-docs cleanup, Rust frame path)

All five verified against the tree on 2026-07-31. Re-checks done first so
nobody chases stale prose: `reprovisionPerImageResources()` is already
extracted, the `clangcl-tsan` preset is already gone, strip/fan triangulation
and 16-bit PNG down-conversion are already in the Rust loader, and the shadow
cascades already use a render bundle — none of those are tasks (the prose
above was corrected in this pass). The three Rust tasks live in the
`ExternalLib/Kataglyphis-RustProjectTemplate` submodule: commit there AND bump
the superproject gitlink in the same change (AGENTS.md § Critical Invariant:
Submodule Pins).

### C++ Vulkan engine

### Docs / repo hygiene

### Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate`)

- **Pre-existing (found 2026-07-31, unrelated to the work below): every GPU-touching
  test in `kataglyphis_webgpu_renderer` fails headless on this host.**
  `Device::create_shader_module` for `forward_shader` rejects `forward.wgsl` at
  line 435: `textureSampleCompareLevel(shadowMap_0, shadowSampler_0, ...)` types
  as `vec4<f32>`, not the `f32` a compare-sample returns, because `shadowMap_0`
  is declared `texture_2d_array<f32>` (`forward.wgsl:60`) instead of
  `texture_depth_2d_array` — WGSL's compare-sample builtins require a depth
  texture type. Reproduced on a clean `git stash` (pre-dates this session's
  changes): 22/30 `tests/headless.rs` cases fail identically, plus the crate's
  own `render::forward::tests::set_animation_time_recomputes_and_dirties_the_cached_normal_matrix`
  lib test. `forward.wgsl` reads as Slang-generated (obfuscated `_S37`/`sum_1`
  names) — likely a naga/wgpu version bump started enforcing a rule the shader
  was already violating, rather than a new regression in the WGSL itself. Not
  attempted here: fixing the texture declaration (and whatever depends on
  `texture_2d_array<f32>` sampling of the same view elsewhere in the file)
  needs its own careful pass and re-verification of every shadow golden, out of
  scope for the bundle-caching task below, which could only be verified via
  `cargo build` + the unaffected lib tests as a result.

## 2026-07-31 batch II — planner (WGSL depth-texture fix, PT device-lost, glTF skinning, docs gate, perf diffing)

All five verified against the tree on 2026-07-31 before writing. Re-checks done
first so nobody chases stale prose: the Rust `PrimUniforms` dirty gate is DONE
(`uniforms_dirty` at `forward.rs:1890`), the shadow-caster bundle cache is DONE
(`c2c2fe4` in the submodule), the wasm size-budget CI step is already a hard
gate (`wasm-size-budget.sh` does its own `rustup target add` and is fatal), and
the Slang shaders already use CPU-precomputed inverses (the PT "use the
precomputed inverse matrices" survey item is moot post-Slang). Suggested order:
the WGSL fix first — it unblocks headless verification for every future Rust
renderer task.

### Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate`)

**`headless.rs` wrong-pixels investigation — DONE (2026-07-31), 16/33 → 32/33.**
Two systemic root causes, not 17 independent bugs:

1. **Vertex attribute `@location` mismatch.** `Vertex::LAYOUT` /
   `InstanceRaw::LAYOUT` in `scene/mod.rs` numbered attributes sequentially
   (0-7, instances 8-11), but the Slang WGSL backend assigns its OWN
   `@location`s to `forward.wgsl`'s `vertexInput_0/1/2` structs
   (position=0, uv1=1, instance columns=2/3, normal=4, uv=5, tangent=6,
   joints=7, weights=8, color=9, instance columns cont'd=10/11) — a
   completely different numbering the Rust side never matched post-migration.
   Every primitive was reading position/normal/uv/color from the wrong slots.
   Fixed by renumbering both `vertex_attr_array!` calls to Slang's actual
   assignment (entries stay in FIELD order; only the `@location` numbers
   attached to each changed). This alone fixed 13 of the 17: everything except
   `alpha_modes_blend_and_mask`, `auto_exposure_brightens_a_dark_scene_over_successive_frames`,
   `caster_culling_engages_and_shadows_survive`, and `ssao_darkens_geometry`
   (`renders_cube_headless`'s `[137, 128, 124, ...]` grey was garbage vertex
   data landing in the wrong per-vertex slots, not a camera/lighting bug).
2. **Inverted shadow factor.** `forward.wgsl`'s `shadow_factor_0()` returns
   *visibility* (1.0 = lit/unoccluded, via `CompareFunction::LessEqual` PCF —
   confirmed by its own out-of-cascade early return of `1.0`, which only
   makes sense as "fully visible outside the map"). The lighting call site
   nonetheless computed `directLight * (1.0 - shadow_factor_0(...))`, i.e.
   multiplied direct light by *occlusion* instead of visibility: fully lit
   ground got zeroed direct light (ambient-only, dim) and the actual shadow
   footprint got full direct light on top of ambient (brighter than its
   surroundings) — a shadow that visibly glows instead of darkens. Fixed by
   dropping the `1.0 -` inversion. This fixed `alpha_modes_blend_and_mask`
   (the scene was reading uniformly dark because direct light was zeroed
   almost everywhere) and `ssao_darkens_geometry` (SSAO's per-pixel multiply
   can only ever darken; the "SSAO increased total energy" symptom was the
   inverted shadow glow being brighter than the correctly-shadowed baseline
   it was compared against, not an SSAO bug).
   `caster_culling_engages_and_shadows_survive`'s pixel classifier also
   needed a fix alongside: it required *neutral* grey for "shadowed", but
   with analytic IBL a correctly-shadowed patch is blue-tinted (sky-lit
   ambient only), not neutral-dark — `shadow_darkens_plane_under_cube`
   already knew this and used a blue-tint-aware classifier;
   `caster_culling`'s comment claimed to be "the same structural check" but
   wasn't. Brought in line with the proven-correct classifier.

Verified: `cargo test -p kataglyphis_webgpu_renderer --test headless` 32/33,
`--lib` 113/113. Diagnosed via a scratch frame-dump/pixel-diff example
(removed after use, per "always dump the picture, not just the number").

**Remaining: `auto_exposure_brightens_a_dark_scene_over_successive_frames`
still fails — likely a stale test assumption, not a renderer bug (S,
needs an owner decision on the fix shape).** Reproduces stably: manual
mean 171.07, auto converges (within 2 frames, as expected for
`speed=3.0, dt=0.5`) to mean 166.48 — auto exposes *down* ~2.7% instead of
up 8%+. The CPU auto-exposure math (`render::auto_exposure`, 113 unit
tests) and the GPU `cs_reduce_exposure` mirror of it both look correct and
match the working `manual_exposure_still_controls_brightness` test. The
scene (`cube.gltf`, default `OrbitCamera`, dim sun + near-zero ambient) is a
small cube against a procedural sky that fills most of a 128×128 frame and
is *not* attenuated by the test's dim light settings (only the cube's direct
+ ambient terms are) — confirmed visually via a frame dump: the cube is a
small fraction of the image. The geometric-mean-luminance auto-exposure
metric is dominated by that already-well-exposed sky, so it correctly holds
near EXPOSURE_KEY rather than brightening for the small dim subject. The
docstring's historical baseline ("182.7 vs 163.1 auto, a 12% lift") was almost
certainly measured before the vertex-attribute fix above, i.e. on a broken
frame where the cube's real shape/shading didn't dominate either — not a
result that exposure fix #1 preserves. Two honest fixes, either needs a call
the executor loop shouldn't make alone: (a) reframe the test scene so the
dim subject actually dominates the frame (tighter FOV / closer camera /
darker sky), which is what "auto-exposure brightens an underlit scene" is
actually meant to exercise; or (b) accept that a sky-dominated frame
legitimately shouldn't brighten much and lower the threshold. Left failing
and unchecked rather than guessed at.

### C++ Vulkan engine

- [b] **(M) Localize (and fix if cheap) the path-tracing compute
  `VK_ERROR_DEVICE_LOST` on the RX 9070 XT** — **two real bugs fixed
  2026-07-31, the crash on the large-mesh reproducer is NOT; root-causing it
  further needs GPU capture tooling (RenderDoc/PIX/AMD crash dumps), which is
  not available here.**

  **Fixed this pass, both verified on the RX 9070 XT (container-built
  `clangcl-debug`, host GPU run):**
  1. `rayQuery.Proceed()` was called exactly once
     (`Resources/ShadersSlang/path_tracing/path_tracing.slang`). Per
     `GL_EXT_ray_query`/DXR RayQuery semantics it must be looped
     (`while (rayQuery.Proceed()) {}`) until it returns false — a single call
     can leave BVH traversal incomplete, so `CommittedStatus()` and the
     committed-hit accessors read undefined state. The pre-Slang GLSL kernel
     (`path_tracing.comp`, still present in packaged release output) looped
     this correctly; the Slang port dropped the loop. Fixed and kept.
  2. `PathTracing.ixx`'s `SpecializationData` defaulted to
     `(specWorkGroupSizeX=16, specWorkGroupSizeY=8)`, feeding
     `PathTracing.cpp`'s dispatch work-group-count math — but the Slang kernel
     hardcodes `[numthreads(8, 8, 1)]` rather than consuming these as SPIR-V
     specialization constants (Slang does not wire them up), so the dispatch
     under-covered the image width by ~2x every frame. Fixed the default to
     `(8, 8)` to match.

  Neither fix alone, nor both together, resolves the device-lost on the
  original reproducer (below) — they were necessary correctness fixes found
  along the way, not the root cause.

  **NOT fixed — remaining device-lost, narrowed far past the original
  report:** `GoldenRender.PathTracingAccumulatesAndConverges` (scene:
  `Models/Dinosaurs/dinosaurs.obj`, hundreds of thousands of vertices) still
  hard-aborts on `vkQueueSubmit` returning `-4`, zero preceding validation
  errors, within 0-2 frames. Bisection (12+ shader-recompile/host-GPU-run
  rounds, no GPU capture tooling used) found:

  - Consuming ONLY the material-fetched `hitColor` (texture sample or
    `material.diffuse` — never touches the vertex buffer's `.position`/
    `.normal` fields, only `.texture_coords` on the dead branch, see below)
    from the RayQuery-committed hit is safe: proven across 40+ frames and an
    8x-per-invocation RayQuery loop.
  - The instant the caller also consumes a value derived from
    `v0.position`/`v0.normal` (fetched via the `Vertices*` buffer-device-
    address pointer indexed by `rayQuery.CommittedPrimitiveIndex()`-derived
    indices) — **either field alone, in any combination, with or without the
    `CommittedObjectToWorld3x4()`/`CommittedWorldToObject3x4()` transform,
    with or without `sceneUBO` access, with or without `brdf_direct()`,
    whether factored into a separate function taking `RayQuery` by value and
    returning a struct or fully inlined into `path_tracing_main`** — the
    device is lost within 0-2 frames.
  - `dinosaurs.obj`'s materials are untextured (`material.textureID < 0` for
    every primitive — confirmed elsewhere in this file), so the
    `.texture_coords`-consuming branch (texture `.Sample()`) is dynamically
    never taken for this scene; whether reading `.texture_coords` at scale
    would ALSO fault could not be confirmed or ruled out.
  - **Mesh-size correlation, not yet exploited:** every PT/RT golden test that
    reads `v0.position`/`v0.normal` via the identical `Vertices`/`Vertex`
    buffer-device-address struct and does NOT crash
    (`GoldenRender.RaytracedWorldFollowsTheModelTransform` via the RT
    *pipeline* rchit shader, `GoldenRender.AddedModelAppearsInPathTracing`,
    `GoldenRender.PathTracingPassesTheWhiteFurnaceTest`,
    `GoldenRender.PathTracingHonorsTheQualityControls`) uses the tiny
    `Models/ShadowTest/shadow_rig.obj` rig. No existing golden test exercises
    `raytrace.rchit.slang`'s identical `v0.position`/`.normal` read pattern
    against the large dinosaur mesh — so it is UNKNOWN whether this is scoped
    to RayQuery/compute (as the entry originally assumed) or is a more
    general large-mesh vertex-buffer-device-address bug that the RT pipeline
    has simply never been tested against.

  **RESOLVED 2026-07-31 by `GoldenRender.RaytracedLargeMeshDoesNotLoseTheDevice`
  (container-built `clangcl-debug`, verified on the RX 9070 XT):** the RT
  *pipeline* (`raytrace.rchit.slang`) raytraces the same dinosaur mesh over 10
  frames, with a real (non-blank) capture, and does NOT lose the device. It
  reads `v0.position`/`v0.normal` through the identical buffer-device-address
  `Vertices*` path as the faulting PT compute kernel. That rules out the
  shared vertex-upload/BDA path (`ASManager`, `GltfLoader`/`ObjLoader` upload,
  CPU/Slang `Vertex` stride/alignment) as the cause — the bug is confirmed
  scoped to `path_tracing.slang`/RayQuery compute specifically. Full 21-test
  `GoldenRender.*:Integration.*` run (minus the two still-known PT
  device-lose reproducers) stayed green, so this is not a false negative from
  some other suite-wide breakage. Whoever picks this back up next should look
  inside `path_tracing.slang` itself (RayQuery flags, SBT/accumulation-image
  binding, or a compute-specific buffer-device-address indexing bug) rather
  than the shared upload path.

  **Side effect of the two fixes above — previously-invisible bugs newly
  exposed, both unrelated to device-lost, NOT attempted here (out of scope
  for this entry):** with the crash no longer shadowing every test after the
  first PT one, two more PT goldens now run to completion and fail on VALUE
  assertions, not aborts: `GoldenRender.PathTracingHonorsTheQualityControls`
  ("the bounce-cap slider did not change the path-traced image") and
  `GoldenRender.PathTracingPassesTheWhiteFurnaceTest` ("furnace converges LOW
  ... not uniform"). Root cause for both is almost certainly the same thing:
  `path_tracing.slang` never implements a bounce loop at all — `pc_ray.
  max_bounces` is read into the push constant on the C++ side
  (`PathTracing.cpp`) but never referenced in the kernel, which only ever
  evaluates ONE ray-query hit per sample (no recursive/iterative bounce),
  unlike the pre-Slang GLSL kernel's `for (tracedSegments < 8)` loop. That is
  a real feature gap (single-bounce direct lighting only, no GI), separate
  from the device-lost above and worth its own sized entry if picked up.

  **Still true, unevaluated:** `GoldenRender.GuiInputSweepNeverCrashesOrLosesTheDevice`
  and `Integration.RenderModesSelectableInGui` both still device-lose (they
  sweep render modes over the default dinosaur scene) — same reproducer as
  above, not a new failure.

  **Files:**
  - `Resources/ShadersSlang/path_tracing/path_tracing.slang` — the RayQuery
    kernel; the crash-frontier comment block above the hit-info extraction
    records the bisection result in place.
  - `Src/GraphicsEngineVulkan/renderer/PathTracing.ixx`/`.cpp` — dispatch,
    descriptor bindings, the now-fixed specialization-constant defaults.
  - `Test/commit/VulkanEngine/goldenRenderSuite.cpp` —
    `PathTracingAccumulatesAndConverges` (the reproducer),
    `RaytracedWorldFollowsTheModelTransform` (the small-mesh RT counterpart
    that would need a large-mesh variant per "Next step" above).
  - `docs/gpu-golden-testing.md` — how to run container-built binaries on the
    host GPU.

  **Build:** `clangcl-debug` via
  `pwsh -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows-Container.ps1 -Configurations clangcl-debug -SkipTests -FreshContainer`
  (`-FreshContainer` needed here because `PathTracing.ixx` changed), then host
  GPU runs per `docs/gpu-golden-testing.md`. Shader-only iterations:
  `pwsh Scripts/Windows/compile-slang-shaders.ps1`, no C++ rebuild needed.

  **Verified 2026-07-31:** 18/20 in
  `--gtest_filter='GoldenRender.*:Integration.*:-GoldenRender.PathTracingAccumulatesAndConverges:GoldenRender.GuiInputSweepNeverCrashesOrLosesTheDevice:Integration.RenderModesSelectableInGui'`
  pass on the RX 9070 XT (the 2 failures are the pre-existing bounce-loop gap
  above, not crashes) — no regression from the two fixes, and strictly more
  of the suite runs than before (previously the abort shadowed everything
  after the first PT test in file order).

## 2026-07-31 batch III — planner (submodule hygiene, PT bounce loop, RT diagnosis, CI filter, frustum perf)

All five verified against the tree on 2026-07-31 before writing. Findings behind
them: the RPT submodule working tree currently REVERTS the committed
shadow-factor fix (see the first task — do it before any other Rust work);
`path_tracing.slang` confirms the batch-II suspicion (no bounce loop at all,
`max_bounces` declared at line 21 and never referenced again); the old GLSL
`Resources/Shaders/` tree is fully deleted (Slang-only now), so the reference
kernel must come from git history; `PushConstantRasterizerUnit` is the ONLY
CPU suite missing from the Windows CI filter (checked every `TEST(` suite name
against `Windows.yml:209-229`); the ContainerHub commit the RPT working tree
points at (`1de9aff`) is already on ContainerHub `origin/main`, so committing
that bump is safe.

### CI and release gaps

## 2026-07-31 batch IV — planner (refactor: dead code, depth-format consolidation, host/device layout pins)

All three verified against the tree on 2026-07-31 (every "dead" claim below was
re-grepped across `Src/` + `Test/` this pass; the depth-format divergence and
the missing layout pins were confirmed by reading the sites). Candidates found
but deliberately NOT tasked this cycle (queue discipline — next planner cycle
should re-verify and size them): README.md:213-220 names two files that do not
exist (`ShaderIncludes.hpp`, `CompileShadersToSPV.cmake` — the single most
actionable doc drift); `docs/cpp-renderer-improvements.md:66-72,119-121` still
describes runtime-glslc recompilation that was deleted with the Slang
migration; `docs/webgpu-renderer-roadmap.md:121` advertises the naga WGSL
export that `docs/shader-sharing.md:133` documents as retired; per-frame heap
allocations in `CascadedShadowMapMath.cpp` (measurable via
`BM_ComputeCascadeData`); sampler creation triplicated with drifted values
(`PostStage.cpp:187`, `Model.cpp:72`, `Texture.cpp:234`); the 4× duplicated
`DescriptorSetGroup` write prologue; `FileReader.ixx`/`Texture::loadTextureData`
error paths have fuzz-only coverage and no deterministic unit tests.

### C++ Vulkan engine

- [ ] **(S) (refactor) Pin the remaining host/device struct layouts: 3 push-constant structs + ObjMaterial** —
  `pushConstantSuite.cpp` pins `PushConstantRasterizer` only; the other three
  push-constant structs and the BDA-read `ObjMaterial` have no layout guard,
  so a reordered or inserted field desyncs the Slang twin silently (exactly
  how the Rust vertex-attribute mismatch and the PT spec-constant mismatch
  hid).

  **Files to read:**
  - `Test/commit/VulkanEngine/pushConstantSuite.cpp` — the pattern to follow (offsetof/sizeof pins + the doc comment style)
  - `Src/GraphicsEngineVulkan/renderer/pushConstants/PushConstantPathTracing.hpp` (`vec4 clearColor; uint width, height, frame_index, samples_per_pixel, max_bounces`)
  - `Src/GraphicsEngineVulkan/renderer/pushConstants/PushConstantPost.hpp` (`float aspect_ratio; uint clouds_enabled, shadows_enabled, skybox_enabled`)
  - `Src/GraphicsEngineVulkan/renderer/pushConstants/PushConstantRayTracing.hpp` — NOTE the struct is named `PushConstantRaytracing` (lowercase t)
  - `Src/shared/scene/ObjMaterial.hpp` — the scalar-layout contract is documented at `:31-32` and `:38`
  - The Slang twins: grep `Resources/ShadersSlang/` for the matching push-constant blocks (`path_tracing/path_tracing.slang`, the post shader, `raytracing/raytrace.rgen.slang`) and the shader-side material struct (`common/material_fetch.slang` or wherever `ObjMaterial` is mirrored)
  - `.github/workflows/Windows.yml:209-224` — the CPU-only suite filter

  **Steps:**
  1. In `pushConstantSuite.cpp`, add three suites mirroring the existing
     pins: `PushConstantPathTracingUnit` (expect `clearColor` at 0, `width`
     16, `height` 20, `frame_index` 24, `samples_per_pixel` 28, `max_bounces`
     32, `sizeof >= 36` — the std430 push-constant layout the Slang kernel
     reads), `PushConstantPostUnit` (`aspect_ratio` 0, `clouds_enabled` 4,
     `shadows_enabled` 8, `skybox_enabled` 12, sizeof 16),
     `PushConstantRaytracingUnit` (`clear_color` 0, sizeof 16).
  2. Add an `ObjMaterialLayoutUnit` suite pinning the scalar-block layout the
     RT/PT kernels read via buffer device address: with glm's default
     (non-aligned) vec3, expect `ambient` 0, `diffuse` 12, `specular` 24,
     `transmittance` 36, `emission` 48, `shininess` 60, `ior` 64, `dissolve`
     68, `illum` 72, `textureID` 76, `alphaCutoff` 80, `uv_scale` 84,
     `uv_offset` 92, `sizeof(ObjMaterial)` 100. First confirm these against
     the Slang-side struct; **if any expectation fails at runtime, that is a
     REAL C++/Slang layout finding — investigate against the `.slang` twin
     and report it, do not adjust the number to make the test pass.**
  3. Add the four new suite names to the `$cpuOnlySuites` filter in
     `Windows.yml` (follow commit `eb077041` — a suite not in the filter
     never runs in CI).

  **Test:** The new tests themselves. Red-proof one pin locally by
  temporarily reordering two fields in a copy of the struct (do not commit
  the reorder).

  **Build:** `clangcl-debug`, incremental (test-only + workflow change, no
  module interface touched):
  `pwsh -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows-Container.ps1 -Configurations clangcl-debug -SkipTests`
  then run `commitTestSuite.exe --gtest_filter='PushConstant*:ObjMaterialLayoutUnit.*'`
  in the container.

  **Context:** Test-coverage-gap item from the refactor mandate. The header
  comment of `pushConstantSuite.cpp:1-15` explains why this class of bug is
  silent (the push range is self-consistent host-side, so validation cannot
  see the drift); `ObjMaterial.hpp` carries an explicit scalar-layout
  contract that nothing enforces. These four structs are every remaining
  hand-mirrored host/device struct with no pin.

## Completed (kept for the reasoning, not the status)

- **Stage-level RAII** (2026-07-19) — leaf types (`VulkanBuffer`/`VulkanImage`)
  are move-only with destructor release; extended through the render stages.
- **Sync-validated barrier removal** (2026-07-19) — sync validation first
  exposed 10 real depth-sync hazards, fixed in the same unit.
- **GPU timestamps + debug labels per pass** (2026-07-19).
- **`DescriptorSetGroup` extraction** (2026-07-19, VulkanRenderer -617 lines).
- **Clouds compute cost** (2026-07-19) — per-pixel `inverse()` hoisted into
  the UBO, A/B verified. Half-res dispatch still open as a quality tradeoff.
- **CMake preset diet** (2026-07-19: 26/24/1 → 23/22/6) — the real problem
  was one test preset, not preset count.
- **CI sanitizers** (2026-07-19) — ASan+UBSan and TSan steps on Linux CI plus
  a `linux-debug-asan-clang` preset.
- **C++ golden rendering tests** (2026-07-19) — headless capture
  (`requestFrameCapture`/`takeCapturedFrame`, fence-synced) plus structural
  assertions. Works while the desktop is locked, unlike screenshots.
- **Perf suite that measures the engine** (2026-07-19) — camera / projection /
  scene-config / OBJ-parse benchmarks; baseline table above.
- **SceneConfig fuzzing** (2026-07-19) — also fixed fuzz targets never
  enabling `CXX_SCAN_FOR_MODULES`, which had made engine modules unfuzzable.
- **Shadow casters were culled by the shadow pass** (2026-07-20, `f429634f`)
  — the camera projection is Y-flipped for Vulkan, reversing triangle winding;
  the cascade matrices come from `glm::ortho` with no such flip, so back-face
  culling removed exactly the faces the camera keeps. The depth map sat at its
  clear value for ~99.8% of sampled texels. Culling is now off for that
  pipeline. Note the earlier "1.4% occlusion" figure never reproduced (it
  measured 0.031% on re-run) — see the open instrument item above.
- **Cascade fitting** (2026-07-20) — shadows fit a `shadow_distance` (60)
  rather than the camera far plane: 3.80 -> 3.04 cm/texel over the subject.
  A practical/logarithmic split blend exists but defaults OFF (lambda 0)
  because measurement did not support enabling it.
- **Perf suite registered with CTest** (2026-07-20) — gates on "the
  benchmarks execute", not on a time budget; see the commit for why.
- **GUI -> Scene round-trip tests** (2026-07-20) — five CPU-only tests over
  the two-copy split that is a suspect in the instrument disagreement.
- **Shader-file reader fuzzing** (2026-07-20) — found `fileExists` throwing
  on permission-denied paths, which is a terminate with exceptions disabled.
- **CSM caster transform** (2026-07-20, `bf6fa37e`) — the shadow pass used a
  hard-coded identity model matrix while the forward pass used the scene's
  (a scale of 60), so casters rendered at 1/60 size and the depth map never
  left its clear value. Five earlier defects were found and fixed while
  chasing this one; the CPU unit tests in `cascadedShadowMapSuite.cpp` and the
  `ShadowPushCarriesTheSceneModelMatrix` regression guard came out of it.
### sccache solved, and the abseil incompatibility that hid behind everything (2026-07-20)

**sccache: root cause was the volume mount.** Ran the server by hand with
trace logging: every `DiskCache::put_raw` died with os error 3 on the wcifs
volume at `C:\sccache`, while PowerShell could write the same paths - the
failure is specific to how the server writes (tempfile + rename) on wcifs.
`SCCACHE_DIR` now lives in the container FS (`C:\sccache-local`), which is
fine because builds run in the persistent container - the cache lives exactly
as long as the thing using it, and a volume with 100% write errors persisted
nothing anyway. Verified after the fix: **757 cache writes, 0 errors**.

Two traps found while fixing it, both mine:

- `SCCACHE_ERROR_LOG` must not live under `SCCACHE_DIR`: the server opens the
  log BEFORE the disk cache creates its directory, dies if the parent is
  missing, and then every wrapped tool fails with "Timed out waiting for
  server startup". With `RUSTC_WRAPPER=sccache` that poisons `cargo tree`,
  which corrosion reports as "Failed to find a dependency on cxxbridge-cmd" -
  three indirections from the cause. The log now sits at `C:\sccache-error.log`.
- A fresh container exposed that the "working" Windows fuzz build was stale
  objects: FUZZTEST at main does not compile against the abseil LTS it itself
  pins.

**The abseil incompatibility (both platforms, one bug):** `fuzzing_bit_gen.h`
friend-declares `absl::random_internal::{DistributionCaller, MockHelpers}`
without including their headers, and in abseil LTS 20260526 `bit_gen_ref.h`
no longer provides them transitively. Upstream's Bazel CI layers includes
differently, so they do not see it. Fixed in two places with the reason for
the asymmetry recorded: the `fuzztest_*` library targets get a force-include
flag, but OUR fuzz targets cannot - a force-include flows into the
synthesized C++20 module BMI compiles of imported engine modules, which have
no abseil include path (tomlplusplus BMI failed with exactly that) - so the
sources include the two headers before `fuzztest.h` instead, which module
synthesis never sees. Full Windows container build: 3/3 steps, 66 tests pass.

The module-TU bypass still caps sccache's payoff; that part of the sccache
item stays open.

### Incremental container builds can ship ODR-broken binaries (2026-07-20, SEVERE)

Found while landing the GPU-timing JSON export, and it upgrades the recorded
"touching a source file usually does NOT cause the container to recompile it"
papercut from annoyance to correctness bug.

Adding members to `VulkanRenderer` (a C++23 module interface, `.ixx`) and
rebuilding incrementally produced a binary where `commitSuite.cpp` allocated
the OLD sizeof while the constructor wrote the NEW layout - an instant ASan
heap-buffer-overflow at construction. The "rebuild" ran **19 ninja edges**;
consumers of the changed module were never recompiled. Ruled out sccache
first: clearing the cache and rebuilding reproduced the crash (an 83% hit rate
on the post-change build looked damning and was innocent). Only deleting the
build tree on BOTH host and container and cold-building produced a sound
binary - 67 tests pass.

Consequence: **after any module-interface change, an incremental container
build is not trustworthy until the module dependency tracking survives the tar
transport.** Until the mtime/dyndep interaction is fixed, treat "ASan crash at
object construction after touching an .ixx" as build skew, not as a code bug -
and cold-build before debugging anything.

### GPU timings are now a comparable artifact (2026-07-20)

`KATAGLYPHIS_GPU_TIMING_JSON=<path>` makes the renderer accumulate raw (not
GUI-smoothed) per-pass times and write averages on cleanUp. Measured on this
machine over 94 frames: ShadowCascades 0.066 ms, Main 0.042 ms, Post 0.037 ms,
Sky 0.024 ms. Unsupported timestamps still write the file with
`"timestamps_supported": false`, so "cannot measure" and "never ran" are
distinguishable. The Rust renderer exposes the same numbers via
`gpu_timings_ms()`, so the side-by-side harness can now compare timings.

### #74 The Linux fuzzer lane — ROOT CAUSE FOUND AND FIXED (2026-07-20)

Red since 2026-05-17. **The cause was an ODR violation, not FUZZTEST and not
the toolchain**, and both of my earlier hypotheses were wrong.

`Test/fuzz/CMakeLists.txt` applied `-fsanitize=address` **per fuzz target**,
while the abseil that FuzzTest links was built without it. Abseil's
`raw_hash_set` layout depends on whether ASan is active, so the two disagreed
about container internals and the binary died during startup — before reaching
a single test, which is why it crashed even while merely *listing* tests.

Reproduced locally in the CI image (Rancher Desktop — see ContainerHub
`docs/rancher-desktop-linux-containers.md`), same source, same compiler, one
variable changed:

| ASan applied to | Result |
|---|---|
| the fuzz target only (what CI did) | `raw_hash_set.h:1016` assertion, *"Try enabling sanitizers."* / SEGV |
| **every TU** | **2 tests PASSED** |

The local build also gave a legible assertion where CI only ever showed
`SEGV on unknown address 0x000000000000`. Three months of that bare SEGV cost
far more than the twenty minutes the container took.

**Windows was never affected**, and the reason is the whole story: on Windows
`ExternalLib/CMakeLists.txt` builds a `kataglyphis_fuzztest_windows_asan`
interface library and propagates the flags to abseil, re2 and every
`fuzztest_*` target by hand. Linux got the per-target flag and none of that
propagation. The bug is that asymmetry.

**The fix:** fuzz targets now *require* `myproject_ENABLE_SANITIZER_ADDRESS`
project-wide on Linux and refuse to build otherwise, the per-target flag is
gone, and CI runs them from `build-asan-clang` instead of the plain Debug tree.

**Two other things this turned up:**

- `:latest` had not been rebuilt since 2026-04-16 while `:latest-cross` is
  refreshed routinely. CI now builds against `:latest-cross`.
- That switch exposed a hardcoded `--gcc-toolchain=/opt/gcc-15.2.0` in 32
  places in `Linux.yml`; the cross image ships **gcc-16.1.0**, so linking
  failed with `cannot find crtbeginS.o`. Updated. Worth deriving rather than
  hardcoding if it moves again.

**The second failure arrived on schedule, plus a third problem that explains
the lane's whole history of lying.** The ODR-fix run failed differently:
`build-asan-clang/first_fuzz_test: No such file or directory`. The ASan build
step had reported success in ~30 seconds — because it failed at configure and
`cmd 2>&1 | tee log` reports tee's exit code, and the runner's default shell
has no pipefail. **Every build step in `Linux.yml` was masked this way**; only
the fuzzer step, which has no `tee`, could ever surface failure. That is why
the lane's failures always landed on the fuzzer step regardless of what was
actually broken. Fixed with an explicit `shell: bash` default (`-eo pipefail`).

The underlying configure failure: the `:latest-cross` image runs as uid 1001
(`kataglyphis`) with `CARGO_HOME=/usr/local/cargo` owned by root, so
Corrosion's cargo dies with "failed to create directory .../registry".
Verified in the image locally; `cmake-configure-build.sh` now falls back to a
writable `${TMPDIR:-/tmp}/cargo-home` when the configured one is unwritable
(fallback itself verified in the image: cargo 1.93.1 runs).

**Lesson recorded on my own process:** I ruled out FUZZTEST pin drift against a
breakage date I had not verified, then spent three CI round trips on a control
that moved two variables at once. The local reproduction took one container run
and answered it outright. Get the failing thing into a shell before theorising.
