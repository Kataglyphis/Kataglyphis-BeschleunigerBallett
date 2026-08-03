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
  `GUIRendererSharedVars::gpuTimings` (GUI "GPU timings" header). ~~A headless
  mode that renders N frames and dumps the per-pass averages as JSON would
  turn them into a comparable artifact~~ — **that exists** (corrected
  2026-08-01): the `KATAGLYPHIS_GPU_TIMING_JSON` export in
  `GpuTimingSubsystem.ixx:211` writes one JSON object keyed by
  `FrontendShared::GPU_TIMED_PASS_EXPORT_NAMES`, the Rust side mirrors it via
  the `dump_gpu_timings` example, and `Scripts/Compare-RendererTimings.ps1`
  drives both and prints them side by side. What is still missing is the
  *assertion*: nothing sets a budget for `GpuTimedPass::ShadowCascades` (or
  any other pass), so the artifact is comparable but ungated.
- **Regression tracking**: Google Benchmark can emit JSON
  (`--benchmark_out=... --benchmark_out_format=json`); storing one baseline
  per machine and diffing beats eyeballing console output. **Done
  (2026-07-31):** `Scripts/Compare-PerfBaseline.ps1` diffs a fresh JSON run
  against the checked-in `Test/perf/baselines/win-9070xt-32core.json`, flags
  any benchmark that regressed beyond a configurable tolerance (default
  +25%), and is deliberately not wired into CI (see the "measured baseline"
  table below for why: machine-dependent numbers).

### Measured baseline (2026-07-31, clangcl-profile, 32-core 4.3 GHz)

`Test/perf/baselines/win-9070xt-32core.json` is the machine-readable source of
truth (checked in, diffed by `Compare-PerfBaseline.ps1`); the table below is a
human-readable summary of it. Keep the two in sync — a 2026-08-01 re-run
confirmed the `BM_ComputeCascadeData` rows below (the JSON previously had them
wrong by ~3x; fixed to match this table, which a fresh run agreed with).

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
| `BM_GltfParse_CubeGlb` | 13.2 us |
| `BM_GltfParse_CubeTextured` | 15.7 us |
| `BM_ComputeCascadeData/1` (`std::array` corners) | 143 ns |
| `BM_ComputeCascadeData/3` (`std::array` corners) | 329 ns |
| `BM_FrustumCull/64` | 339 ns |
| `BM_FrustumCull/512` | 2.93 us |
| `BM_FrustumCullShadowCaster/64` | 326 ns |
| `BM_FrustumCullShadowCaster/512` | 2.77 us |

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

- ~~**Multiview `viewMask` validation warning (observed 2026-07-23)**~~ —
  **CONFIRMED RESOLVED (re-checked 2026-08-01).** The blocker this bullet was
  waiting on (`#2106`, "multiview feature is not enabled") was root-caused and
  fixed on 2026-07-31: `VulkanDevice.cpp` set `features2.pNext = nullptr`,
  severing the whole Vulkan11/12/13 feature chain, so `features11.multiview`
  never reached `vkCreateDevice` (see "GPU host verification" below). The
  follow-up RX 9070 XT run recorded there states the `multiview` VUIDs are
  "completely gone from the log". Both instruments therefore agree and there is
  nothing left to re-run. Historical detail kept below because the
  `clampCascadeCount` machinery it describes is still live code. `VulkanDevice`
  now queries
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
- **Synchronization validation** — run
  `pwsh -ExecutionPolicy Bypass -File .\Scripts\Windows\Run-SyncValidation.ps1`
  (documented in `docs/gpu-golden-testing.md`). It sets
  `khronos_validation.validate_sync = true` via `Scripts/vk_layer_settings.txt`,
  copied next to the executable for the run, and exits non-zero on any
  `SYNC-HAZARD` in the log. This found 10 real WRITE-AFTER-WRITE hazards in
  July 2026; it is still not part of any automated run (needs a GPU), so it
  needs a deliberate pass after touching render passes, barriers, or
  frames-in-flight — the script just makes that pass one command instead of
  a hand-written layer settings file.
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
  `goldenRenderSuite.cpp` now carries 29 runnable GPU tests (forward/deferred raster,
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
verified, not blind — each was confirmed by the full GPU golden suite passing
(the count as of those dates; see `docs/gpu-golden-testing.md` for the current
tally).

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
- **Packaging paths are only half exercised** (corrected 2026-08-01 — the
  previous "never exercised" was stale). Linux CI *does* package: `Linux.yml`
  runs a `linux-release-clang` configure/build followed by a
  `--build-target package` step and uploads `*.deb` / `*.tar.gz` / `*.tgz` /
  `*.AppImage` / `*.flatpak`. Still unexercised: **WiX**
  (`windows-clang-release-wix`), which nothing builds anywhere, and **MSIX**,
  which `Windows.yml` collects (`**/*.msix`) but only inside the workflow
  that is itself gated on `[build-win]` — so in practice neither Windows
  packaging path runs unless a commit message opts in.
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
  - **FrameSync — DONE 2026-07-23 (`e7e7579d`, module `kataglyphis.vulkan.frame_sync`; the golden suite as of that date green).** For reference, the state was
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
    surface as validation errors / device hangs — run the full golden suite on
    the RX 9070 XT (`docs/gpu-golden-testing.md`). Touches `VulkanRenderer.ixx` →
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

- ~~**`self.depth` (the resolved single-sample depth buffer) appears to read as
  wrong/empty on the RX 9070 XT, breaking every one of its consumers**~~ —
  **root-caused and fixed (2026-08-01), two independent bugs, not one.** Batch
  IV named the first correctly (Slang's WGSL backend not honouring `SV_Depth`,
  emitting a colour `@location(0)` instead of `@builtin(frag_depth)`) and that
  fix alone was applied first — it did **not** turn the tests green. An
  isolated repro (a synthetic MSAA depth texture cleared to a known value, run
  through the actual `depth_resolve` pipeline in complete isolation from the
  rest of the renderer) showed the resolve pass itself was correct once
  patched, yet the full renderer still resolved every pixel to 0.0. Second bug
  found by that repro: the forward pass's `depth_msaa` attachment used
  `store: wgpu::StoreOp::Discard` (`forward.rs`, comment read "Discard MSAA
  depth; the resolve pass copies to `depth`") — but the resolve pass is a
  **separate** render pass recorded *after* the forward pass ends, not a
  subpass, so the store op already discarded the content before resolve ever
  read it. Changed to `StoreOp::Store`. With both fixes: `a_cube_hidden_behind_
  another_reads_back_zero_samples`, `two_side_by_side_cubes_are_both_visible`,
  `an_occluded_primitive_is_actually_skipped_in_the_opaque_pass`,
  `loading_a_new_scene_does_not_inherit_the_old_scene_visibility` and
  `ssao_darkens_geometry` are now green. `alpha_modes_blend_and_mask`, also
  flagged here as unexplained by this bug, is green too as of 2026-08-01 (came
  back passing on its own after the `cascade_splits` double-duty fix, before
  any targeted investigation into this specific test) — the "blend pass
  doesn't touch `self.depth`" note above was never confirmed as its actual
  cause. The two GPU-culling-specific tests
  (`an_occluded_primitive_is_skipped_with_gpu_culling`,
  `two_visible_cubes_are_both_drawn_with_gpu_culling`) are now green, recorded
  passing under `1594a4a0` (see the batch XIV planner notes below).
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
  `pwsh -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows-Container.ps1 -Configurations clangcl-debug -FreshContainer`
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

## 2026-07-31 batch V — planner (docs drift, per-frame allocations, sampler drift, error-path tests)

The actionable queue was empty when this batch was written (only `- [b]` entries
remained). All five tasks below are the candidates batch IV deliberately deferred
"for the next planner cycle to re-verify and size" — each one was re-checked
against the tree this pass:

- README.md:213-220 — **confirmed**: neither
  `Src/GraphicsEngineVulkan/vulkan_base/ShaderIncludes.hpp` nor
  `Src/GraphicsEngineVulkan/cmake/CompileShadersToSPV.cmake` exists anywhere in
  the repo (`find` returns nothing).
- `docs/cpp-renderer-improvements.md` — **confirmed**: :66-72 still describes
  "runtime glslc resolution" and "the loader no longer recompiles GLSL", :119-121
  still tells you to edit a GLSL source and rely on `compile-shaders.ps1`. Only
  `Scripts/Windows/compile-slang-shaders.ps1` exists; there is no runtime shader
  compilation at all (AGENTS.md § Shaders).
- `docs/webgpu-renderer-roadmap.md:121` — **confirmed**: advertises the naga
  WGSL→SPIR-V export as a shipped feature while `docs/shader-sharing.md:133`
  documents the same route under "Historical note: the retired naga/WGSL-export
  route".
- `CascadedShadowMapMath.cpp:29-48` — **confirmed**: `frustumCornersWorldSpace`
  returns a `std::vector<glm::vec4>` built by 8 `push_back`s from empty, once per
  cascade.
- Sampler triplication — **confirmed** at `PostStage.cpp:188`, `Model.cpp:76`,
  `Texture.cpp:237`, with real drift between them (see the task).
- `FileReader.ixx` / `Texture::loadTextureData` — **confirmed**: the only
  coverage is `Test/fuzz/`; no deterministic suite exercises either.

Candidates found but NOT tasked this cycle (queue discipline; re-verify next
pass): the 4× duplicated `checkWritePreconditions`/`findBinding` prologue in
`DescriptorSetGroup.cpp:191,217,241,267` — three lines each, and hoisting it
needs an out-param, so the consolidation is likely worse than the duplication;
`VulkanRenderer.cpp:1194` copies `scene->getObjectDescriptions()` by value
(needs a check of whether that path is per-frame or scene-change-only before it
is worth a task); `ExternalLib/NLOHMANN_JSON` gitlink is drifted in the working
tree again (`eaedec85` → `2222d386`) — the recurring drift recorded in
`[[submodule-pin-drift]]`, an owner call rather than an executor task.

### Docs

### C++ Vulkan engine

## 2026-08-01 batch — planner (texture-slot cap, redundant raster in RT/PT, per-frame allocs, Rust tile binning)

The actionable queue was empty when this batch was written (only `- [b]` entries
remained across the whole file). Every claim below was read out of the tree this
pass, with the numbers measured rather than asserted:

- **`MAX_TEXTURE_COUNT` is 24 and the release default scene needs 33.**
  `Src/GraphicsEngineVulkan/common/host_device_shared_vars.hpp:8` and
  `Resources/ShadersSlang/common/scene_types.slang:8` both say `24`.
  `SceneConfig.cpp:131` selects `Models/crytek-sponza/sponza_triag.obj` under
  `NDEBUG`; its `.mtl` declares **24 materials referencing 33 distinct texture
  files** (counted: `grep -c '^newmtl'` = 24, unique `map_*` targets = 33). So
  the shipping release build overflows the cap on its own default scene — the
  warning at `VulkanRenderer.cpp:1427` fires and 9 textures never get bound.
  This is the part of the 2026-07-22 item #3 that was deliberately left: the
  flattening + `texture_offset` landed, the cap did not.
- **The whole binding block is duplicated host↔shader with nothing pinning it.**
  `host_device_shared_vars.hpp` and `common/scene_types.slang` carry the same
  seven constants (`MAX_TEXTURE_COUNT`, `globalUBO_BINDING` … `SHADOW_MAP_BINDING`,
  `TLAS_BINDING`, `OUT_IMAGE_BINDING`, `ACCUMULATION_IMAGE_BINDING`) as two
  independent literals. `pushConstantSuite` pins push-constant *layouts*; nothing
  pins these.
- **The raster pass runs and is thrown away every RT/PT frame.**
  `record_commands` calls `recordRasterPass` unconditionally
  (`VulkanRenderer.cpp:897`) and then `recordRaytracingOrPathTracing`
  (`:899`). `activeOffscreenTexture()` (`:819`) returns *the same* texture the
  rasterizer just rendered into, and the rgen/PT dispatch writes every pixel of
  it. Confirmed by the barrier at `Raytracing.cpp:94`, whose `oldLayout` is
  hard-coded to `eShaderReadOnlyOptimal` precisely *because* the raster render
  pass ran first (`PathTracing.cpp:83` is identical).
- **Per-frame heap allocations survive in three record paths.**
  `DeferredRasterizer.cpp:487` allocates a `std::vector<vk::Buffer>` **per mesh
  per frame**; `SkyBox.cpp:457` one per frame; `CascadedShadowMap.cpp:448`
  (`cascadeFrusta`) and `:484` (`shadowDescriptorSets`) one each per frame.
  `Rasterizer.cpp:154-156` and `CascadedShadowMap.cpp:519-521` already use the
  pointer form, so the fix is the pattern the file itself establishes.
- **The Rust tiled-lighting binning ignores light range.**
  `forward.rs:228-327` projects each light's *position* to one pixel and bins it
  into exactly ONE `16×16` tile; `light.range` is packed at
  `packed[base+1][3]` (`forward.rs:212`) and never read by the binner. The
  shader honours the grid (`src/shaders/forward.wgsl:448,468,484`) and only
  falls back to all-lights when `tileW == 0`. Lights whose centre is off-screen
  or behind the camera are dropped entirely (`:308-312`), and a
  `CpuLightKind::Directional` entry (kind `3.0`, no meaningful position) is
  binned by its position like a point light.

Candidates found but NOT tasked this cycle (queue discipline; re-verify next
pass): `VulkanRenderer.cpp:1196` copying `getObjectDescriptions()` by value is
**not** a per-frame cost — it sits in `create_object_description_buffer()`,
which only runs on scene change, so the batch-V deferral resolves to "leave it";
`kind` is bound and never used in both loops of `build_tile_light_grid`
(`forward.rs:250,306`) — a warning-level nit that the range-aware rewrite below
will consume anyway; `forward.rs` is 4084 lines and is the Rust-side equivalent
of the `VulkanRenderer` hub, but no clean extraction was identified this pass.

### C++ Vulkan engine

### Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate`)

## 2026-08-01 batch II — planner (refactor: dead generality, extension-dispatch triplication, span-ify layout params)

The actionable queue was empty when this batch was written (only `- [b]` entries
remained across the whole file). Every claim below was read out of the tree this
pass:

- **`CascadedShadowMap` still carries the pre-multiview per-cascade
  containers.** `framebuffers` and `shadowMapLayerViews`
  (`CascadedShadowMap.ixx:133,137`) are `std::vector`s that `createFramebuffers`
  `resize(1)`s (`.cpp:169-170`) and only ever indexes at `[0]` (`:184,189,196`,
  and `:461` in `recordCommands`). The comment at `:165-168` says so outright:
  "The per-layer views and per-cascade framebuffers are gone." The accessor
  `getFramebuffers()` (`.ixx:92`) is **dead** — grepped across `Src/` and
  `Test/`, zero callers.
- **Model-file extension dispatch exists in three independent copies, and has
  already drifted once.** `SceneConfig.cpp:100-105` lowercases
  `path().extension()` and tests `== ".obj" || ".gltf" || ".glb"`;
  `Scene.cpp:38-43` (`loadModelByExtension`) lowercases the **whole path** and
  tests `ends_with(".gltf") || ends_with(".glb")`;
  `AsyncModelParse.ixx:125-132` (`isGltfPath`) is a third, byte-identical copy
  of the `Scene.cpp` logic. The comment at `SceneConfig.cpp:97-99` records the
  drift that already shipped: that filter was `== ".obj"`, so `cube.glb` could
  never be picked and `.OBJ` was invisible. Adding a format today means editing
  three places, and the two `ends_with`-on-full-path copies also misclassify a
  path whose *directory* ends in `.glb`.
- **Five stages take pipeline-layout arrays as `const std::vector<...>&` while
  the record path has already moved to `std::span`.** `Rasterizer`,
  `DeferredRasterizer`, `PostStage`, `Raytracing` and `PathTracing` each declare
  three functions (`init`, `shaderHotReload`, `create*Pipeline*`) taking
  `const std::vector<vk::DescriptorSetLayout> &`. Inside, the parameter is used
  **only** via `.size()` / `.data()` into `vk::PipelineLayoutCreateInfo`
  (verified at `DeferredRasterizer.cpp:328,358`, `PathTracing.cpp:206,209`,
  `PostStage.cpp:303`, `Rasterizer.cpp:424`, `Raytracing.cpp:254`) — a drop-in
  `std::span` substitution. Six caller-side `std::vector` temporaries of 1-2
  elements exist purely to satisfy the parameter type
  (`VulkanRenderer.cpp:101,102,117,420,423` and the RT pair at `:123,428`).
  Commit `6e4d0204` already did exactly this for descriptor *sets* across the
  record path; this is the same convention applied to the init/hot-reload path.

Candidates found but NOT tasked this cycle (queue discipline; re-verify next
pass): the `auto r = device...createX(info); ASSERT_VULKAN(...); h = r.value;`
idiom appears at **52** sites and could collapse into a `vkCheck()` helper, but
that is a whole-codebase sweep touching ~20 files in one commit — wants a
deliberate moment, not an executor session; `PostStage.cpp:272` is the only
`createRenderPass` call using the C-style `(&info, nullptr, &out)` overload
while the other four use the `ResultValue` form (a one-line consistency nit,
folded into the `vkCheck()` sweep if that ever happens); the per-stage
`createRenderPass` bodies are genuinely different (1, 2 and 3 subpass
dependencies, different attachment sets) and are **not** a consolidation
target.

## 2026-08-01 batch III — planner (dead GPU-culling path, unchecked Vulkan results, shadow/CI test gaps)

The actionable queue was empty when this batch was written (only `- [b]` entries
remained across the whole file). Every claim below was read out of the tree this
pass:

- **The Rust GPU compute culling path is wired to nothing.** `gpu_culling` is
  referenced at exactly five places in `forward.rs` (`:562, :573, :1117, :1119,
  :2314-2316` — grepped). `GpuCulling::cull()` runs the compute dispatch and
  copies the visibility buffer to `readback_buffer` (`gpu_occlusion.rs:240-247`),
  but **`GpuCulling::readback()` (`:251`) has no caller anywhere in the crate**,
  so `GpuCulling::visibility` stays an empty `Vec` forever. The draw loop's skip
  is gated on `self.occlusion_queries_enabled && !self.occlusion.visible(i)`
  (`forward.rs:2212-2216`) and never consults `gpu_culling` at all. Net effect:
  setting `gpu_culling_enabled = true` costs a compute dispatch plus a buffer
  copy every frame and culls nothing — and because the dispatch sits in an
  `if/else if`, turning it on also *disables* the hardware-query path that does
  work. Zero tests reference `gpu_culling_enabled` (`tests/occlusion.rs` covers
  only the query path). `inv_view_proj` is passed as `Mat4::IDENTITY` with a
  `// placeholder` comment (`:2328`); harmless today only because
  `gpu_cull.wgsl` declares the field (`:9`) and never reads it.
- **The doc comment on `occlusion_queries_enabled` is two increments stale.**
  `forward.rs:566-569` says "this increment only DETECTS occlusion … it does not
  yet skip any draw, so the frame renders exactly as before whether it is on or
  off". It does skip: `:2212`. The test
  `an_occluded_primitive_is_actually_skipped_in_the_opaque_pass`
  (`tests/occlusion.rs:164`) exists precisely because it skips.
- **~20 Vulkan creation calls take `.value` with no `ASSERT_VULKAN`.** Measured
  by scanning every engine `.cpp` for `.value` with no `ASSERT_VULKAN` in the
  preceding six lines. The worst is `VulkanInstance.cpp:79`,
  `instance = vk::createInstance(create_info).value;` — and it is reached even
  when the extension check *failed*: `:70-73` logs `spdlog::error("VkInstance
  does not support required extensions!")` and then falls through to create the
  instance anyway. On a driver missing an extension that is a guaranteed
  `VK_ERROR_EXTENSION_NOT_PRESENT`, a null `VkInstance`, and
  `VULKAN_HPP_DEFAULT_DISPATCHER.init(instance)` on it — a crash with no
  diagnostic, one line after the diagnostic was already known. `Model.cpp:84-88`
  carries a `TODO` admitting the same pattern for `createSampler`.
- **Shadow-path GPU coverage is still one number.** `goldenRenderSuite.cpp` has
  25 tests; the only shadow oracle is `ShadowsDarkenSomePixels` (:565), a
  darkened-pixel *ratio*. `CascadedShadowMapUnit.CascadesRespondToLightDirection`
  (:288) pins the same property on the CPU, but nothing checks end-to-end that
  the light direction reaching the shadow pass actually moves the rendered
  shadow — the exact gap that produced the "shadow baked into the model reported
  as cast" retraction recorded above.
- **The Windows CI test filter is a hand-maintained list that has already
  drifted twice.** `.github/workflows/Windows.yml:209-237` enumerates 27
  `<Suite>.*` globs. `Test/commit/VulkanEngine/*.cpp` defines 29 suite names, of
  which `GoldenRender` and `Integration` are the deliberate GPU exclusions — so
  the list is complete *right now*, but only because commit `eb077041` had to
  add `PushConstantRasterizerUnit` by hand after it was missed. AGENTS.md and
  this file both already warn "a suite added to the repo does not run in CI
  unless it is added to the filter"; nothing enforces it.

Candidates found but NOT tasked this cycle (queue discipline; re-verify next
pass): `Model::addSampler` creates one `vk::Sampler` per texture with identical
parameters except `maxLod`, so Sponza allocates 33 near-duplicate samplers
against a device `maxSamplerAllocationCount` — dedup is real but small;
`Model::cleanUp` (`Model.cpp:26-37`) does not `meshes.clear()` while it clears
every other container, so a second `cleanUp()` re-walks already-cleaned meshes
(idempotent in practice, inconsistent on its face); the `# cascades` GUI slider
still advertises 1..8 against `MAX_CASCADES` 3 — now cheaply fixable since
`host_device_shared_vars.hpp` is a plain header the GUI could include, but the
engine-side clamp already makes it a cosmetic lie rather than a bug.

### Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate`)

## 2026-08-01 batch IV — planner (depth-resolve root cause, generated-shader gates)

The actionable queue was empty when this batch was written (only `- [b]` entries
remained across the whole file). Every claim below was read out of the tree this
pass.

**The depth-resolve regression from batch III is ROOT-CAUSED.** Batch III recorded
it as "not root-caused … needs a dedicated session", with three second-order
symptoms (occlusion counts always 0, SSAO adding energy, 0 composited alpha
pixels) and the MSAA depth resolve named as prime suspect. It is the resolve, and
the mechanism is exact:

- `Resources/ShadersSlang/depth_resolve/depth_resolve.slang:20` declares
  `float fs_main(FullscreenVsOut In) : SV_Depth` — correct.
- The Slang **WGSL backend does not honour `SV_Depth`**. Both the generated
  `Resources/ShadersSlang/build/combined_depth_resolve.wgsl:21` and the
  checked-in crate copy
  `crates/webgpu_renderer/src/shaders/depth_resolve.wgsl:19-22` emit
  `struct pixelOutput_0 { @location(0) output_0 : f32 }` — a **colour** output,
  not `@builtin(frag_depth)`. Confirmed globally: `grep -o '@builtin([a-z_]*)'`
  across every `src/shaders/*.wgsl` yields `position`, `vertex_index`,
  `global_invocation_id`, `local_invocation_index` — **zero** `frag_depth`.
- `create_depth_resolve_pipeline` (`forward.rs:2860-2880`) declares
  `targets: &[]` and the pass declares `color_attachments: &[]`, so that
  `@location(0)` output is silently discarded. Depth is therefore written from
  the *rasterized* `svPosition.z`, which `vs_main` hard-codes to `0.0`
  (`depth_resolve.slang:14`), under `depth_compare: Always, depth_write_enabled:
  true`. **`self.depth` ends up 0.0 at every texel, every frame.**
- That single fact explains all three symptoms without needing three bugs. The
  occlusion AABB pipeline is `depth_compare: LessEqual` with write off
  (`occlusion.rs:173-174`) against `self.depth`: against a uniform 0.0 every
  AABB fragment fails, so **every** primitive reads 0 samples whether occluded
  or not — precisely the reported symptom. SSAO reconstructs position from the
  same buffer; the tonemap/alpha composite reads it too.
- Both compile scripts already carry a **post-emit patch table** for exactly
  this class of Slang-WGSL-backend deficiency —
  `$DepthTexturePatches` (`Scripts/Windows/compile-slang-shaders.ps1:231-249`)
  and the `sed -i -E` block (`Scripts/Linux/compile-slang-shaders.sh:183-196`),
  both of which already patch `depth_resolve.wgsl`. So the fix has an
  established home, and this is the same failure mode as commit `64f5053e`
  ("fix Slang WGSL-backend depth-texture emission blocking all Rust GPU tests").

Two gate gaps let it through, both tasked below: the two patch tables are
independent hand-written copies with nothing pinning them, and `depth_resolve`
is one of three `src/shaders/*.wgsl` files absent from `tests/shader_export.rs`'s
hard-coded `SHADERS` list — the checked-in generated WGSL has **no** staleness or
validity gate on the Rust side at all (`BuildIntegrity`'s
`CompiledShadersAreNotOlderThanTheirSources` /
`EveryShaderSourceHasCompiledBinary` walk `build/spirv` only).

Also verified this pass and folded into the tasks rather than tasked separately:
`depth_resolve.slang:26` loops `i < 4u` while `GetDimensions` already wrote
`samples` into an unused local (silent wrongness if `MSAA_SAMPLE_COUNT` ever
moves off 4); `forward.rs:379-385`'s doc comment on `occlusion_queries_enabled`
still claims "it does not yet skip any draw" — the twin at `:566` was corrected,
this one was missed, and `:2218` does skip.

Candidates found but NOT tasked this cycle (checked, then rejected with a
reason — do not re-propose without new evidence): **the RT/PT barrier
`oldLayout`** after commit `60729e06` skipped the raster pass is *correct* — that
commit moved both `Raytracing.cpp` and `PathTracing.cpp` to `eUndefined` and ran
a `validate_sync` pass; **`Model` sampler dedup** (Sponza allocates 33 samplers
differing only in `maxLod`) is not worth it — `modelTextureSamplers[t]` is
positionally indexed by the descriptor write (`VulkanRenderer.cpp:1484`) and the
destroy loop (`Model.cpp:32-34`) would double-destroy shared handles, so dedup
costs an index indirection plus a separate owned-unique list to buy headroom
against a limit whose spec minimum is 4000; **`Texture::textureSampler` is not a
duplicate** of the `Model` sampler — `createTextureSampler` has five callers
(clouds/shadow map/skybox) and none of them are model textures; **extracting the
animation samplers** (`keyframe_lerp_indices`/`cubic_spline_weights`/`sample_*`,
`forward.rs:3188-3324`) out of the 3968-line hub would be pure code motion — they
already have inline unit tests at `forward.rs:3711-3940`; **a C++ headless
per-pass GPU-timing JSON dump** already exists (`KATAGLYPHIS_GPU_TIMING_JSON`,
consumed by `Scripts/Compare-RendererTimings.ps1`).

### Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate`)

### Build / scripts

### C++ Vulkan engine

## 2026-08-01 batch V — planner (refactor pass)

The actionable queue was empty again when this batch was written (only `- [b]`
entries across the whole file, and batch IV's three subsections above are drained).
Every claim below was read out of the tree this pass; the three tasks are all
C++ Vulkan engine, all found by a dead-code / API-consolidation / doc-drift sweep.

Verified this pass and folded into the tasks rather than tasked separately:
batch IV's two "folded" fixes **did land** — `depth_resolve.slang:26` now loops
`i < samples` from `GetDimensions`, and `forward.rs`'s `occlusion_queries_enabled`
doc comment no longer claims "it does not yet skip any draw". Nothing to re-do there.

**One build-system fact that all three tasks depend on, checked this pass:**
`kataglyphis_collect_module_interfaces` (`cmake/KataglyphisCMakeHelpers.cmake:10-13`)
uses a plain `file(GLOB_RECURSE ... *.ixx)` with **no `CONFIGURE_DEPENDS`** — unlike
the commit-test glob (`Test/commit/VulkanEngine/CMakeLists.txt:9`), which has it
precisely because a new file was otherwise silently never compiled. So **adding or
deleting any `.ixx` requires a CMake re-configure**, and combined with the recorded
module-BMI skew hazard ("Incremental container builds can ship ODR-broken binaries")
every task below wants `-FreshContainer`, not an incremental build.

Candidates found but NOT tasked this cycle (checked, then rejected with a reason —
do not re-propose without new evidence): **unifying the CSM shadow-pass draw loop**
(`CascadedShadowMap.cpp:499-528`) into task 3's helper — it ORs visibility across
`cascadeFrusta` via `isVisibleAsShadowCaster` and pushes a *different* push-constant
type, so folding it in would need a predicate + a type parameter and would
misrepresent two genuinely different loops as one; **`Scene::getObjectDescriptions()`
returning `std::vector` by value** (`Scene.ixx:110`) — one caller
(`VulkanRenderer.cpp:1217`, inside `rebuildObjectDescriptions`), which runs on scene
change, not per frame, so the `std::span` treatment that paid off for
`BM_AvailableModelPaths` buys nothing measurable here; **a shared framebuffer-creation
helper** across the five `vk::FramebufferCreateInfo` sites (Rasterizer, DeferredRasterizer,
PostStage, CascadedShadowMap, SkyBox) — they differ in attachment count, layer count
and multiview, so the helper would be almost all parameters.

### C++ Vulkan engine

## 2026-08-01 batch VI — planner (Slang-source shadow inversion, GPU-cull root cause, generated-artifact gate)

The actionable queue was empty again when this batch was written (only the eight
`- [b]` entries across the whole file; batch IV's and batch V's subsections are
drained). Every claim below was read out of the tree this pass.

**The headline finding: the inverted shadow factor has now been "fixed" twice and
reverted twice, because both fixes were applied to a GENERATED file.**
`git log -L 365,365:Resources/ShadersSlang/forward/forward.slang` returns exactly
one commit — `40b1cbe3 complete slang migration` — so the Slang source has read
`directLight *= 1.0 - shadow;` since the day it was written and has never been
touched. Meanwhile `1dd2ebd` (Jul 31, "fix … inverted shadow factor", 16/33 →
32/33 headless) and `9acdc0a6` (Jul 31, "restore reverted RPT shadow-factor fix")
both edited only `crates/webgpu_renderer/src/shaders/forward.wgsl`, a
`compile-slang-shaders` output. The RPT working tree is dirty again right now with
the regenerated — i.e. re-inverted — WGSL, and the three-line warning comment
`1dd2ebd` added ("Do not wrap it in `1.0 - ...`") is gone with it. That comment is
itself the proof of the anti-pattern: Slang's WGSL backend emits zero comments, so
a `//` in a generated file can only be a hand-edit awaiting deletion.

Two mechanisms let this recur, both tasked below:

- **The convention is inverted between the two renderers under near-identical
  names.** `Resources/ShadersSlang/common/cascaded_shadow.slang:10-11`'s
  `calc_cascaded_shadow` returns OCCLUSION ("0 = fully lit"), and its two C++ call
  sites (`rasterizer.slang:82`, `deferred.slang:133`) correctly write
  `color *= 1.0 - shadow * intensity`. `forward.slang:192`'s `shadow_factor`
  returns VISIBILITY (line 207 early-returns `1.0` for out-of-cascade;
  `SampleCmpLevelZero` returns 1 where the fragment passes the depth compare), so
  `1.0 - shadow` at `:365` is wrong. Copying the C++ idiom onto the Rust helper is
  the whole bug, and the names give no warning.
- **Nothing automated can catch it.** `BuildIntegrity.CheckedInWgslIsNotOlderThanItsSlangSource`
  (`buildIntegritySuite.cpp:807`) compares mtimes only — a regenerate makes the
  destination *newer*, so it passes while carrying the regression (and after a
  fresh CI checkout mtimes carry no staleness information at all). On the Rust
  side, `shadow_darkens_plane_under_cube` *would* catch a full inversion (it
  demands `lit_plane > 1000` neutral-bright pixels, which the inversion destroys),
  but `rust_ubuntu24_04.yml` runs `cargo_test.sh` in a GPU-less container where
  every `headless.rs` test hits its `GpuContext::new_headless()` early return and
  prints `SKIP`.

**Second root cause found this pass: the GPU-culling compute path culls every
primitive, and the reason is in the shader.** The `- [b]` "Make `gpu_culling_enabled`
actually cull" entry above closed with "needs a fresh look at the compute-shader
culling path itself"; this is that look. `Resources/ShadersSlang/gpu_cull/gpu_cull.slang:31-44`
projects **only the AABB centre** and keeps the primitive when
`depth <= sampledDepth`. For any opaque object that rendered itself, the centre of
its own bounding box sits *behind* its own front face, so its projected depth is
strictly greater than the depth the object wrote at that texel and the test fails
— every visible primitive is culled. That is exactly the reported symptom
(`two_visible_cubes_are_both_drawn_with_gpu_culling` returning `(0, 2)`). The
hardware-query path it was told to mirror does not have this property because it
rasterizes the *box* (front face nearer than the object) with `LessEqual`.
Lines 37-38 add a second over-cull: a primitive whose centre falls off-screen or
behind the near plane is culled outright, so a large ground plane disappears.

Verified this pass and folded into the tasks rather than tasked separately: the
`alpha_modes_blend_and_mask` failure that batch IV recorded as "isolated, needs its
own investigation" is very likely the same inversion — its classifier requires
`g > 140 && r > 70 && b > 60` over a "bright white plane", and zeroing direct light
everywhere outside a cascade takes the frame to ambient/IBL only, which reads as
the reported 0 composited pixels. Task 1 states the expected outcome rather than
assuming it. Also confirmed: `parseCpu`'s texture extraction (`GltfLoader.cpp:438-441`)
is NOT a second alignment hazard — `textureID` is assigned from
`textureImages.size()` at push time, so a dropped image shifts nothing.

Candidates found but NOT tasked this cycle (checked, then rejected with a reason —
do not re-propose without new evidence): **`histogram.wgsl` being the one
hand-written WGSL** — AGENTS.md § Shaders already records why (Slang has no
`InterlockedAdd` on `RWStructuredBuffer` for WGSL) and it is Rust-only, with no
C++ counterpart to share; **README/`shader-build-pipeline.md`/`webgpu-renderer-roadmap.md`
shader drift** that batch IV deferred — re-read this pass and all three are now
correct (`shader-build-pipeline.md:49` explicitly frames the GLSL tree as a
"Historical note", the roadmap marks the naga route retired); **the `Y`-flip
divergence** between `cascaded_shadow.slang:29` (`proj.xy * 0.5 + 0.5`) and
`forward.slang:206` (`0.5 - proj.y * 0.5`) — the two renderers build their cascade
matrices differently and both shadow goldens pass, so there is no evidence either
is wrong; **`CheckedInWgslIsNotOlderThanItsSlangSource`'s missing mtime tolerance**
— a real sharp edge, but task 3's content gate makes the mtime test's vacuousness
moot rather than needing its own fix.

## 2026-08-01 batch VII — planner (multi-model texture offsets, unsynchronized clouds pass, cascade-count drift)

The actionable queue was empty again when this batch was written (the eight `- [b]`
entries are the only checkboxes left in the file; batches IV, V and VI are drained).
Every claim below was read out of the tree this pass, with the `file:line` given.

**The headline finding: `create_object_description_buffer` indexes a PER-MESH array
with a PER-MODEL counter.** `Scene::add_model` (`Scene.cpp:150-153`) pushes *one
object description per mesh*, flattened across models — its own comment says so
("objectIndex (the per-draw push constant) indexes this list"). But
`VulkanRenderer.cpp:1222-1226` walks that list with `i < scene->getModelCount()`
and accumulates `scene->getTextureCount(i)`, i.e. it treats slot `i` as *model* `i`.
Its comment ("descriptions are pushed one per model, in model order") is stale and
is what makes the bug read as correct. Task 1 fixes it. It is latent with the
default single-model scene (every offset is legitimately 0) and bites the moment a
multi-mesh model is followed by a second model — exactly the case
`GoldenRender.SecondModelLoadsAndRenders` exercises without asserting the index
arithmetic (the "Multi-object rendering works" note above already flags that gap).

**Second finding: the clouds compute pass has no synchronization with its consumer.**
`VulkanRenderer.cpp:871` dispatches the cloud compute shader, which writes
`cloudOutputTexture` as a storage image; `VulkanRenderer.cpp:926` runs the post pass,
whose descriptor set samples that same image (`VulkanRenderer.cpp:1155-1159`).
Between them there is nothing: `grep pipelineBarrier Src/.../VulkanRenderer.cpp`
returns **zero hits**, and PostStage's only subpass dependency is
`eColorAttachmentOutput -> eColorAttachmentOutput` from `VK_SUBPASS_EXTERNAL`
(`PostStage.cpp:250-256`), which orders the swapchain colour attachment and says
nothing about a compute write. The comment at `VulkanRenderer.cpp:917-920` is about
that swapchain barrier specifically and does not cover this hazard. Tasks 2 and 3.

**Third finding: `MAX_CASCADES` has a third, ungated copy.** The pin added by
`766bd89c` compares `host_device_shared_vars.hpp` against `scene_types.slang` only
(`buildIntegritySuite.cpp:626-667`), but
`Resources/ShadersSlang/rasterizer/shadows/shadow_map.slang` declares its own
`static const int NUM_CASCADES = 3` (`:8`) *and* a literal `ConstantBuffer<float4x4[3]>`
(`:10`), neither of which the gate can see. Task 4.

Candidates found but NOT tasked this cycle (checked, then rejected with a reason —
do not re-propose without new evidence): **`gpu_cull.slang:79`'s comment calling the
maximum sampled depth "nearest-to-camera"** — the comment is backwards for a standard
0-near depth buffer, but the *math* (`cull iff aabbNear > maxSampled`) is the correct
conservative HZB test, so this is a one-word comment nit not worth a task on its own;
**`Texture::generateMipMaps`'s `[[maybe_unused]] uint32_t in_mip_levels`**
(`Texture.cpp:299`, loop reads the member `mip_levels` at `:323` instead) — real dead
parameter, but the single call site passes exactly that member, so it is cosmetic;
**the `# cascades` GUI slider advertising 1..8 against `MAX_CASCADES` 3** — already
recorded as a deliberate cosmetic lie in batch III's prose above, with the engine-side
clamp making it harmless; **checking in the generated SPIR-V under a content gate like
the WGSL one** — unnecessary, `git ls-files Resources/ShadersSlang/build` returns
nothing, so no SPIR-V or WGSL artifact is tracked in this repo and there is no
hand-edit surface to guard.

### Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate`)

## 2026-08-01 batch VIII — planner (refactor: unread shadow stats + dead accessors, skybox view plumbing, descriptor-write duplication)

The actionable queue was empty again when this batch was written (the eight `- [b]`
entries are the only checkboxes left in the file; batches IV–VII are drained).
Every claim below was read out of the tree this pass, with the `file:line` given.

**The headline finding: the shadow pass computes cull statistics every frame and
throws them away, while the GUI already has the panel that would show them.**
`CascadedShadowMap::recordCommands` resets `castersDrawn`/`castersConsidered`
(`CascadedShadowMap.cpp:433-434`) and increments them per mesh (`:505`, `:515`).
Their only readers are `getCastersDrawn()`/`getCastersConsidered()`
(`CascadedShadowMap.ixx:112-113`), and a grep of `Src/` + `Test/` for both names
returns **only those six lines** — nothing calls the getters. Meanwhile
`GUI.cpp:247-252` already renders a "Frustum culling" checkbox plus
`Culled: %u (%.1f%%)` from `guiRendererSharedVars.visibility.meshes_drawn` /
`meshes_total`, which `VulkanRenderer.cpp:1008-1011` publishes from the *raster*
path. So the identical diagnostic exists for one pass and is computed-then-discarded
for the other. The Rust renderer already resolved this the same way
(`c2c2fe4` "cache the shadow-caster render bundle across frames, make caster stats
honest").

**Same finding, second half: the comment describing the culling toggle is wrong.**
`GUIRendererSharedVars.ixx:59-64` documents `frustum_culling_enabled` as the switch
you flip "when something goes missing" to learn "whether culling is the cause", and
signs off with "Never applied to the shadow pass; see Rasterizer::recordCommands."
The flag is read at exactly one site (`VulkanRenderer.cpp:931-934`, building the
optional camera frustum) — but `CascadedShadowMap::recordCommands` culls
*unconditionally* against the cascade frusta (`:507-514`), gated by nothing. The
sentence is therefore true about the flag and false about the pass, and it reads as
the latter. A missing *shadow* cannot be diagnosed with that checkbox today, which is
precisely the failure mode the comment claims the checkbox exists to rule out.

**Second finding: the skybox framebuffer views are built twice, and half of each
build is a vector of N copies of one value.** `VulkanRenderer.cpp:133-138` and
`:695-700` are the same six lines verbatim (init and `recreateSwapChain`). Both fill
`skyboxDepthViews[i] = postStage.getDepthBufferImageView()` in a loop — the same
handle for every element, because `PostStage` owns a single depth buffer
(`PostStage.ixx:31`, one `depthBufferImage`). `SkyBox::createFramebuffers` /
`recreateFrameResources` (`SkyBox.ixx:29,33`) take both as
`const std::vector<vk::ImageView>&` plus a redundant `size_t count` that is always
`imageViews.size()`. This is the same shape commit `39486995` already collapsed for
`CascadedShadowMap` ("per-cascade framebuffer vectors to scalars"), and the same
`std::span` conversion `7cff9cc0`/`6e4d0204` applied to the record path.

**Third finding: `DescriptorSetGroup`'s four write helpers share a copy-pasted
prologue and epilogue.** `writeBuffer`, `writeImage`, `writeImageArray` and
`writeAccelerationStructure` (`DescriptorSetGroup.cpp:171,197,223,249`) each open
with the identical `checkWritePreconditions` + `findBinding` + null-check pair, then
each fill the identical five `vk::WriteDescriptorSet` fields
(`dstSet`/`dstBinding`/`dstArrayElement`/`descriptorType`/`descriptorCount`) and
close with the identical `updateDescriptorSets(1, &descriptor_write, 0, nullptr)`.
Only the payload pointer differs (`pBufferInfo` / `pImageInfo` / `pNext`), plus one
`descriptorCount` that comes from the binding rather than being 1. This was recorded
as a deferred candidate in batch IV ("the 4× duplicated `DescriptorSetGroup` write
prologue") and never tasked.

**Build-system fact all three tasks depend on** (re-checked this pass, unchanged
since batch V): `kataglyphis_collect_module_interfaces`
(`cmake/KataglyphisCMakeHelpers.cmake:10-13`) globs `*.ixx` **without**
`CONFIGURE_DEPENDS`, and the recorded module-BMI skew hazard ("Incremental container
builds can ship ODR-broken binaries") applies to any edited module interface. All
three tasks below edit a `.ixx`, so all three want `-FreshContainer`.

Candidates found but NOT tasked this cycle (checked, then rejected or deferred with a
reason — do not re-propose without new evidence): **`Raytracing`'s duplicated
swapchain pointer** — `init` stores the parameter in `this->vulkanSwapChain`
(`Raytracing.cpp:31`), `recordCommands` takes a *second* `VulkanSwapChain*`
(`:47`) and picks between them with `swapchain ? swapchain : this->vulkanSwapChain`
(`:114`), while the sole call site passes `&vulkanSwapChain`
(`VulkanRenderer.cpp:1046`) — the same object `init` was given, never null. Both the
member (`Raytracing.ixx:42`) and the parameter carry a stale `[[maybe_unused]]`
though line 114 uses them. Real dead generality worth a task, deferred purely for
queue discipline (three-task cap) — **pick this up next cycle**;
**`PathTracing.ixx:48`'s `[[maybe_unused]] vk::PushConstantRange pc_range{..., 0, 0}`**
— zero-sized, grep-confirmed unreferenced (the `pc_ranges` hits are `Raytracing`'s
separate, live member), but it is a single line with no behaviour attached, so it
rides along with the Raytracing task rather than earning its own; **the `vkCheck()`
sweep over the 52 `createX` + `ASSERT_VULKAN` + `.value` sites** — still the same
whole-codebase, ~20-file commit batch II deferred, still wanting a deliberate moment
rather than an executor session; **`Scene::getObjectDescriptions()` returning
`std::vector` by value** — re-verified this pass, still one caller on the
scene-change path, still not worth a `std::span`, exactly as batch V concluded.

### C++ Vulkan engine

## 2026-08-01 batch IX — planner (dead RT/PT plumbing, a queue-family transfer that never happens, cross-frame clouds WAR, shadow-cull toggle, occlusion strobe)

The actionable queue was empty again when this batch was written (the eight `- [b]`
entries are the only checkboxes left in the file; batches IV–VIII are drained).
Every claim below was read out of the tree this pass, with the `file:line` given.

**Task 1 is the item batch VIII explicitly deferred with "pick this up next
cycle"** — re-verified unchanged: `Raytracing::init` stores its swapchain
parameter (`Raytracing.cpp:31`), `recordCommands` takes a *second* one
(`:47`, still marked `[[maybe_unused]]`) and picks between them at `:114`, while
the sole call site (`VulkanRenderer.cpp:1050`) passes `&vulkanSwapChain` — the
same object `init` was handed. `PathTracing` is NOT the same shape: its
`vulkanSwapChain` parameter is genuinely read (`PathTracing.cpp:121`); only the
zero-sized `pc_range` member (`PathTracing.ixx:48`) is dead, and it rides along.

**Second finding: PathTracing's two image barriers declare a queue-family
ownership transfer that no second barrier ever completes.**
`PathTracing.cpp:79-80` sets `srcQueueFamilyIndex = graphics_family`,
`dstQueueFamilyIndex = compute_family`, and `:105-106` sets compute→compute —
but this command buffer is recorded into the frame's graphics command buffer and
submitted to the graphics queue, and the `dispatch` that consumes the image
follows on that same queue. A release with no paired acquire leaves the contents
undefined for the acquiring queue. It is invisible today only because
`getQueueFamilies` breaks as soon as one family satisfies graphics + compute +
present (`VulkanDevice.cpp:683`), which on every desktop AMD/NVIDIA part is
family 0 for all three, so `src == dst` and Vulkan ignores both fields. Every
other barrier in the engine already uses `VK_QUEUE_FAMILY_IGNORED`
(`Raytracing.cpp:100-101`, `VulkanRenderer.cpp:881-882`); PathTracing is the
outlier. Task 2.

**Third finding: the cross-frame WAR on `cloudOutputTexture` is still open, and
the code says so.** `VulkanRenderer.cpp:898-906` documents it precisely
("Follow-up, not fixed here") and nothing has closed it since:
`cloudOutputTexture` is a single image (`Clouds.cpp:286-289`, one texture, not
per-frame-in-flight), `MAX_FRAME_DRAWS == 3`, so frame N's post-pass read and
frame N+1's compute write are ordered by nothing. The barrier at `:878-896`
closes only the same-frame RAW. Since `5ccaca80` there is now a one-command way
to check it (`Scripts/Windows/Run-SyncValidation.ps1`), and the fix is cheap and
deterministic either way — a barrier orders against *all* previously submitted
commands on the queue, including earlier submissions, so a WAR barrier before
the dispatch closes it without duplicating the image. Task 3. Note
`clouds_enabled` defaults to **false** (`GUISceneSharedVars.ixx:45`), so any
verification run must turn clouds on.

**Fourth finding: batch VIII fixed the comment about the culling toggle but not
the gap it described.** `GUIRendererSharedVars.ixx:69-73` now correctly states
that `frustum_culling_enabled` gates only the raster paths and that "the cascade
pass culls unconditionally … this switch does not turn that off". That is now an
accurate description of a real diagnostic hole: the checkbox exists so that "when
something goes missing, [you can tell] in one click whether culling is the cause"
(`:66-68`), and a missing *shadow* is exactly the case it cannot answer.
`CascadedShadowMap::recordCommands` culls against the cascade frusta at
`CascadedShadowMap.cpp:507-514` gated by nothing. The caster counters the same
batch published (`VulkanRenderer.cpp:918-919`) make the fix assertable. Task 4.

**Fifth finding (Rust): the hardware-occlusion path still strobes any primitive
the camera is inside.** This is item #1 of the 2026-07-22 Rust survey, re-checked
and still open — `OcclusionQueries::record` takes only `view_proj`
(`occlusion.rs:227-233`) and there is no eye position anywhere in the module, so
nothing can force-visible a box containing the camera. `cull_mode: None`
(`:163-165`) is present and its comment names the situation, but back faces still
fail `LessEqual` against the geometry's own depth while front faces are
near-plane clipped, so the query reads 0. The consumer is live:
`forward.rs:2049-2050` skips the draw for `!self.occlusion.visible(i)`, which
empties the depth there, which makes the next query pass — a ~30 Hz flicker on
the object filling the screen. Task 5.

Verified-and-closed while surveying (do NOT re-propose these; they are done, and
the 2026-07-22 Rust survey text above is stale for them): #2 `set_instances`
widening `scene_bounds` — `recompute_scene_bounds` now exists
(`forward.rs:2581-2597`) and is called from all three mutation sites (`:1000`,
`:1034`, `:2578`); #3 `world_center` metric drift — every site is the AABB centre
now, including upload (`:1605`); #5 instanced normals — `instance_cofactor_0`
is emitted and applied (`forward.wgsl:129-131`, `:181`); #10
`KHR_materials_unlit` — loaded (`gltf_loader.rs:600`), plumbed
(`forward.rs:1834`) and demoed (`wasm_demo.rs:48`); #11 anisotropy —
`anisotropy_for` plus a unit test (`forward.rs:3319`, `:3676`); #14 zero-strength
bloom/SSAO — gated at `forward.rs:2192`, `:2196`.

Candidates found but NOT tasked this cycle (checked, then rejected or deferred
with a reason — do not re-propose without new evidence): **`Texture::generateMipMaps`'s
dead linear-blit branch** — `spdlog::error("...does not support linear blitting!")`
at `Texture.cpp:303-306` can no longer fire, because the only caller already
consulted `supportsLinearBlit` and forced `mip_levels = 1` on failure
(`:129-132`), so the function is never entered in that case; it also re-queries
`getFormatProperties` for nothing and carries the dead `[[maybe_unused]] uint32_t
in_mip_levels` parameter (`:299`, the loop reads the member at `:323`). Real dead
code, deferred purely for the five-task cap — **pick this up next cycle**;
**`Model::addSampler` creating one `vk::Sampler` per texture** (`Model.cpp:66-82`)
— identical except `maxLod`, so a Sponza-class model burns 33 sampler objects
against a `maxSamplerAllocationCount` floor of 4000; they *are* destroyed
(`:32-35`), so this is headroom, not a leak, and not worth a task yet;
**`docs/gpu-golden-testing.md:121` claiming the suite is 21 tests** when
`goldenRenderSuite.cpp` now holds 28 — one-number doc drift, fold it into
whichever task next touches that file; **`VulkanDevice::getQueueFamilies`
breaking as soon as all three indices are set** (`:683`), so a dedicated
async-compute or transfer family can never be selected — deliberate given nothing
uses a second queue today, and task 2 removes the only code that pretended
otherwise.

## 2026-08-01 batch X — planner (an over-subscribed uniform slot in the Rust forward shader, dead mip branch, script + baseline coverage)

The actionable queue was empty again (the eight `- [b]` entries are the only
checkboxes left; batches IV–IX are drained). Every claim below was read out of
the tree this pass with the `file:line` given.

**The headline finding is task 1, and it is worse than a naming problem.**
`Resources/ShadersSlang/forward/forward.slang:22` documents its uniform as
`float4 cascade_splits; // x,y: splits, z: count, w: tile_h` — five values
asked of a `vec4`. The shader then reads `.z` twice, for two different things:
as the **cascade count** at `:199-200` (`int count = int(frame.cascade_splits.z);
if (cascade > count - 1) cascade = count - 1;`) and as the **tile-grid width**
at `:270` (`uint tileW = uint(frame.cascade_splits.z);`). The Rust host resolves
the conflict in favour of tiles — `forward.rs:1736-1741` writes
`[cascade_splits[0], cascade_splits[1], tile_counts.0 as f32, tile_counts.1 as f32]`
— so the `CASCADE_COUNT as f32` that `update_cascades` puts in
`self.cascade_splits[2]` (`:2619`, and the initializer at `:930`) is written and
then thrown away, and the cascade clamp is fed a tile width.

That alone is only latently wrong (a 256px target gives `tileW = 16`, and
`min(cascade, 15)` never bites). The part that bites is the ordering:
`tile_counts` starts `(0, 0)` (`:910`) and is assigned at `:1809`, **after** the
frame-uniform write at `:1743-1747`. So the value the shader sees is always one
frame stale, and on the first frame it is zero — making the clamp compute
`count - 1 == -1` and force `cascade = -1` for every fragment in the frame.
**Every headless test renders exactly one frame** (`render_to_pixels`), so the
entire Rust GPU suite runs in that state; the shadow tests pass only because a
negative array index degrades to cascade 0's matrix, which is the right cascade
for a near-camera fixture. The same stale frame recurs after every resize.

Task 2 (the still-red `alpha_modes_blend_and_mask`) is sequenced **after** task
1 on purpose: a blend quad shadowed by the wrong cascade is a live hypothesis
for why its green pixels miss the `g > 140` threshold, and re-diagnosing it
before task 1 lands risks chasing a symptom of task 1.

Task 3 is the item batch IX deferred with "pick this up next cycle", re-verified
unchanged. Task 5 folds in the `docs/gpu-golden-testing.md` count drift batch IX
asked to fold into "whichever task next touches that file", and adds a second
instrument disagreement found this pass: the BACKLOG "Measured baseline" table
says `BM_ComputeCascadeData/1` is 142 ns and `/3` is 324 ns, while
`Test/perf/baselines/win-9070xt-32core.json` — captured the same day
(`context.date` 2026-07-31T19:36, same host, same 32 cores) — records 372.82 ns
and 1012.79 ns. 2.6x and 3.1x apart is not desktop noise.

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **`ForwardRenderer::light_space_matrix`**
(`forward.rs:2655-2680`) is `#[allow(dead_code)]` and a near-duplicate of
`light_matrix_for` — real dead code, but it is inside the file task 1 edits, so
delete it there rather than spending a task on it; **the Rust cascade fit has no
texel snapping** (`light_matrix_for:2634-2652` anchors `look_at_rh` at a
continuously moving `center`, which is exactly the shimmer the C++ engine fixed
on 2026-07-31 in `CascadedShadowMapMath.cpp:153-204`) — a genuine cross-renderer
parity gap and the C++ code is a ready-made template, but it needs its own cycle
and its own CPU oracle, so it is recorded here rather than half-specified;
**`Model::addSampler` burning one `vk::Sampler` per texture** (`Model.cpp:66-82`)
— re-checked, still headroom rather than a leak, still not worth a task.

### C++ Vulkan engine

### Build / scripts

### Docs

## 2026-08-01 batch XI — planner (refactor: dead parameters, Clouds setup triplication, forward.rs geometry extraction)

The actionable queue was empty again (the eight `- [b]` entries are the only
checkboxes left in the file; batches IV–X are drained). Every claim below was
read out of the tree this pass with the `file:line` given.

**Re-verified as genuinely closed while surveying — do NOT re-propose these.**
`ForwardRenderer::light_space_matrix` is gone (batch X task 1 deleted it as
instructed; the only `allow(dead_code)` left in the crate are four
`cfg_attr(target_arch = "wasm32", ...)` on `forward.rs:233-244`, which are
platform gating, not dead code). `update_cascades` no longer writes a cascade
count into `cascade_splits[2]` — `forward.rs:2620-2622` now carries the comment
"z, w (tile_w, tile_h) are supplied by the per-frame uniform write in `render`".
Batch II's three items all landed: `Src/GraphicsEngineVulkan/scene/ModelFileKind.ixx`
now owns the extension dispatch behind `isGltfModelPath`/`isSupportedModelPath`
(with tests at `cameraSceneConfigSuite.cpp:206-215`), zero
`const std::vector<vk::DescriptorSetLayout> &` parameters remain anywhere in
`Src/`, and `CascadedShadowMap`'s dead `getFramebuffers()` is gone.
`docs/gpu-golden-testing.md:121-122` is **not** drifted despite the suite now
holding 30 `TEST(GoldenRender, ...)`: 30 − 1 `DISABLED_` − 3 excluded by the
documented filter + 2 `Integration` tests = the 28 it claims. That arithmetic is
non-obvious, so do not "fix" the number.

**Task 1's four items are the last `[[maybe_unused]]` markers in `Src/`** —
grepped this pass, there are exactly four and every one of them marks something
that should simply be deleted rather than annotated. `Texture::generateMipMaps`
kept `vk::Format image_format` when commit `c80e7503` removed its two *other*
dead parameters (`physical_device`, `in_mip_levels`); the format is now
`[[maybe_unused]]` in the definition (`Texture.cpp:293`) but plain in the
declaration (`Texture.ixx:96`), which is the tell that it was overlooked rather
than kept deliberately.

**Task 2: `Clouds` writes the same two sequences three and two times.** The
"create a storage `Texture` + view + sampler, then transition it to `eGeneral`
through a one-shot command buffer" block appears at `Clouds.cpp:33-41`,
`:44-53` and — byte-for-byte identical to the second — at `:286-293` inside
`recreateFrameResources`. The "load SPIR-V, fill a `vk::PipelineShaderStageCreateInfo`,
create a pipeline layout, create the compute pipeline, destroy the module"
sequence appears at `:174-200` and `:203-228`. Together that is ~50 lines of
duplication in a 355-line file, and the `recreateFrameResources` copy is exactly
the shape that drifts: a format or usage flag changed in `createTextures` and not
there produces a resize-only bug. **This is GPU-verifiable** — `clouds_enabled`
defaults to `false` (`GUISceneSharedVars.ixx:45`), but two golden tests turn it
on: `GoldenRender.CloudsAcrossManyFramesDoesNotLoseTheDevice`
(`goldenRenderSuite.cpp:2665`, 30 frames past the frames-in-flight cycle) and
the all-maximum case of `GuiInputSweepNeverCrashesOrLosesTheDevice` (`:2514`).

**Task 3: `forward.rs` is 3740 lines and ~255 of them are pure-CPU geometry that
touches no wgpu type at all.** `Frustum` (`:3007-3068`), `normal_matrix_of`
(`:3070`), `aabb_contains_point` (`:3086`), `instanced_bounds` (`:3103`),
`widen_bounds_for_skin` (`:3128`), `primitive_world_aabb` (`:3150`),
`primitive_local_aabb` (`:3189`), `transform_aabb` (`:3220`) and
`compute_world_bounds` (`:3241`) form one contiguous, self-contained block whose
only imports are `glam`, `crate::scene::*` and two symbols from
`crate::render::occlusion`. They are already documented as a group by
`docs/renderer-bounds-invariant.md` ("Consumers — everything that reads
bounds"), which names them by symbol and never by file, so the move costs no doc
churn. The precedent is `c85ef931`, which pulled animation sampling out of the
same file into `render/animation.rs`; `render/mod.rs` is the one-line registry.

Candidates found but NOT tasked this cycle (checked, then rejected or deferred
with a reason — do not re-propose without new evidence):
**`Clouds::recordComputeCommands` taking `std::span<const vk::DescriptorSet>`
and reading only `[0]`** (`Clouds.cpp:264-271`) — it looks like dead generality
but is not: `VulkanRenderer.cpp:861` builds one `std::array<vk::DescriptorSet, 1>`
and hands the same span to the rasterizer, deferred, skybox and post record
paths, so Clouds is following the house convention, not deviating from it;
**`Model::addSampler` burning one `vk::Sampler` per texture** (`Model.cpp:66-82`)
— re-checked a third time, still headroom against a 4000 floor rather than a
leak, still not worth a task (stop re-checking it);
**the `vkCheck()` sweep over the 52 `auto r = createX(...); ASSERT_VULKAN(...);
h = r.value;` sites** that batch II deferred — re-confirmed as still the right
call to defer: it is a ~20-file single commit that wants a deliberate moment;
**the Rust cascade fit still has no texel snapping** (`light_matrix_for:2637-2660`
anchors `look_at_rh` at a continuously moving `center`, the shimmer the C++ side
fixed in `CascadedShadowMapMath.cpp`) — a real cross-renderer parity gap, but it
is a behaviour change needing its own CPU oracle, not a refactor, and task 3
deliberately does **not** touch it (`light_matrix_for` stays in `forward.rs`
because it reads `self.light_dir_ambient`).

## 2026-08-01 batch XII — planner (Rust cascade fitting, texture-upload round trips, shadow comparison sampler, a drifted comparison script)

The actionable queue was empty again — batches IV–XI are drained and the only
checkboxes left in the file were `- [b]`. Every `file:line` below was read out
of the tree this pass.

**One `- [b]` closed while surveying, not re-proposed.** The
`gpu_culling_enabled` entry above is now `- [x]`: its blocker ("needs a fresh
look at the compute-shader culling path itself") was removed by `1594a4a0`,
whose commit message records both
`an_occluded_primitive_is_skipped_with_gpu_culling` and
`two_visible_cubes_are_both_drawn_with_gpu_culling` as passing. Blocker gone
*and* work done, so the entry was pruned to a stub per this file's own rules.
Two stale prose claims were corrected in place for the same reason (packaging
in CI, the headless GPU-timings JSON) — see those bullets.

**Tasks 1 and 2 both edit Rust cascade fitting and task 2 builds on task 1's
extraction. Do them in order.** They are split rather than merged because they
fix different things: task 1 is a correctness bug (the near cascades cover a
depth band nothing is ever assigned to), task 2 is a stability gap (shimmer).

**Task 1 is a real bug, and the C++ engine already proves the invariant that
catches it.** `update_cascades` (`forward.rs:2610-2635`) sets
`cascade_splits = [near_radius * 2.0, mid_radius * 2.0, 0.0, 0.0]` — values
derived from the *scene* radius — while `forward.slang:201-202` selects a
cascade by comparing them against `In.viewDepth`, which
`forward.slang:151` defines as `distance(worldPos, camera_position)`: an
**eye distance**, not a scene-radius fraction. The two are unrelated
quantities. Cascade 0's box is centred at `camera.target.lerp(camera.eye(),
0.15)` with half-extent `near_radius`, i.e. it covers eye distances roughly
`0.85 * camera.radius ± near_radius`, but it is *selected* for eye distances
`< 2 * near_radius`. For any framing where the camera sits more than about
`3.5 * near_radius` from its target — the normal case — those two intervals do
not overlap at all, so every fragment routed to cascade 0 lands outside
cascade 0's ortho box, `forward.slang:210` sees `uv` outside `[0,1]` and
`return 1.0` (fully lit). The failure is silent by construction: shadows still
appear, because everything far enough out falls through to cascade 2, which
does cover the scene. The symptom is "the near cascades never do anything and
shadow resolution is always the coarsest cascade's", not "shadows are
missing" — which is exactly why no existing test catches it. The C++ engine
has the right oracle already: `CascadedShadowMapUnit.EachCascadeCoversItsOwnFrustumSlice`
(`cascadedShadowMapSuite.cpp:109`) and `StabilizedCascadesStillCoverTheirSlice`
(`:430`) assert precisely this property against `computeCascadeData`, and the
C++ splits come from real view-depth distances (`CascadeData::splitDepth =
cascadeSplits[i + 1]`). Port the assertion, then make it pass.

**Task 2 is the parity gap batch XI deliberately deferred**, re-proposed here
with the new evidence batch XI said it needed: task 1 gives it a pure-CPU home
(`render/cascades.rs`) and a working oracle to extend, which is the "own CPU
oracle" that deferral was waiting on. `light_matrix_for`
(`forward.rs:2637-2655`) anchors `Mat4::look_at_rh` at a continuously moving
`center`, so the ortho box translates *and* the light basis rotates every
frame; the C++ side fixed exactly this in `CascadedShadowMapMath.cpp:154-201`
and documents the three necessary ingredients in the comment there.

**Task 3: `Texture::uploadRgba` costs three full GPU round trips per texture.**
`CommandBufferManager::endAndSubmitCommandBuffer`
(`CommandBufferManager.cpp:57-125`) creates a fence, submits, and
`waitForFences(..., UINT64_MAX)` before returning — every call is a
synchronous stall. `uploadRgba` (`Texture.cpp:151-172`) makes three of them
back to back: `transitionImageLayout`, `vulkanBufferManager.copyImageBuffer`,
then either `generateMipMaps` (which begins/ends its own,
`Texture.cpp:288-378`) or a second transition. They are all recordable into
one command buffer — `VulkanImage::transitionImageLayout` already has a
command-buffer overload (wave 5), and `copyImageBuffer` only needs one.
Related and in the same file: every `Texture` **owns** a `VulkanBufferManager`
(`Texture.ixx:100`) purely to reach `copyImageBuffer`, which touches no member
state; the manager's whole reason to exist is its reusable staging buffer, and
the texture path ignores it and creates a fresh `VulkanBuffer` per texture
(`Texture.cpp:134-138`). One texture-heavy model pays both costs per texture.

**Task 4: the cascade shadow map is bilinear-filtered and then compared, which
is not PCF.** `CascadedShadowMap.cpp:57` builds the sampler with
`vk::Filter::eLinear`, and `common/cascaded_shadow.slang` declares it as a
plain `Sampler2DArray`, calls `.Sample()`, and does the depth compare in the
shader (`occluded += (currentDepth - bias) > mapDepth ? 1.0 : 0.0`). Averaging
four *depths* and then thresholding the average is the textbook wrong order:
at any silhouette the blended depth is a value no surface actually has, so the
comparison answers about geometry that does not exist. Hardware comparison
samplers exist for this, and the Rust renderer already uses one —
`forward.slang:215` calls `shadowMap.SampleCmpLevelZero(shadowSampler, ...)`.
This is a cross-renderer parity fix with a free quality win (the 2x2 hardware
PCF comes with the comparison sampler).

**Task 5 is a defect in a checked-in tool, found by reading it against the two
enums it claims to track.** `Scripts/Compare-RendererTimings.ps1:34` defaults
`$RustExpectedPasses` to
`@('Forward','ShadowCascades','Ssao','Bloom','Histogram','Post')`, and
`Assert-PassesExist` (`:49-63`) sets `exitCode = 1` for any expected pass the
JSON does not carry. **There is no `Post` pass on the Rust side.**
`TimedPass::name()` (`gpu_timing.rs:83-93`) emits `ShadowCascades`, `Forward`,
`OcclusionCull`, `Bloom`, `Ssao`, `Histogram`, `ExposureReduce`, `Tonemap` —
the tonemap pass is called `Tonemap`. So the script fails on every run for a
pass that cannot exist, and simultaneously never notices that
`ExposureReduce`, `Tonemap` and `OcclusionCull` are unchecked. The C++ list
(`:32`) does currently match `GPU_TIMED_PASS_EXPORT_NAMES`
(`GUIRendererSharedVars.ixx:26-32`), but nothing keeps it that way. This is
the same class the repo already gates elsewhere (`a63edf10` self-enforcing
Windows CI test filter, `28887db1` / `41b76ab8` generated-WGSL gates), and the
Pester precedent is `Scripts/Windows/tests/Compare-PerfBaseline.Tests.ps1`.

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **`Frustum::from_view_proj` in `render/bounds.rs:21-35`
does not normalize its planes** while the C++ `extractFrustumPlanes`
(`Frustum.cpp:26-53`) does — but normalization divides by a strictly positive
length, so the sign test both sides perform is unaffected, and the C++
normalization exists only so its `1e-4` epsilon is scale-independent (the Rust
version has no epsilon). Cosmetic, not a defect. **`intersects_aabb_as_caster`
(`bounds.rs:52`) has no production caller** — grepped the whole crate, only
its own test — but this is deliberate and documented: `forward.rs:1877-1884`
disabled per-cascade caster culling when the shadow draw list became a cached
`RenderBundle`, and re-enabling it needs culling-aware bundle invalidation,
which that comment explicitly defers as a design decision. Do not delete the
function and do not re-enable culling without that design. **`GltfLoader.cpp:299`
says "Placeholder for now"** about flat normals that `:352-372` actually
computes — one stale sentence, too small to task; fold it into the next edit
that touches the file. **The `Model::addSampler` per-texture sampler** — batch
XI's instruction to stop re-checking this still stands; it was not re-checked.

## 2026-08-01 batch XIII — planner (a divide-by-zero on the sync error path, a rotted comparison script, a CI GPU probe that always says yes, the Rust cascade oracle batch XII asked for, dead stateless-class members)

Batch XII is fully drained (`50c0304b`, `add6f17c`, `d277fa99`, `9bfe48b4`,
`092f166d`); every checkbox left in the file was `- [b]`. Every `file:line`
below was read out of the tree this pass.

**Task 1 is a real crash on an untested error path, traced end to end.**
`FrameSync::advanceFrame()` (`FrameSync.ixx:121`) is
`current_frame = (current_frame + 1) % frame_sync_count`, and `create()` sets
`frame_sync_count = 0` on *any* creation failure (`:65` for the
semaphore/fence loop, `:82` for the render-finished loop). `drawFrame` does
guard on it — but at `VulkanRenderer.cpp:442`, **before** it can re-enter
`recreateSwapChain()` at `:450`/`:484`, and `recreateSwapChain()` ends with
`createSynchronization()` → `frameSync.create(...)`
(`VulkanRenderer.cpp:1484-1487`). So the count can go to 0 *after* the only
guard. Walk the second failure path (`FrameSync.ixx:82`, render-finished
semaphore): `image_available` and `in_flight_fences` are already fully
populated, so `currentFrame() >= inFlightFenceCount()` (`:455`) passes and
`!inFlightFence()` (`:461`) passes; `render_finished_by_image` was resized to
`imageCount` so `renderFinishedCount()` (`:501`) passes, and
`!renderFinishedSemaphore(image_index)` only fires if the *acquired* image is
one of the ones that failed. Acquire image 0 when image 2 was the failure and
the frame runs all the way to `advanceFrame()` at `:613` → `% 0`. The class
should defend its own invariant instead of relying on a caller check that
runs too early.

**Task 2: `Scripts/Compare-RendererPixels.ps1` cannot complete a run.**
`$bmp.Dispose()` is called and the *next* statement passes `$bmp.Width` /
`$bmp.Height` as arguments (`:204-205`, and again at `:223-224`).
`System.Drawing.Image.Width` on a disposed bitmap throws, and the script sets
`$ErrorActionPreference = 'Stop'` at `:37`, so Phase 3 dies the moment a C++
frame exists — the exact path the script is for. Two more defects in the same
file, all of the "a green run means nothing" class this repo keeps
rediscovering: `-ValidationOnly` enumerates `*.png` at `:92` and then
*ignores* that list, testing `Test-Path $cppPng` where `$cppPng` is the
hard-coded `cpp-vulkan.png` (`:103`) while the capture at `:129-135` actually
writes `cpp-vulkan-<suffix>.png` — so validation mode always finds nothing;
and with neither renderer producing a frame, `$exitCode` stays 0 and the
script prints `PIXEL COMPARISON PASSED`. It is also the only one of the three
`Scripts/Compare-*.ps1` tools with no Pester suite. Sibling precedent for
everything this needs: `Scripts/Windows/tests/Compare-RendererTimings.Tests.ps1`
(child-process invocation + fixture files, Pester 3.4.0 dash-less syntax).

**Task 3 is the half of batch XII task 1 that did not land.** That entry said
"The C++ engine has the right oracle already:
`CascadedShadowMapUnit.EachCascadeCoversItsOwnFrustumSlice`
(`cascadedShadowMapSuite.cpp:109`) … Port the assertion, then make it pass."
`add6f17c`/`50c0304b` shipped the fit and five good tests in
`render/cascades.rs:161-344` — splits growing with the camera, the
scene-radius fallback, finiteness, texel snapping, box-size invariance — but
**not the coverage assertion**, and the gap it was meant to catch is still
there. `fit_cascades` (`cascades.rs:91-96`) gives cascade 0 a sphere centred
at `camera.target.lerp(camera.eye(), 0.15)` with radius `0.35 * d`, i.e. it
covers eye distances `[0.5d, 1.2d]`, while `forward.slang:198-201` selects it
for every eye distance below `splits[0] = 0.7d`. Everything closer than
`0.5d` is routed to a cascade whose box does not contain it, and
`forward.slang:212` answers that with `return 1.0` — **fully lit, silently**.
Do NOT "fix" this by re-deriving the boxes from the camera frustum: the
module doc at `cascades.rs:26-34` records that an earlier attempt at exactly
that regressed `shadow_darkens_plane_under_cube` to zero shadowed pixels.

**Task 4: the Windows CI GPU probe asks a question that is not about the
GPU.** `Windows.yml:315` runs `commitTestSuite.exe --gtest_list_tests |
Select-String "GoldenRender"` and prints `GPU_AVAILABLE` if it matches — but
gtest lists registered test names without touching an adapter, so it matches
on every runner that has the binary, and `:318` prints `GPU_NOT_AVAILABLE`
unconditionally right after. `$test -match 'GPU_AVAILABLE'` at `:320` filters
the array and gets a non-empty result, so `$hasGpu` is always true and `:323`
resolves `$validationFlag` to `''` — the step runs
`Compare-RendererTimings.ps1` **without** `-ValidationOnly` on a GPU-less
hosted runner, which is the opposite of what its own comment (`:296-301`)
says it does. Same family as the swapchain screenshot that reads black while
the session is locked: a probe that cannot say no.

**Task 5 (refactor): `CommandBufferManager` has no state, and six objects
carry one anyway.** The class (`CommandBufferManager.ixx:8-22`) has exactly
two members, both `static`, and an empty `private:` section. Six types hold a
zero-information instance of it: `ASManager.ixx:52`, `Texture.ixx:97`,
`DeferredRasterizer.ixx:74`, `VulkanRenderer.ixx:204`,
`VulkanBufferManager.ixx:69`, `Rasterizer.ixx:78` — plus
`DeferredRasterizer.cpp:39` (`commandBufferManager = CommandBufferManager();`,
a no-op assignment) and `Texture.cpp:31`/`:47`, which move the dead member
through `Texture`'s move constructor and move assignment. `d277fa99` deleted
the per-`Texture` `VulkanBufferManager` for the same reason and left this one
behind. Every call site in the tree already uses the static form
(`Texture.cpp:152`, `VulkanBufferManager.cpp:21`, `:34`, `:62`, `:72`).

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **`Texture::createImage` never assigns
`this->mip_levels`** (`Texture.cpp:203-218`), so a texture built that way
reports `getMipLevel() == 0` and `createTextureSampler` gives it
`maxLod = 0.0`. Harmless today — every one of the seven external callers
passes `in_mip_levels = 1` (`DeferredRasterizer.cpp:81`/`:100`,
`PostStage.cpp:174`, `Rasterizer.cpp:301`/`:320`, `Clouds.cpp:33`,
`CascadedShadowMap.cpp:53`, `SkyBox.cpp:112`, `VulkanRenderer.cpp:1269`) and
`maxLod = 0` is correct for one mip — but it is a trap for the first mipped
image created without going through `uploadRgba`. Fold the one-line
assignment into the next change that touches `Texture`. **The PCF radius
slider is 1..20** (`GUI.cpp:199`) and `cascaded_shadow.slang:39-52` loops
`(2r+1)^2` comparison taps, so the maximum is 1681 taps per shadowed
fragment; now that the comparison sampler adds hardware 2x2 filtering the
useful range is much smaller, but narrowing a user-facing slider is an owner
decision, not an agent's. **`Frustum::from_view_proj` plane normalization**
and **`intersects_aabb_as_caster` having no production caller** — batch XII
already rejected both with reasons; they still hold.

## 2026-08-01 batch XIV — planner (refactor: a copy-pasted shader-stage block in six pipelines, forward.rs's texture helpers, model-loading doc drift)

Batch XIII is fully drained (`62e56684`, `24baeeef`, `7b8c3ccd`, `ea464598`,
`3fd2f217`); every checkbox left in the file was `- [b]`. Every `file:line`
below was read out of the tree this pass.

**Task 1: six pipelines each hand-write the same vertex+fragment stage
block, and one of them leaks it across 48 lines.** The sequence "load two
`.spv` into `vk::ShaderModule`, fill two `vk::PipelineShaderStageCreateInfo`
with `pName = "main"`, pack them into a two-element array, build the
pipeline, destroy both modules" appears verbatim at `Rasterizer.cpp:354-370`
+ `:402-403`, `PostStage.cpp:286-300` + `:319-320`,
`DeferredRasterizer.cpp:312-317` + `:348-349` and `:352-357` + `:380-381`,
`SkyBox.cpp:332-345` + `:385-386`, and
`CascadedShadowMap.cpp:335-350` + `:435-436`. Two tells that this is
copy-paste rather than convergent style: `CascadedShadowMap.cpp:350` names
its local `std::array skyStages` — a name that only makes sense in
`SkyBox.cpp:345`, where it was copied from — and `Rasterizer` puts 32 lines
of vertex-input and layout setup between loading its modules and destroying
them, so any early return added in that window leaks two `vk::ShaderModule`.
`Raytracing.cpp` (four modules), `PathTracing.cpp` (one compute module) and
`Clouds.cpp` (compute, already deduped by `9aac4cb2`) are deliberately out of
scope: different stage counts, different shapes.

**Task 2: `forward.rs` carries ~280 lines of texture/sampler code that no
other renderer file needs and no test can currently reach.** `create_sampler`
(`:2977`), `anisotropy_for` (`:3014`), `srgb_to_linear` (`:3022`),
`linear_to_srgb` (`:3031`), `generate_mips` (`:3042`),
`compressed_wgpu_format` (`:3081`), `create_compressed_texture` (`:3098`),
`create_material_texture` (`:3149`), `create_depth_texture` (`:3217`) and
`create_hdr_texture` (`:3236`) form one contiguous block; grepping the crate
confirms **not one of them is referenced outside `forward.rs`** (the four
other `create_sampler` hits in `bloom.rs`/`ibl.rs`/`tonemap.rs` are
`wgpu::Device::create_sampler`, a different function). Three of them —
`srgb_to_linear`, `linear_to_srgb`, `generate_mips` — are pure CPU with zero
wgpu types and **zero tests**, even though `generate_mips` is the box filter
every material texture's whole mip chain is built from and the sRGB pair is
what keeps colour textures from being averaged in the wrong space. The
precedents are `c85ef931` (`render/animation.rs`) and `0378178e`
(`render/bounds.rs`); `render/mod.rs` is the one-line registry.

**Task 3: `docs/model-loading.md` describes a `MeshRange` that no longer
exists and points at a refactor that already landed.** `:71` lists the struct
as six fields, but `scene/MeshRange.ixx:18-30` has a seventh — `bool
doubleSided`, which `uploadParsed` forwards to `add_new_mesh` so glTF
`doubleSided` materials disable back-face culling per mesh. The doc never
mentions that the struct moved into its own `kataglyphis.vulkan.mesh_range`
module (commit `fba308d7`, wave 6) or that the re-basing arithmetic it warns
about at `:111-115` now lives in exactly one shared function,
`sliceMeshRange`, with a `MeshSlice` return type and its own test suite
(`Test/commit/VulkanEngine/meshRangeSliceSuite.cpp`). Worse, `:118-120` still
tells the reader that deduplicating "that loop" is a queued refactor and "a
natural moment" — that IS `fba308d7`. `:36-37` also predates
`scene/ModelFileKind.ixx`, which now owns the extension dispatch that both
`AsyncModelParse.ixx:55` and `Scene.cpp:36` call through
(`isGltfModelPath`).

## 2026-08-01 batch XV — planner (tiled-lighting binning is upside down, an "empty tile" fallback that defeats tiling, an unpinned light-packing layout, a CI fuzz list that cannot say no, stale golden-suite counts)

Batch XIV is fully drained (`3fd2f217`, `f97712f4`, `e76a860f`, `7b38ac3e`);
every checkbox left in the file was `- [b]`. Every `file:line` below was read
out of the tree this pass.

**Tasks 1 and 2 are both in the tiled punctual-lighting path and task 2 is
unsafe before task 1 lands. Do them in order.** Task 1 is a correctness bug
(tiles are indexed upside down); task 2 is the fallback that has been hiding
it, and removing that fallback while the binning is still mirrored would turn
a hidden bug into a visible one.

**Task 1: the CPU tile binning and the shader disagree about which way is
up.** `tile_rect_for_light` projects a light's candidate points and converts
clip space to a screen fraction with `let uv = ndc * 0.5 + 0.5;`
(`tile_grid.rs:79`), then derives tile rows from `uv.y`
(`tile_grid.rs:94`/`:96`). NDC y is **up** (`+1` = top of the viewport), so
`uv.y == 1.0` lands in the LAST tile row. The consumer indexes the same grid
from the fragment's framebuffer position: `uint tileY = uint(fragCoord.y) /
16u` (`forward.slang:293`, `In.svPosition` = `SV_Position` at `:393`/`:120`,
which is `@builtin(position)` in the emitted WGSL — y **down**, `0` = top),
and `uint tileIndex = ty * tileW + tx` (`:300`). So the row a light is written
to is the mirror of the row that reads it; x is fine (NDC `-1` and
`fragCoord.x == 0` are both the left edge). The observable defect needs two
lights at different screen heights: a top-half tile that receives some
*other* light's mirrored entry gets `count > 0`, takes the per-tile list, and
therefore **misses the light actually over it**. None of the six existing
tests in `tile_grid.rs:200-357` can catch this — every one of them puts its
light at the origin dead-centre, uses a directional light, or asserts only
"at least one tile is lit", all of which are symmetric under a vertical flip.

**Task 2: an empty tile iterates every light in the scene, which is the exact
opposite of what tiling is for.** `forward.slang:305` reads
`if (count <= 0 || tileW == 0u) tileCount = totalLights;`. `tileW == 0` is the
documented "the grid was never built" fallback (`tile_grid.rs:8-9` says so),
but `count <= 0` is a *different* fact — "the CPU binned no light into this
tile" — and the shader conflates the two. With `MAX_PUNCTUAL_LIGHTS = 256`
(`forward.rs:32`), every tile the lights do not reach pays a 256-iteration
loop instead of zero, so a scene gets *slower* the more empty screen it has.
It is not a correctness bug today (iterating all lights is the physically
correct answer; binning is the optimisation), which is why nothing has caught
it — and it is also why it silently absorbs task 1's mirroring. Before the
`count <= 0` branch can go, the binning has to actually be conservative, and
today it is not in one case: `tile_rect_for_light` builds its screen AABB from
only those of the seven candidate points with `clip.w > 0.0`
(`tile_grid.rs:74-84`), so a light sphere **straddling the near plane** — centre
in front, part of the sphere behind, or vice versa — gets a footprint computed
from a subset of its extent and can under-cover. That must be fixed in the
same change or task 2 will delete light.

**Task 3: `pack_punctual_lights` is a 4×`vec4`-per-light host/device contract
with zero tests.** `forward.rs:188-222` writes `[pos.xyz, kind]`,
`[colour*intensity, range]`, `[dir.xyz, cos_inner]`, `[cos_outer, 0, 0, 0]`;
`forward.slang:311-314` reads exactly those four rows back and decodes `kind`
with float thresholds (`kind > 2.5` → directional, `kind > 1.5` → spot,
`forward.slang:317`/`:334`). Nothing pins the two together, and the packer is
pure CPU — no wgpu types, no adapter — so there is no reason for it to be
untested. It also feeds task 1's binner, which reads `packed[base][3]` as
`kind` and `packed[base + 1][3]` as `range` (`tile_grid.rs:130-135`), a third
consumer of the same unwritten contract. The extraction precedents are
`c85ef931` (`render/animation.rs`), `0378178e` (`render/bounds.rs`) and
`e76a860f` (`render/texture.rs`); `render/mod.rs` is the one-line registry.

**Task 4: the Windows CI fuzz step re-hand-maintains the list that
`EveryCpuSuiteIsInTheWindowsCiFilter` already exists to stop being
hand-maintained.** `Windows.yml:277` hard-codes
`@('obj_parsing_fuzz_test','gltf_parsing_fuzz_test','scene_config_fuzz_test','shader_file_reader_fuzz_test','texture_loading_fuzz_test')`
while the targets themselves are declared by seven
`kataglyphis_add_fuzz_test(...)` calls in `Test/fuzz/CMakeLists.txt:106-164` —
a new fuzz target is silently not run in CI, which is the same drift class
`BuildIntegrity.EveryCpuSuiteIsInTheWindowsCiFilter`
(`buildIntegritySuite.cpp:682`) was written for. Worse, `Windows.yml:279`
reads `if (-not (Test-Path $exe)) { Write-Host ('missing ' + ...); continue }`
— a fuzz target that stops being built makes the step print one line and pass.
The step only runs after a successful `clangcl-debug` build in which fuzzing
mode is on, so a missing executable is a build regression, not an
environment difference: this is another instance of the probe-that-cannot-say-no
that `ea464598` just removed from the GPU check three steps below it.

**Task 5: the golden-suite counts are stale in four places and one of them
contradicts a line 75 lines further down the same file.**
`docs/gpu-golden-testing.md:46-47` says "As of 2026-07-23 the baseline is 19
`GoldenRender` + 2 `Integration` tests, ~44 s", while `:121-122` of the same
document records a 2026-08-01 run of "28 tests from 2 test suites" with three
excluded by name. The tree says 30 `TEST(GoldenRender...)` in
`goldenRenderSuite.cpp` (one of them `DISABLED_DumpsFrameToPng`) plus
`Integration.RenderModesSelectableInGui` and `Integration.VulkanEngine`, i.e.
**29 runnable `GoldenRender` + 2 `Integration` = 31**, and 31 − 3 excluded = the
28 the later line reports. So the later line is right and the earlier one is
stale by ten tests. BACKLOG repeats the old figure three more times
(`:258` "22 GPU tests", `:372` "all 19-21 GPU tests passing", `:626`/`:639`
"22 golden"). Separately, BACKLOG `:709-714` still calls
`an_occluded_primitive_is_skipped_with_gpu_culling` and
`two_visible_cubes_are_both_drawn_with_gpu_culling` "Still red" while
`:3001-3007` in the same file records both as passing under `1594a4a0` and
prunes the entry that owned them.

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **`CascadedShadowMap::recordCommands` binds the wrong
set on an unreachable path** (`CascadedShadowMap.cpp:483-491`) — when
`descriptorSets` is empty it binds `descriptorSet` (built from the *light*
layout) at set 0, which is layout-incompatible with the pipeline layout's set
0; the only caller (`VulkanRenderer.cpp:956`) always passes a non-empty span,
so this is dead defensive code, not a live defect. **`generate_mips` underflows
on a zero-dimension texture** (`texture.rs:86-88`, `pw - 1` on `pw == 0`) —
unreachable, every `CpuTexture` comes from a successful decode. **`Texture::
createImage` never assigns `this->mip_levels`** — batch XIII already rejected
this with reasons that still hold; fold the one-liner into the next change
that touches `Texture`.

### Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate`)

- [b] **Stop an empty tile from iterating every light in the scene** —
  **step 1 done (2026-08-01), step 2 blocked on a deeper, newly-found gap.**
  `tile_rect_for_light` (`tile_grid.rs:47`) now returns the whole grid when a
  light's seven candidate points straddle the near plane (some `clip.w > 0`,
  some `<= 0`), same as the directional fallback — the case the original task
  described. New test `a_light_straddling_the_near_plane_covers_the_whole_grid`
  pins it (camera `(0,0,5)`, point light at `(0,0,4.9)` range `2.0`, every tile
  must list it).

  **The shader's `count <= 0 || tileW == 0u` fallback in
  `forward.slang`'s `punctual_lighting` was deliberately NOT removed.** Only
  the safe half of step 3 (moving the `tileLightGrid` read so it does not fire
  on the `tileW == 0` path) was applied; the `count <= 0` → iterate-everything
  branch stays exactly as before.

  **Why: the "genuinely conservative" premise was wrong in a second, larger
  way the original task text did not anticipate.** A completeness test
  (`build_tile_light_grid_is_complete_for_random_lights` — sample many points
  on each light's actual sphere surface, project each independently of
  `tile_rect_for_light`, assert the grid lists the light for every tile a
  sample lands in) was written per the task's step 4 and **failed on lights
  with no near-plane involvement at all**. Root cause, confirmed by direct
  calculation: the 7-candidate AABB (`pos` plus `pos ± range * axis`) is not
  a bound on the sphere's true screen-space silhouette even when every
  candidate is in front of the camera. The silhouette's tangent points
  combine a lateral *and* a depth-axis offset — no single axis-aligned
  candidate sits there — and the true extent exceeds the naive one by a
  factor of `D / sqrt(D² - r²)` (`D` = eye-to-centre distance, `r` = range),
  worse as range grows relative to distance. Repro: light at
  `pos=(1.13,-2.07,0.77)`, `range=0.74` (`D≈4.84`, so `r/D≈0.15`, a small
  ratio) still under-covered by a full tile row. This is documented in
  `tile_grid.rs`'s `tile_rect_for_light` doc comment now.

  A uniform scalar inflation using that factor was considered and rejected:
  it is only exactly correct when the light lies on the view axis (the
  aligned 2D case verified above) — for a light off to the side, on an
  asymmetric or non-square-aspect projection, the correct bound is
  direction-dependent and needs a projection-matrix-aware formula (see Mara &
  McGuire, "2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D
  Sphere", 2013). Shipping the scalar-inflation shortcut would have looked
  fixed while still under-covering in untested configurations — the same
  "green because untested" trap this task's own context note warned about,
  one level deeper. `build_tile_light_grid_is_complete_for_random_lights` was
  therefore **not** added to the tree (it would either fail or need range
  bounds tight enough to be dishonest about what it tests); the straddle test
  is the only one that survived.

  **Next step for whoever picks this up**: implement a real
  projection-matrix-aware sphere screen-bound (Mara & McGuire or equivalent),
  re-add the completeness test with unconstrained random ranges, confirm it
  passes, *then* remove the shader's `count <= 0` fallback. Until that
  lands, the fallback is load-bearing correctness, not just a performance
  safety net — do not remove it as a drive-by.

  Verified: `cargo test --workspace --locked` from
  `ExternalLib/Kataglyphis-RustProjectTemplate` (all `tile_grid` tests green;
  the pre-existing `auto_exposure_brightens_a_dark_scene_over_successive_frames`
  and the 11 `ibl` test failures reproduce identically on unmodified `develop`,
  confirmed by stashing this change — unrelated headless-GPU-adapter issues in
  this environment, not caused by this change). Shaders recompiled
  (`compile-slang-shaders.ps1`); `clangcl-debug` container build green;
  `commitTestSuite.exe --gtest_filter='BuildIntegrity.*'` shows the
  shader-staleness/no-hand-edit gates (`CompiledShadersAreNotOlderThanTheirSources`,
  `CheckedInWgslIsNotOlderThanItsSlangSource`, `CheckedInWgslHasNoHandEdits`)
  green. Two unrelated, pre-existing `BuildIntegrity` failures surfaced in the
  same run and are NOT caused by this change (confirmed by file scope —
  neither touches anything this change edited):
  `NoShaderRedeclaresTheCascadeCount` (`forward.slang:15`'s
  `CASCADE_COUNT`, untouched by this change) and `VulkanCreationResultsAreChecked`
  (`Clouds.cpp:227`, `ShaderHelper.cpp:38` — files this change never opened).
  Worth their own task. The full `commitTestSuite.exe` GPU golden suite was
  also tried and failed broadly with "No synchronization frames available" —
  looks like GPU/session contention from the three build containers already
  running in this environment (matches the known "swapchain reads black
  while the session is locked" papercut), not a rendering regression; the
  task's own build instructions only called for the `BuildIntegrity.*` filter,
  which is green.

## 2026-08-01 batch XVI — planner (two RED BuildIntegrity gates that fail Windows CI, a ray-tracing SBT that reads past its miss region, an unpinned manifest pair, unpinned Rust/Slang constants)

Batch XV is fully drained (`8b28543c`, `cb669253`, `3a0a49c7`, `c9e920b7`,
`e2767bb1`); every checkbox left in the file before this batch was `- [b]`.
Every `file:line` below was read out of the tree this pass.

**Tasks 1 and 2 are the priority: both are `BuildIntegrity` tests that are RED
right now, and `'BuildIntegrity.*'` is in the Windows CI filter
(`Windows.yml:210`), so any `[build-win]` commit fails the Test step.** Batch
XV's executor notes spotted both as "unrelated, pre-existing failures … worth
their own task" and correctly did not chase them mid-task; this is that task.
Neither needs a GPU — they are pure file-scanning CPU gtests.

**Task 1 is a stale line-number allowlist, not a real unchecked result.**
`VulkanCreationResultsAreChecked` (`buildIntegritySuite.cpp:1386`) exempts three
`.value` reads via `kCheckedResultAllowlist` (`:1312-1327`), keyed on
`{relative file, 1-based line}`. Two of the three line numbers have drifted out
from under it: the allowlist says `vulkan_base/ShaderHelper.cpp` **37** but the
`.value` read is now at **38**, and `scene/atmospheric_effects/clouds/Clouds.cpp`
**249** but the read is now at **227**. (`vulkan_base/VulkanDevice.cpp` 231 is
still correct.) Both written justifications still hold verbatim — I read both
sites: `ShaderHelper.cpp:30-38` still does `spdlog::critical` + `std::abort()`
above the read, and `Clouds.cpp:222-227` still logs and `return`s on failure
rather than aborting. So the test is reporting a bookkeeping drift as a
correctness gap, which is exactly the failure mode that trains people to ignore
a red gate. A line-number key cannot survive an edit anywhere above it in the
file; the fix is to anchor the exemption to something that moves *with* the
code.

**Task 2 is a scope bug: the gate scans a shader the Vulkan engine does not
consume.** `NoShaderRedeclaresTheCascadeCount` (`:1094`) walks every `.slang`
under `Resources/ShadersSlang/` outside `build/`, and fails on any
`static const int <name containing "cascade"> = <MAX_CASCADES>`. It fires on
`Resources/ShadersSlang/forward/forward.slang:15`
(`static const int CASCADE_COUNT = 3;`). That constant is **not** a stale copy of
the C++ engine's `MAX_CASCADES` — `forward/forward.slang` is the Rust/WebGPU
forward shader, WGSL-emit only (`compile-slang-shaders.ps1` /
`.sh` list it with `Targets = wgsl` only), and no `forward.*.spv` appears
anywhere in the engine's SPIR-V load list (checked: the engine loads
`rasterizer.*`, `deferred.*`, `shadow_map.*`, `skybox.*`, `post.*`,
`raytrace.*`, `shadow.rmiss.*`, `path_tracing.*`, `compute/*` and nothing else).
Its owner is `crates/webgpu_renderer/src/render/forward.rs:43`
(`pub const CASCADE_COUNT: usize = 3;`), and the shader's own comment at `:13`
says so. The two constants are equal at 3 by coincidence of both renderers
choosing three cascades. Raising the C++ `MAX_CASCADES` cannot make
`forward.slang` stale, which is the only thing this gate is for. Widening the
gate to "any cascade-ish 3 anywhere" made it fire on a shader it has no claim
over.

**Task 3 is a real Vulkan spec violation in the ray-tracing shader binding
table, latent on this GPU and wrong on a device with
`shaderGroupHandleAlignment > shaderGroupHandleSize`.** Three coupled defects in
`Raytracing.cpp`, all verified by reading `createSBT` (`:289-334`),
`recordCommands` (`:45-73`), the group table (`:210-250`) and the two
`TraceRay` call sites. The pipeline has four groups — 0 raygen, **1 and 2 both
miss** (`rmiss_main`, `shadow_rmiss_main`), 3 triangles-hit — and
`raytrace.rchit.slang:98-103` traces its shadow ray with **miss index 1**
(`0u, 0u, 1u,   // sbt offset, stride, miss index (shadow miss)`), while
`raytrace.rgen.slang:36-43` uses miss index 0. But `recordCommands` sets
`miss_region.stride = handle_size_aligned; miss_region.size =
handle_size_aligned;` (`:65-66`) — a region declared to hold **one** record,
from which the shader reads record **1**. Separately, `createSBT` copies handles
out of the `getRayTracingShaderGroupHandlesKHR` blob at the *aligned* stride
(`handles.data() + handle_size_aligned`, `handles.data() + handle_size_aligned *
3`, `:332-333`), but that call writes handles **tightly packed at
`shaderGroupHandleSize`** — the aligned stride is the SBT's *device-side* record
stride, not the blob's. And the two miss handles are then written back-to-back
at `handle_size` stride (`memcpy(mapped_miss, …, handle_size * 2)`) into a
buffer allocated `2 * handle_size` (`:319`), while the device reads them at
`handle_size_aligned` stride. All three are identities when
`shaderGroupHandleAlignment == shaderGroupHandleSize` (32/32 on the RX 9070 XT
and most desktop drivers), which is why every RT golden test passes today.

**Task 4 pins a manifest pair that has not drifted yet but has no gate**, and
task 5 pins two Rust/Slang constants that do. Both follow precedents already in
the tree (`SlangWgslPatchTablesAgree`, `EveryFuzzTargetIsInTheWindowsCiFuzzList`,
and batch XV's `render/lights.rs` layout pin).

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **the Windows and Linux Slang manifests currently
disagree on `histogram/histogram.slang`** — false alarm, that row is *commented
out* in `compile-slang-shaders.ps1:109` (the hand-written `histogram.wgsl`
documented in AGENTS.md); the 49 live rows match exactly, which is why task 4 is
written as a preventive gate and not a bug fix. Note the same comment trap for
whoever implements it. **The GUI `# cascades` slider advertising 1..8** — stale
backlog claim, `GUI.cpp:196` already reads
`ImGui::SliderInt("# cascades", …, 1, MAX_CASCADES)`. **`Clouds::
recordComputeCommands` indexes `descriptorSets[0]` with no emptiness check**
(`Clouds.cpp:246`) — same dead-defensive-path class batch XV already rejected for
`CascadedShadowMap::recordCommands`; the only caller always passes a non-empty
span.

### C++ Vulkan engine

- [b] **(M) Fix the ray-tracing SBT: the miss region declares one record while the shadow ray reads record 1, and handle offsets assume the device stride** — a spec violation that is invisible only because `shaderGroupHandleAlignment == shaderGroupHandleSize` on this GPU.

  **Status (2026-08-01): code fix implemented and committed, GPU verification blocked.**
  All steps 1-5 below are done: `sbt_handle_source_offset`/`sbt_record_offset`
  extracted into `MemoryHelper.hpp`, `createSBT` fixed to source at
  packed `handle_size` and write records at aligned `handle_size_aligned`
  offsets, `miss_region.size` widened to `2 * handle_size_aligned`, and the
  two new `MemoryHelperUnit` CPU tests (plus a no-op-on-this-hardware
  regression test) added and green in the `clangcl-debug` container build.
  **GPU-verify could not be run**: the host RDP session (`jonas`, session 2)
  is currently disconnected, and *every* `GoldenRender.*` test fails
  identically — including `GoldenRender.RendersNonBlankFrame`, which has
  nothing to do with ray tracing — with `No synchronization frames
  available; skipping draw frame.` from frame 0 and `frame.empty() == true`.
  This reproduces with or without this change (confirmed via baseline test)
  and persists across repeated runs and a 20s wait, so it is not the known
  `PathTracingAccumulatesAndConverges` device-lost issue recovering slowly —
  it looks like swapchain presentation is unavailable to a disconnected
  session on this box. `tscon 2 /dest:console` failed with access denied
  (needs admin). Next executor with a live/attached GPU session: rerun
  `.\commitTestSuite.exe --gtest_filter='GoldenRender.Raytrac*:GoldenRender.*Raytraced*:GoldenRender.PathTracing*:GoldenRender.AddedModelAppearsInPathTracing'`
  and `Run-SyncValidation.ps1` from repo root to close this out, then delete
  this entry.

  **Files to read:**
  - `Src/GraphicsEngineVulkan/renderer/Raytracing.cpp:289-334` (`createSBT`),
    `:45-73` (the three `vk::StridedDeviceAddressRegionKHR` set-ups in
    `recordCommands`), `:210-250` (the four-group table: 0 raygen, 1 miss,
    2 shadow miss, 3 triangles-hit).
  - `Resources/ShadersSlang/raytracing/raytrace.rchit.slang:98-103` — the shadow
    `TraceRay` with **miss index 1**; `raytrace.rgen.slang:36-43` — miss index 0.
  - `Src/GraphicsEngineVulkan/common/MemoryHelper.hpp:6` — `align_up`, and
    `Test/commit/VulkanEngine/memoryHelperSuite.cpp` for the existing pure-CPU
    test pattern to extend.
  - `Src/GraphicsEngineVulkan/vulkan_base/VulkanDevice.cpp:560-580` — where
    `shaderGroupBaseAlignment`/`shaderGroupHandleAlignment` are already read and
    logged at device selection.

  **Steps:**
  1. **Source offsets.** `getRayTracingShaderGroupHandlesKHR` writes handles
     tightly packed at `shaderGroupHandleSize`. Change the three `memcpy` sources
     at `:331-333` to index by `handle_size`, not `handle_size_aligned`
     (group 0 at `0`, groups 1-2 at `handle_size`/`2 * handle_size`, group 3 at
     `3 * handle_size`).
  2. **Destination layout.** Size the three SBT buffers in units of
     `handle_size_aligned`, not `handle_size` (`:317-322`): raygen
     `handle_size_aligned`, miss `2 * handle_size_aligned`, hit
     `handle_size_aligned`. Copy each record into its own aligned slot — the two
     miss handles must land at offsets `0` and `handle_size_aligned`, replacing
     the single `handle_size * 2` block copy.
  3. **Region sizes.** In `recordCommands`, set
     `miss_region.size = 2 * handle_size_aligned` (stride stays
     `handle_size_aligned`) so the declared region covers the miss index 1 the
     closest-hit shader actually uses. Leave raygen and hit at
     `size == stride == handle_size_aligned` (the spec requires
     `size == stride` for the raygen region specifically).
  4. **Make the arithmetic testable.** Extract the two offset formulas as
     `constexpr` free functions next to `align_up` in
     `common/MemoryHelper.hpp` — e.g.
     `sbt_handle_source_offset(groupIndex, handleSize)` and
     `sbt_record_offset(recordIndex, handleSizeAligned)` — and call them from
     `createSBT` instead of inline multiplications. Keep them header-only and
     free of Vulkan types so the headless test binary can use them.
  5. Add a short comment at `createSBT` recording *why* the two strides differ
     (blob is packed, device records are aligned) — that distinction is the whole
     bug and the next reader will otherwise re-collapse them.

  **Test:** Add to `Test/commit/VulkanEngine/memoryHelperSuite.cpp` (suite name
  is **`MemoryHelperUnit`**, already in the Windows CI filter at
  `Windows.yml:239` — no workflow edit needed):
  `MemoryHelperUnit.SbtSourceOffsetsArePackedAtHandleSize` and
  `MemoryHelperUnit.SbtRecordOffsetsUseTheAlignedStride`, both exercising the
  **`handleSize = 32`, `alignment = 64`** case (so `aligned = 64 != 32`) where
  every current formula is wrong: source offsets must be `0, 32, 64, 96`, record
  offsets `0, 64`. Also assert the degenerate `alignment == handleSize` case
  still yields today's values, so the change is provably a no-op on this
  hardware. These are pure CPU. If you do add a new suite name,
  `BuildIntegrity.EveryCpuSuiteIsInTheWindowsCiFilter` will tell you to register
  it.

  **Build:** `clangcl-debug` for the CPU tests. **Then GPU-verify on the host RX
  9070 XT** — this is a render-path change and the CPU tests cannot see it:
  `.\commitTestSuite.exe --gtest_filter='GoldenRender.Raytrac*:GoldenRender.*Raytraced*:GoldenRender.PathTracing*:GoldenRender.AddedModelAppearsInPathTracing'`
  from the repo root (`RaytracedWorldFollowsTheModelTransform`,
  `RaytracingFrameSkipsTheRasterPass`, `RaytracedLargeMeshDoesNotLoseTheDevice`,
  and the five path-tracing tests). A wrong handle offset shows up as the wrong
  shader being invoked — expect visibly broken RT output, not a subtle shift, if
  step 1 or 2 is done wrong. Also run
  `pwsh -ExecutionPolicy Bypass -File .\Scripts\Windows\Run-SyncValidation.ps1`;
  the validation layers check SBT region sizes against the miss indices used, so
  step 3 is directly observable there.

  **Context:** See `docs/gpu-golden-testing.md` for the host verification loop
  and `[[host-gpu-golden-verification]]`. This is deliberately three fixes in one
  change because they are one bug — the code conflates the packed *source* stride
  with the aligned *device record* stride, and fixing either half alone leaves the
  SBT self-inconsistent. Do not "simplify" by asserting
  `shaderGroupHandleAlignment == shaderGroupHandleSize`: that is exactly the
  portability assumption being removed, and the RX 9070 XT would never fail the
  assert.

### Build / scripts

### Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate`)

## 2026-08-01 batch XVII — planner (a dangling pointer in swapchain creation, an incomplete blit-capability gate, three coverage gaps)

The actionable queue was empty again: the only checkboxes left in the whole file
are `- [b]` entries, and batch XVI is drained (`e8b1db52`, `f1a67217`,
`74aaee23`, `302faa90`, `ee4abb24`). Every `file:line` below was read out of the
tree this pass.

**The headline finding is task 1 and it is undefined behaviour, not a style
nit.** `VulkanSwapChain::initVulkanContext` declares
`uint32_t queue_family_indices[]` **inside** the
`if (indices.graphics_family != indices.presentation_family)` block
(`VulkanSwapChain.cpp:95-96`), stores its address into
`swap_chain_create_info.pQueueFamilyIndices` at `:100`, and the block closes at
`:101`. `createSwapchainKHR` reads that pointer at `:117` — sixteen lines after
the array's lifetime ended. It is invisible here for one reason only: on this
box `graphics_family == presentation_family`, so the branch never executes and
the `else` at `:102-105` writes `nullptr`/count 0. On a device that presents
from a different queue family the driver reads freed stack. This is the same
shape as the SBT stride bug batch XVI fixed — correct-by-luck on one machine,
wrong by spec everywhere.

Two smaller real gaps in the same pass. `Texture.cpp`'s `supportsLinearBlit`
(`:59-64`) tests only `eSampledImageFilterLinear`, but `vkCmdBlitImage` also
requires `eBlitSrc` on the source and `eBlitDst` on the destination — and since
`c80e7503` deleted the (then-unreachable) in-loop fallback,
`supportsLinearBlit` is the **only** gate left in front of
`generateMipMaps`, so an incomplete check now means no check at all on a device
that lacks those bits. And `AsyncModelParse` — the engine's only threaded class,
driven every time the GUI loads a model — has **zero** tests, on a platform with
no ThreadSanitizer (AGENTS.md § "There is no Windows ThreadSanitizer").

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **extracting the eight bind-group-layout and four
pipeline-layout descriptors out of `ForwardRenderer::new`**
(`forward.rs:431-745`, ~330 lines of inline descriptor construction) — this is
pure code motion, which batch XIII already rejected for the same file and the
same reason (the animation samplers); **`map_format` collapsing `*_SRGB_BLOCK`
onto the Unorm `CompressedFormat`** is not itself a bug — `compressed_wgpu_format`
(`render/texture.rs:111-125`) picks the sRGB wgpu variant from the *usage* flag,
which is what glTF specifies; what is missing is a diagnostic when the container
disagrees, which is task 5 and deliberately not a behaviour change;
**`docs/gpu-golden-testing.md:47-48` vs `:122-123`** currently agree
(30 defined − 1 `DISABLED_` = 29 runnable, + 2 `Integration` = 31 total, minus
the 3 named exclusions = 28) — task 4 is therefore a preventive gate, not a
drift fix.

### C++ Vulkan engine

### Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate`)

## 2026-08-01 batch XVIII — planner (refactor: a "kept in lockstep" guard that is written twice, untested texture-slot flattening, a depth-aspect rule with four hand-rolled copies)

Batch XVII is fully drained (`ad836844`, `0705aa7f`, `2df83793`, `8e0d8294`,
`73192584`); every checkbox left in the file before this batch was `- [b]`.
Every `file:line` below was read out of the tree this pass.

**All three tasks are refactors and all three are pure-CPU verifiable.** Tasks 1
and 2 both edit `renderer/VulkanRenderer.cpp`, so do them in either order but
not concurrently.

**Task 1 is duplication the code itself already flags as dangerous.**
`VulkanRenderer.ixx:174-180` documents `raytracingOwnsFrame` as "the single
predicate deciding whether `recordRaytracingOrPathTracing` will actually
dispatch this frame … if they do [disagree], a frame renders nothing" — and yet
`recordRaytracingOrPathTracing` does **not call it**: it re-writes the same
three-term condition inline at `VulkanRenderer.cpp:1082-1083`, with its own
copy of the rationale comment at `:1078-1081` and a second copy at `:1064-1068`.
Separately, the three descriptor writes that bind TLAS / output image /
accumulation image exist verbatim twice — `:798-801` (per-image, from the frame
path) and `:1368-1371` (inside the all-images loop). `update_raytracing_
descriptor_set` is also declared **public** (`VulkanRenderer.ixx:81`) while its
only caller in the tree is `VulkanRenderer.cpp:547`, inside the class.

**Task 2 is the largest block of untested pure logic left in the renderer.**
`updateTexturesInSharedRenderDescriptorSet` (`VulkanRenderer.cpp:1518-1592`)
mixes four Vulkan writes with ~45 lines of index arithmetic — flatten every
model's textures in model order, stop at `MAX_TEXTURE_COUNT`, pad the remaining
fixed-size slots with slot 0 — and that arithmetic must agree slot-for-slot
with `assignTextureOffsets` (`scene/ObjectDescription.ixx:21-36`), which stamps
the `texture_offset` the shaders add to model-local `textureID`s. If they ever
disagree, every model past the first samples the wrong images; that is the
exact bug the offsets were introduced to fix (`ObjectDescription.hpp:14-19`).
`assignTextureOffsets` has three CPU tests
(`Test/commit/VulkanEngine/objectDescriptionOffsetsSuite.cpp`); its counterpart
has **none**, because it is welded to `DescriptorSetGroup::writeImageArray`.
The extraction also fixes a real (minor) reporting defect: the exhaustion
warning at `:1558-1563` reports `image_info_textures.size() + (model_texture_
count - t)`, which counts only up to the model that overflowed and **ignores
every later model's textures** — so the number it prints is not the total the
scene asked for, which is the one number the message exists to give you.

**Task 3 is one rule with four hand-rolled implementations and no test.**
Every depth image in the engine takes its format from
`chooseDepthFormat` (`common/FormatHelper.hpp:43-49`), whose preference list
contains two combined depth/stencil formats. Deriving the aspect mask from that
format is then done four different ways: `Rasterizer.cpp:30-35` has a private
anonymous-namespace `hasStencilComponent` and builds `eDepth | eStencil`
(`:329-341`, correct — that view is also the subject of a layout transition,
where a combined format must carry both aspects), while
`DeferredRasterizer.cpp:99`, `PostStage.cpp:183` and
`CascadedShadowMap.cpp:56`/`:181` each hard-code `eDepth`. Two of those
hard-codes are **required** to stay single-aspect and must not be "fixed"
(details in the task). Nothing states the rule in one place and nothing tests
it, so the next depth image added is a coin flip.

Found, real, and deliberately **deferred to a later batch** (not rejected — do
not re-derive, just pick up): (a) the two cloud-output `vk::ImageMemoryBarrier`
blocks at `VulkanRenderer.cpp:876-894` and `:908-926` are 20 lines of identical
field setup differing only in stage/access masks, and the ~19-line rationale at
`:928-936` **repeats `:869-874` almost verbatim** — only the 2026-08-01
measurement note at `:937-946` is unique to it; (b)
`DeferredRasterizer.cpp:194-200` lists six attachments including "1: Position",
but the code builds five and has no position attachment — `:87-90` in the same
file explains at length that it was removed. Both are small, both are real
duplication/drift.

### C++ Vulkan engine

### Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate`)

## 2026-08-02 batch — planner (a second vertical mirror, this one in GPU culling; a mirrored world-position reconstruction in the deferred lighting pass; the two comment/duplication items batch XVIII deferred)

Batch XVIII is fully drained (`a3b42dfc`, `21b263e0`, `a1cbbdfd`); every
checkbox left in the file before this batch was `- [b]`. Every `file:line`
below was read out of the tree this pass.

**The headline is that the vertical-mirror bug class fixed in `8b28543c`
(tile-light binning) has two more instances, in two different renderers, and
both are invisible to the tests that cover them because the test scenes are
vertically symmetric.** The two renderers use *opposite* NDC-y conventions and
nothing in the tree writes that down:

| | NDC y | texel/uv v from ndc.y | canonical site |
| --- | --- | --- | --- |
| Vulkan (C++) | +1 = **bottom** | `v = (ndc.y + 1) * 0.5` | `compute/clouds.slang:112-113` |
| WebGPU (Rust) | +1 = **top** | `v = 0.5 - ndc.y * 0.5` | `render/tile_grid.rs:108`, `ssao/ssao.slang:42` |

Task 1 is `gpu_cull/gpu_cull.slang:61` using the **Vulkan** formula
(`float2 uv = ndc * 0.5 + 0.5`) in a shader that is compiled for **WGSL only**
(`compile-slang-shaders.ps1:104`, `Targets = @('wgsl')`) and used only by the
Rust renderer. Every AABB corner therefore lands in the vertically mirrored
half of the depth buffer, so the 8×8 depth-tap rect at `:87` reads occluders
from the wrong place: a primitive occluded at the top of the screen is tested
against whatever is at the bottom. The rewrite in batch VI made the *test*
conservative and correct (`aabbNear > maxSampled`, all eight corners) but
carried this line over unchanged. `tile_grid.rs:108` is the same computation
written the right way, three files away, and was fixed for exactly this reason
in `8b28543c`.

Task 2 is the same class in the C++ deferred lighting pass, and the same shape
as batch XVIII's depth-aspect finding — one rule, several hand-rolled copies,
one of them wrong. `common/fullscreen.slang:25` defines the engine's fullscreen
uv as `float2((x + 1.0) * 0.5, 1.0 - (y + 1.0) * 0.5)`; `ssao/ssao.slang:42`
inverts it correctly (`ndc.y = 1.0 - uv.y * 2.0`); `deferred/deferred.slang`
**re-declares both** — its own `LightingVsOut` (`:87-91`) and
`lighting_vs_main` (`:93-102`) are byte-for-byte `fullscreen.slang`'s, and then
`:110` inverts with `In.uv * 2.0 - 1.0` applied to *both* components. For y that
is not the inverse: substituting the vs mapping gives `clipPos.y = -ndc.y`, so
every reconstructed `worldPos` is mirrored about the horizontal centreline. That
feeds `V` (`:120`) and `calc_cascaded_shadow` (`:132`) — deferred shadows land in
the wrong half of the frame, at `cascaded_shadow_intensity = 0.65`
(`GUISceneSharedVars.ixx:20`).

**Task 2 also has to resolve a contradiction that its own oracle exposes.**
`goldenRenderSuite.cpp:1036` asserts forward-vs-deferred mean absolute channel
difference `< 1.0`, with `:1027-1028` recording `~0.2` measured on this rig. That
number cannot be right for these two shaders even *without* the mirror: forward
(`forward/forward.slang:401-423`) adds IBL or hemisphere ambient, tiled punctual
lights, emissive and occlusion on top of the directional BRDF, while deferred
(`deferred/deferred.slang:126-133`) computes the directional BRDF and the cascade
shadow and nothing else. Two images 0.2/255 apart are the same image. Either the
capture is still not deferred (`:999-1004` records that exact failure happening
once already, fixed by `handleRasterizationModeChange`,
`VulkanRenderer.cpp:277-294`) or the recorded measurement is stale. Task 2 makes
the test say which.

Tasks 3-5 are the small refactors. Task 3 is the consolidation half of task 2 and
must land **after** it (both edit `deferred.slang`). Tasks 4 and 5 are the two
items batch XVIII found, verified and explicitly deferred — both re-confirmed
present this pass, so pick them up rather than re-deriving them.

Numbering used throughout this preamble and inside the entries (the checkboxes
below are split across two sections, so they are not in this order):
**1** = `gpu_cull.slang` mirror (Rust section);
**2** = deferred lighting mirror + parity oracle;
**3** = one fullscreen uv↔NDC definition;
**4** = cloud-output barriers;
**5** = `DeferredRasterizer` attachment comment.

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **extracting the free pipeline/bind-group constructors at
`forward.rs:2591-2943` into a `render/pipelines.rs`** — this is the third time
this file has offered pure code motion and batches XIII and XVII both rejected it
for the same reason; **`GpuCulling`'s readback ring**
(`gpu_occlusion.rs:300-383`) — `free_slot()` scans from `current` and `end_frame`
advances past the slot it just mapped, so the two agree, and the `generation`
guard correctly discards cross-scene results; **`bloom.rs`/`tonemap.rs`/`ssao.rs`
having zero tests** — real, but each is a thin wgpu-object wrapper whose only
CPU logic is `(width / 2).max(1)` (`bloom.rs:112`), which is not worth a suite;
**`DeferredRasterizer.cpp:352`'s missing push-constant range on the lighting
pipeline layout** — `lighting_fs_main` never references the module-scope
`pc_raster` (`deferred.slang:11`), so Slang does not emit it into that entry
point and the layout is correct; verifying it needs the compiled SPIR-V, which
is gitignored.

### C++ Vulkan engine

- [b] **(L) Fix the vertical mirror in the deferred lighting pass's world-position reconstruction, and make the forward/deferred parity oracle able to tell the two paths apart** — deferred shadows are sampled from the mirrored half of the frame, and the test that should have caught it currently claims the two paths agree to 0.2/255.

  **BLOCKED 2026-08-02:** Step 1 requires measuring
  `GoldenRender.DeferredMatchesForwardRoughly`'s current mean-abs-diff on the
  host GPU before touching the shader, and step 5/6 require GPU pixel capture
  to add and validate the new oracle. Host GPU golden verification is
  currently broken in this RDP session (see memory
  `host-gpu-golden-verification`, entries from 2026-08-01): every
  `GoldenRender.*` test fails immediately with `No synchronization frames
  available; skipping draw frame.` (`frameSync.frameSyncCount() == 0`, i.e.
  the swapchain came back with zero images), reproduced here with `quser`
  showing the RDP session as Active (`jonas`, session 2). This is
  environmental, not a code regression — `GoldenRender.RendersNonBlankFrame`
  (untouched) fails identically. Re-attempt once a console/physical session
  restores swapchain image counts; do not chase this further from a headless
  executor run per that memory's guidance ("don't burn more than one retry").

  **Files to read:**
  - `Resources/ShadersSlang/deferred/deferred.slang:87-134` — `LightingVsOut`,
    `lighting_vs_main`, `lighting_fs_main`. `:100` writes the uv, `:110` inverts it.
  - `Resources/ShadersSlang/ssao/ssao.slang:40-46` — `view_pos_at`, the correct
    inverse of the same uv mapping. Copy its y term.
  - `Resources/ShadersSlang/common/fullscreen.slang:11-27` — where the uv
    convention is defined and documented ("UV origin is top-left (y flipped)").
  - `Resources/ShadersSlang/forward/forward.slang:394-423` — the term list the
    forward path actually computes, for the parity discussion below.
  - `Src/GraphicsEngineVulkan/renderer/VulkanRenderer.cpp:169`,`:198-199` —
    `projection[1][1] *= -1` then `inv_projection = inverse(projection)`, i.e.
    `inv_projection` expects the fragment's **true** Vulkan NDC, whatever the
    projection is. This is why feeding `-ndc.y` is wrong independently of the
    projection convention.
  - `Test/commit/VulkanEngine/goldenRenderSuite.cpp:947-1039` —
    `GoldenRender.DeferredMatchesForwardRoughly`, the test to strengthen.
  - `Test/commit/VulkanEngine/goldenRenderSuite.cpp:900-945` — the
    shadow-direction test immediately above it; its "shadowed-pixel union" +
    `moved_fraction` construction is the pattern to follow for the new oracle.

  **Steps:**
  1. **Measure before touching anything.** Build `clangcl-debug`, copy
     `commitTestSuite.exe` to the host, and run
     `--gtest_filter='GoldenRender.DeferredMatchesForwardRoughly'` from the repo
     root on the GPU. Record the `deferred-vs-forward mean abs channel diff`
     line it logs. This number decides step 2.
  2. If the measured diff is still ≈0.2, the deferred capture is **not
     deferred** — find out why before changing the shader (start at
     `VulkanRenderer.cpp:277-294`: confirm `handleRasterizationModeChange` runs
     on the headless harness's frame path at all, and that the post stage's
     input descriptor follows the mode). Fix that first; the rest of this task
     is meaningless until the test compares two different images.
  3. Fix the mirror: replace `deferred.slang:110` with
     `float4 clipPos = float4(In.uv.x * 2.0 - 1.0, 1.0 - In.uv.y * 2.0, depth, 1.0);`
     Add a one-line comment pointing at `ssao.slang:42` as the shared inverse.
     Do **not** touch `lighting_vs_main` in this task — that is task 3.
  4. Recompile shaders (`Scripts/Windows/compile-slang-shaders.ps1`). `deferred`
     is SPIR-V-only (`compile-slang-shaders.ps1:72-75`), so no WGSL is affected.
  5. Add the oracle, `GoldenRender.DeferredShadowsLandWhereForwardShadowsLand`:
     for each of `Forward` and `Deferred`, capture once with
     `scene_vars.cascaded_shadow_intensity = 0.0F` and once with the default
     `0.65F`, and build the shadowed-pixel mask (per-pixel luminance dropped by
     more than a small threshold between the two captures). Then assert
     `IoU(mask_deferred, mask_forward) > IoU(mask_deferred, vflip(mask_forward))`
     by a clear margin, and that `mask_forward` is neither empty nor the whole
     frame (otherwise the comparison is vacuous). This is direction-of-error
     sensitive and survives driver differences, which a pixel threshold would
     not. `SKIP_WITHOUT_GPU()` and the `supportsFrameCapture()` guard as in the
     neighbouring tests.
  6. Re-run the measurement from step 1 and **update the comment at
     `goldenRenderSuite.cpp:1025-1029` with the new number**. If the honest diff
     is now above `MEAN_ABS_DIFF_LIMIT = 1.0` because deferred genuinely lacks
     forward's ambient/punctual/emissive terms, raise the limit to the measured
     value plus headroom and replace the "the paths no longer shade alike"
     wording with the actual term-by-term gap (list it). Do **not** add the
     missing lighting terms to the deferred shader — that is a renderer-scope
     decision, not this task; record it as a new backlog idea instead.

  **Test:** `GoldenRender.DeferredShadowsLandWhereForwardShadowsLand` (new) plus
  the corrected `GoldenRender.DeferredMatchesForwardRoughly`. Update the golden
  counts in `docs/gpu-golden-testing.md` — `BuildIntegrity.GoldenTestCountsInDocsMatchTheSuite`
  (`buildIntegritySuite.cpp:1841`) will fail otherwise.

  **Build:** `clangcl-debug`. Run:
  `pwsh -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows-Container.ps1 -Configurations clangcl-debug`
  then `docker cp bb-build-persistent:C:\ws\build-clangcl-debug\bin\commitTestSuite.exe .\`
  and run it from the repo root on the host GPU (containers have no swapchain —
  AGENTS.md § Running on the Host).

  **Context:** Same bug class as `8b28543c` (tile-light binning) and the same
  shape as batch XVIII's depth-aspect finding: a rule with several hand-rolled
  copies, one of which is wrong. The parity test's own comment
  (`goldenRenderSuite.cpp:999-1004`) documents that it passed for weeks while
  comparing forward against forward — treat a suspiciously small diff as
  evidence of that failure mode recurring, not as good news.

- [b] **(S) (refactor) Give the fullscreen uv↔NDC round trip one definition, and pin it** — `deferred.slang` re-declares `fullscreen.slang`'s vertex output and vertex shader verbatim, which is how its inverse drifted.

  **BLOCKED 2026-08-02:** This task's own **Context** says to do it "after task 2
  [the deferred-mirror fix] and not concurrently — both edit `deferred.slang`",
  and its **Test** section requires re-running `GoldenRender.*` on the host GPU
  to confirm the refactor is pixel-neutral. The deferred-mirror fix is itself
  blocked on the same host-GPU verification (see the entry above), so both its
  prerequisite and its own acceptance test are unavailable right now. Re-attempt
  after the deferred-mirror task unblocks.

  **Files to read:**
  - `Resources/ShadersSlang/common/fullscreen.slang:1-27` — `FullscreenVsOut`,
    `fullscreen_vs`, and the usage comment at `:7-9`.
  - `Resources/ShadersSlang/deferred/deferred.slang:87-105` — the duplicate.
  - `Resources/ShadersSlang/ssao/ssao.slang:40-52` — the correct consumer:
    imports `fullscreen`, calls `fullscreen_vs`, and inverts in `view_pos_at`.
  - `Resources/ShadersSlang/ibl/ibl.slang:20-22` — a deliberate exception
    (different triangle winding), documented in place. Must stay allowlisted.
  - `Test/commit/VulkanEngine/buildIntegritySuite.cpp:1445` —
    `NoShaderRedeclaresTheCascadeCount`, the source-scanning gate to copy.

  **Steps:**
  1. Add to `common/fullscreen.slang`, next to `fullscreen_vs`, the inverse:
     `float2 fullscreen_uv_to_ndc(float2 uv)` returning
     `float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0)`, with a comment stating that it
     is the exact inverse of `fullscreen_vs`'s uv and that the two must be edited
     together.
  2. In `deferred.slang`: `import fullscreen;`, delete `LightingVsOut` (`:87-91`)
     and the body of `lighting_vs_main` (`:96-101`) in favour of
     `return fullscreen_vs(vid);` returning `FullscreenVsOut`, change
     `lighting_fs_main`'s parameter type to `FullscreenVsOut`, and build
     `clipPos` from `fullscreen_uv_to_ndc(In.uv)`. Entry-point **names** must not
     change — `compile-slang-shaders.ps1:74-75` and
     `buildIntegritySuite.cpp:944-947` both list them.
  3. In `ssao.slang`, rewrite `view_pos_at`'s first line to use
     `fullscreen_uv_to_ndc(uv)` so there is exactly one copy of the y term.
  4. Recompile shaders; `ssao` emits WGSL, so re-run
     `Scripts/Windows/compile-slang-shaders.ps1` and commit the regenerated
     `crates/webgpu_renderer/src/shaders/ssao.wgsl` in the submodule
     (`BuildIntegrity.CheckedInWgslIsNotOlderThanItsSlangSource` and
     `CheckedInWgslHasNoHandEdits` enforce this).
  5. Add `BuildIntegrity.NoShaderRedeclaresTheFullscreenUvMapping`: scan every
     `Resources/ShadersSlang/**/*.slang` for the literal uv expression
     (`1.0 - (y + 1.0) * 0.5`) and for a re-declared
     `struct ...VsOut { float4 svPosition : SV_Position; float2 uv : TEXCOORD0; };`,
     and fail on any hit outside `common/fullscreen.slang`, with `ibl/ibl.slang`
     allowlisted by name plus its stated reason. Follow
     `NoShaderRedeclaresTheCascadeCount`'s structure, including anchoring the
     allowlist to a source marker rather than a line number (`e8b1db52`).

  **Test:** `BuildIntegrity.NoShaderRedeclaresTheFullscreenUvMapping` (new, pure
  CPU). Re-run `GoldenRender.*` on the host GPU to confirm deferred and SSAO are
  unchanged — this task must be pixel-neutral, since task 2 already fixed the
  behaviour.

  **Build:** `clangcl-debug`, same invocation as task 2.

  **Context:** Do this **after** task 2 and not concurrently — both edit
  `deferred.slang`. Straight continuation of `a3b42dfc` (one depth-aspect
  definition in `FormatHelper.hpp`) and `f97712f4` (one `ShaderStagePair` instead
  of six copies): the payoff is that the next fullscreen pass cannot get the
  convention wrong, not the lines removed.

- [b] **(S) (refactor) Collapse the two cloud-output image barriers and the rationale comment that is written twice** — deferred from batch XVIII with the finding already verified; ~20 lines of identical field setup and a ~10-line comment repeated almost verbatim.

  **BLOCKED 2026-08-02:** This task's acceptance test is
  `GoldenRender.CloudsAcrossManyFramesDoesNotLoseTheDevice` staying green on
  the host GPU, plus an optional sync-validation rerun. Host GPU golden
  verification is currently unavailable in this RDP session (see the blocked
  deferred-mirror task above for the reproduced failure). This refactor
  touches cross-frame WAR barrier synchronization, so shipping it unverified
  risks a real device-lost/sync-hazard regression rather than a cosmetic one
  — not worth doing blind. Re-attempt once host GPU verification is restored.

  **Files to read:**
  - `Src/GraphicsEngineVulkan/renderer/VulkanRenderer.cpp:871-951` — the whole
    clouds block. `:881-893` and `:913-925` are the two barriers; `:871-880` and
    `:933-941` are the duplicated rationale; `:942-951` is the only part of the
    second comment that is unique (the 2026-08-01 sync-validation measurement).

  **Steps:**
  1. Add a small file-local helper (anonymous namespace or a static lambda above
     the clouds block) that takes the image plus src/dst stage and access masks
     and records an `eGeneral -> eGeneral` colour-aspect
     `vk::ImageMemoryBarrier` — every other field is identical between the two
     call sites.
  2. Replace both barriers with calls to it:
     `eFragmentShader -> eComputeShader`, `{} -> eShaderWrite` before
     `recordComputeCommands`, and `eComputeShader -> eFragmentShader`,
     `eShaderWrite -> eShaderRead` after. Do not change any mask — this is a
     pure de-duplication and the goldens must not move.
  3. Keep the cross-frame-WAR rationale **once**, on the helper or on the first
     call site. Delete the repeat at `:933-941` but **keep `:942-951` intact** —
     the sync-validation measurement is unique and load-bearing evidence.
  4. Preserve the second comment's distinct content too (`:903-912`: why this is
     hand-written rather than `VulkanImage::transitionImageLayout`'s
     `eGeneral->eGeneral` overload, which would cost `eAllCommands ->
     eAllCommands`). That reasoning is not duplicated anywhere.

  **Test:** No new test. Re-run `GoldenRender.CloudsAcrossManyFramesDoesNotLoseTheDevice`
  on the host GPU; it must stay green. If `Scripts/Windows/Run-SyncValidation.ps1`
  is runnable in this environment, re-run it and confirm no `SYNC-HAZARD`,
  matching the measurement the comment records.

  **Build:** `clangcl-debug`, same invocation as task 2.

  **Context:** Batch XVIII found this, confirmed it, and deferred it explicitly
  — do not re-derive it. `9aac4cb2` (Clouds' duplicated storage-texture and
  compute-pipeline setup) is the pattern; the risk here is deleting the wrong
  half of the comment, so re-read `:942-951` before cutting.

## 2026-08-02 batch II — planner (a partial-face OBJ read that runs off the end of `indices`, a glTF emitter that writes unescaped JSON, a texture-path rule the two renderers disagree on, unpinned histogram constants)

**Every task in this batch is verifiable with no GPU.** The three `- [b]`
entries above are all blocked on host GPU golden verification, which is still
unavailable in this session, so the whole batch was chosen to be provable with
`commitTestSuite.exe --gtest_filter='ObjParseUnit.*'` (device-free by
construction — `objParseSuite.cpp:1-10`) and `cargo test`. Nothing below needs
a swapchain. Every `file:line` was read out of the tree this pass.

**The headline is a memory-safety bug in the OBJ loader that its own fuzz
target cannot find.** `ObjLoader::loadVertices` guards each face *vertex*
against a malformed index and `continue`s past it
(`ObjLoader.cpp:250-252`), but the enclosing face keeps going: the other two
vertices are still emitted. That breaks the "`indices.size()` is a multiple of
3" invariant the rest of the function assumes, and the flat-normal pass at
`:333-344` walks `i += 3` bounded only by `i < indices.size()`, so it reads
`indices[i+1]` / `indices[i+2]` past the end. `GltfLoader.cpp:358` writes the
same loop correctly (`i + 2 < indices.size()`) and adds a degenerate-triangle
guard the OBJ copy lacks — one rule, two hand-rolled copies, one wrong, which
is the same shape as batch XVIII's depth-aspect finding and `a3b42dfc`.

Reachability was traced through the vendored parser rather than assumed:
`tiny_obj_loader.h:6398` (`fixIndex`) returns `idx - 1` for any **positive**
index with no upper-bound check, and the `} else {` branch at `:8155` — which
is the path a triangulating reader takes for a plain 3-vertex `f` — copies the
face's indices into `shape.mesh.indices` verbatim with no validation. (The
`npolys != 3` branch at `:7674` *does* validate, which is why quads are safe
and triangles are not.) So `v 0 0 0 / v 1 0 0 / v 0 1 0 / f 1 2 999999`
parses successfully and hands the loader an out-of-range positive index. Three
lines of OBJ.

`Test/fuzz/obj_parsing_fuzz_test.cpp` claims in its header comment to "prove
malformed model files cannot crash or OOB-read the loader", but its mirror
(`:44-73`) stops at the *attribute* reads — it accumulates a checksum and never
reproduces the index emission or the flat-normal post-pass, i.e. exactly the
two places the bug lives. Task 1 fixes the loader and closes that hole in the
harness. There is a third mirror of the same walk in `Test/perf/perfSuite.cpp:214`.

Tasks 3-5 are independent. Task 3 is a hand-written JSON emitter that
interpolates untrusted strings unescaped. Task 4 is a genuine cross-renderer
divergence: measured against the shipped asset tree, the C++ engine resolves
**23/23** `map_Kd` references correctly and the Rust converter resolves **0/23**.
Task 5 pins the one hand-written WGSL shader in the tree against its Rust
constants — the Slang manifest gates cannot cover it because it is not generated
from Slang (AGENTS.md § Shaders).

Ordering: task 2 must land **after** task 1 (both edit `ObjLoader.cpp`'s
flat-normal loop, and task 1 is the behaviour fix while task 2 is pure
consolidation). Everything else is independent.

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **`common/cascaded_shadow.slang:32` (`proj.xy * 0.5 + 0.5`)
vs `forward/forward.slang:230` (`0.5 - proj.y * 0.5`) looking like a third
vertical mirror** — it is not one; `cascaded_shadow.slang` is SPIR-V/Vulkan
(NDC +1 = bottom) and `forward.slang` is the Rust/WebGPU shader (NDC +1 = top),
so each matches its own renderer's convention per the table in the batch above;
**`MAX_OBJECTS`'s abuse as a byte count in the descriptor pool size**
(`VulkanRenderer.cpp:1421-1423` passes `sizeof(ObjectDescription) * MAX_OBJECTS`
as a *descriptor count*) — wrong units, but it only over-allocates, and the line
above it already says "Historical pool sizing kept as-is (generously
overallocated)"; **`GltfLoader.cpp:296-299`'s "Placeholder for now" comment** —
reads like a TODO but the flat normals it defers to are genuinely computed at
`:352-372`, so the comment is confusing rather than stale; **`bloom.rs` /
`tonemap.rs` / `ssao.rs` / `context.rs` having no unit tests** — re-confirmed,
and re-rejected for the reason the batch above gave.

### C++ Vulkan engine

### Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate`)

### Cross-renderer

## 2026-08-02 batch III — planner (refactor: a guard whose test pins a copy of it, a full-extent viewport written five times, a per-model count vector built twice on both halves of one invariant)

**Every task in this batch is verifiable with no GPU**, deliberately: the four
`- [b]` entries above are all blocked on host GPU golden verification, which is
still unavailable. All three tasks below are provable with device-free suites
(`SkyBoxUnit.*`, a new `ViewportHelperUnit.*`, `SceneAccessorUnit.*` — the last
constructs a bare `Scene` with no `VulkanDevice`, see
`sceneAccessorSuite.cpp:18-31`). Every `file:line` was read out of the tree this
pass.

**The theme is one rule with more than one hand-rolled copy** — the same shape as
`a3b42dfc` (depth aspect → `FormatHelper.hpp`), `f97712f4` (six shader-stage
blocks → `ShaderStagePair`) and `15e64b10` (flat normals shared between loaders).
Task 1 is the sharpest of the three because the duplicate copy lives *in the
test*: `skyBoxSuite.cpp:11-14` states outright that it re-declares
`cubemapFacesConsistent` rather than calling it, so the suite would stay green
through any regression of the shipped guard. That is a test-coverage gap wearing
a passing test's clothes. Task 3 is the same failure mode one level up: the
"flatten every model's textures in model order" invariant is explicitly
documented as having two halves (`VulkanRenderer.cpp:1536-1541`), and each half
builds its own `textureCountPerModel` loop, so the two can silently disagree
about model count or ordering.

Ordering: all three are independent and touch disjoint files. Task 2 is the
largest edit surface (five files) but the least subtle.

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **`SkyBox::recreateFrameResources` being a one-line
pass-through to `createFramebuffers` (`SkyBox.cpp:500-503`)** — it is a
pass-through, but it is the shape `Rasterizer`/`DeferredRasterizer`/`PostStage`
all present to `VulkanRenderer.cpp:677-688`, so collapsing only SkyBox's would
make the call site *less* uniform, not more; **`Scene::getObjectDescriptions()`
returning by value (`Scene.ixx:111`)** — the one caller
(`VulkanRenderer.cpp:1304`) mutates the result via `assignTextureOffsets`, so the
copy is load-bearing; **`MAX_OBJECTS` used as a byte count in the descriptor pool
size (`VulkanRenderer.cpp:1421-1423`)** — re-confirmed as wrong units and
re-rejected for the reason batch II gave (it only over-allocates, and the line
above already documents the generous sizing).

## 2026-08-02 batch IV — planner (a BLAS that declares one vertex too many, a GPU ranking where the tie-break outweighs the device type, a shadow-map combo that lies at startup, two hard-coded dispatch grids, a KTX2 mip count nothing checks)

**Every task in this batch is verifiable with no GPU**, deliberately: the four
`- [b]` entries above are all blocked on host GPU golden verification, which is
still unavailable in this session. All five tasks below are provable with
device-free gtest suites (the `SwapchainChoices.hpp` / `FormatHelper.hpp`
"extract the pure decision into a header, test it without a device" pattern —
`swapchainChoicesSuite.cpp:1-6`, `formatHelperSuite.cpp:1-16`), a
`BuildIntegrity` source-scan gate, or `cargo test`. Every `file:line` below was
read out of the tree this pass.

**The headline is a Vulkan spec violation in the BLAS build.**
`ASManager::objectToVkGeometryKHR` sets
`acceleration_structure_triangles_data.maxVertex = mesh->getVertexCount()`
(`ASManager.cpp:495`), but `maxVertex` is defined as the **highest index of a
vertex that will be addressed**, i.e. `vertexCount - 1` — and
`Mesh::vertex_count` is `vertices.size()` (`Mesh.cpp:38`). Every BLAS this
engine builds therefore tells the driver it may read one `sizeof(Vertex)`
stride past the end of the vertex buffer. It is invisible today only because
VMA suballocation almost always leaves readable bytes there and because
`robustBufferAccess` is explicitly `VK_FALSE` (`VulkanDevice.cpp:484`), so
there is no bounds check to trip either. This is the same shape as the SBT
handle-stride finding (`74aaee23`): correct on this GPU, undefined by the spec.

Tasks 2-5 are independent. Task 2 is a ranking function whose tie-break term is
numerically larger than the gaps it is meant to break. Task 3 is a startup/GUI
divergence the user sees on every launch. Task 4 pins two dispatch grids against
the `[numthreads(...)]` they must match, the way `ee4abb24` pinned `TILE_SIZE`.
Task 5 is untrusted-asset hardening in the Rust crate, the same class as
`a0cffe7a` / `d25cd1e5`.

Ordering: all five are independent and touch disjoint files.

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **`Texture::createImage` never recording its
`in_mip_levels` into the `mip_levels` member (`Texture.cpp:208-223`), so
`createTextureSampler` builds a `maxLod = 0` sampler for every non-`uploadRgba`
caller** — real, but all three such callers (`Clouds.cpp:33`, `SkyBox.cpp:97`,
`CascadedShadowMap.cpp:54`) pass exactly 1 mip, for which `maxLod = 0` is the
correct value; **the scratch-buffer reuse barrier in
`ASManager::createBLAS:112-121`** — `srcAccessMask = eAccelerationStructureWriteKHR`
/ `dstAccessMask = eAccelerationStructureReadKHR` between consecutive builds is
verbatim the nvpro-core `cmdCreateBlas` idiom, not a missing WAW barrier;
**`asset/hdr.rs`** — swept for the OOB/overflow class the OBJ loader had and
found already hardened at every branch (`checked_add` on the old-style repeat
shift, `get_mut` on every span, a zero-length-literal guard against the infinite
loop), with a test that enumerates twelve malformed inputs; **`ASManager::cleanUp`
leaving `tlas.vulkanAS` non-null and `blas` unshrunk (`ASManager.cpp:381-401`)**
— a double-`cleanUp` would double-destroy, but the only caller
(`VulkanRenderer.cpp:1173`) runs once at shutdown and the destructor is
`= default`, so there is no reachable path.

### C++ Vulkan engine

### Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate`)

## 2026-08-02 batch V — planner (an SSAO hemisphere pointed into the screen, an exposure shader that snaps on a stalled frame, the last unpinned dispatch grid plus its inert spec constants, two command buffers that record nothing, an LOD merge that is only half order-independent)

**Every task in this batch is verifiable with no GPU**, deliberately: the five
`- [b]` entries above are all blocked on host GPU golden verification, which is
still unavailable in this session. Tasks 1, 2 and 5 are provable with `cargo
test` (unit tests, plus headless tests that already skip themselves when no
adapter is present — `headless.rs:621-624`); task 3 is a `BuildIntegrity`
source-scan gate of exactly the shape `a348bd9f` added for the cloud dispatches;
task 4 is a provable no-op (an empty command buffer executes nothing) that the
compiler and the existing suites confirm. Every `file:line` below was read out of
the tree this pass.

**The headline is that the Rust renderer's SSAO hemisphere points away from the
camera.** `ssao.slang:66` builds the reconstructed normal as
`normalize(cross(px - p, py - p))` where `px` is one texel to the RIGHT and `py`
one texel DOWN. `view_pos_at` (`ssao.slang:40-45`) maps `uv.y` down to `ndc.y`
up, and the camera is `Mat4::perspective_rh` (`scene/camera.rs:52`), so in view
space `px - p ≈ +X` and `py - p ≈ -Y`; `cross(+X, -Y) = -Z`. View space here is
right-handed with visible geometry at `z < 0`, so a surface facing the camera has
a normal of **+Z** — every reconstructed normal is negated. The kernel
(`ssao.slang:17-30`, all twelve entries with `k.z > 0`) is then rotated into a
hemisphere that points *into* the surface, so every sample lands behind the
geometry and the occlusion test `sceneZ >= samplePos.z + bias` is true for
essentially every sample on every surface. Worked through at the shipped tuning
(`forward.rs:1663`, `radius = 0.6, bias = 0.02, intensity = 1.0`) for a
fronto-parallel wall at view `z = -5`: `samplePos.z ≈ -5.18`, `sceneZ = -5`,
`rangeCheck = 1`, so `ao = 1 - 12/12 = 0` — the AO buffer is black wherever there
is geometry, and `tonemap.slang:58-60`'s `lerp(1.0, aoRaw, ssao_strength)`
multiplies the whole image by `1 - ssao_strength` (0.3 at the default 0.7). SSAO
currently contributes a flat global dimming and **no** crease darkening at all.
The existing oracle cannot see it: `ssao_darkens_geometry`
(`tests/headless.rs:620-655`) only asserts that total energy drops, which a
uniform multiply satisfies perfectly — the same "the test that should have caught
it cannot tell the two cases apart" shape as `6a6fa2bf` and `8b28543c`.

Tasks 2-5 are independent. Task 2 is a CPU-oracle/shader divergence on a case
`FrameClock` is documented to produce. Task 3 finishes the dispatch-pinning sweep
`a348bd9f` started and deletes the specialization machinery that the
already-fixed entry at line 2044 proved inert. Tasks 4 and 5 are refactors, the
second of which strengthens an invariant the module already claims.

Ordering: all five are independent and touch disjoint files. Task 1 regenerates a
committed `.wgsl`, so run it before any other change that touches
`Resources/ShadersSlang/`.

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **`Texture::createImage` not recording `in_mip_levels`
into `mip_levels`** — re-confirmed, re-rejected for the reason batch IV gave;
**`PostStage::recreateFrameResources` not destroying its framebuffers before
`createFramebuffer()` re-fills the vector (`PostStage.cpp:149-155`)** — looks
like a per-resize leak, but `VulkanRenderer::recreateSwapChain` calls
`postStage.destroyFramebuffers()` first (`VulkanRenderer.cpp:664`), and the same
holds for `Rasterizer`/`DeferredRasterizer`/`SkyBox`; **`VulkanSwapChain`'s
`std::vector<Texture>` reallocating mid-loop while each element wraps a swapchain
image (`VulkanSwapChain.cpp:155-161`)** — `VulkanImage::setImage` clears
`owns_image` (`VulkanImage.cpp:153-159`) and the move constructor carries the
flag, so a reallocation cannot double-destroy; **`cs_reduce_exposure` having no
`is_finite` recovery for a NaN `exposure_state[0]`, unlike
`adapt_exposure_ev`** — real divergence, but nothing writes NaN into that buffer
(`histogram.wgsl` guards every divide) so there is no reachable path.

### C++ Vulkan engine

### Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate`)

## 2026-08-02 batch VI — planner (refactor: a RED CI gate that says three suites never run, a source file grep classifies as binary, a descriptor-pool override with one caller that passes bytes as a count)

**Every task in this batch is verifiable with no GPU**, deliberately: the twelve
`- [b]` entries above are all blocked on host GPU golden verification, which is
still unavailable in this session. Task 1 is a YAML edit proven by a test that is
red *right now*; task 2 is a one-character source fix plus a `BuildIntegrity`
source-scan gate of exactly the shape `a348bd9f`/`302faa90` added; task 3 is a
dead-code deletion whose only behavioural edge becomes a pure free function with
its own device-free suite. Every `file:line` below was read out of the tree this
pass; the actionable queue was empty when this batch was written.

**The headline is that `BuildIntegrity.EveryCpuSuiteIsInTheWindowsCiFilter` is
currently RED, and it is red about the thing it exists to catch.** Three
device-free suites are defined under `Test/commit/VulkanEngine` but appear
neither in `Windows.yml`'s `$cpuOnlySuites` array (`.github/workflows/Windows.yml:209-244`)
nor in the test's `gpu_excluded_suites` set (`buildIntegritySuite.cpp:1066`):
`BlasGeometryLimitsUnit` (`blasGeometryLimitsSuite.cpp`, added by `20c242ef`),
`ShadowResolutionUnit` (`guiSceneVarsRoundTripSuite.cpp:193,202,212`, added by
`adb0c183`) and `ViewportHelperUnit` (`viewportHelperSuite.cpp`, added by
`29081658`). Reproduced this pass by running the gate's own two parsers by hand:
`collect_defined_suites`'s start-of-line `TEST(`/`TEST_F(` anchoring
(`buildIntegritySuite.cpp:226-258`) against `parse_ci_filter_suites`'s
`$cpuOnlySuites` scrape (`:355-386`) yields exactly those three names and no dead
filter entries. Because `'BuildIntegrity.*'` *is* in the filter
(`Windows.yml:210`), the Windows CI test step runs this assertion and fails on
it — so the three suites do not run in CI *and* the gate that reports it is
itself failing the job. The comment directly above the array
(`Windows.yml:196`, "If you add a suite, add it to this filter or it will not
run in CI") is the instruction three consecutive executor commits missed.

**Second finding: `SceneConfig.cpp` contains a literal NUL byte, so grep and
ripgrep classify the whole file as binary and silently skip it.**
`Src/GraphicsEngineVulkan/scene/SceneConfig.cpp:116` reads
`if (*override_path != '<NUL>')` — the intended `'\0'` was committed as the raw
0x00 byte itself (confirmed at byte offset 4573; it is the only NUL in any
tracked file under `Src/`, `Test/`, `Resources/ShadersSlang/`, `cmake/` or
`Scripts/`). The code is semantically correct and compiles, but every
`grep -rn`/`rg` over `Src/` reports "binary file matches" instead of the line, so
`resolveModelPath`, `getModelFile`, `getAvailableModelPaths`,
`findResourcesBasePath` and `KATAGLYPHIS_MODEL_OVERRIDE` are all invisible to the
search tool that both the agentic loop and a human use to find call sites. Two
searches in this planning pass returned "Binary file
Src/GraphicsEngineVulkan/scene/SceneConfig.cpp matches" where a real hit list was
expected. This is the same failure class as the shader-manifest and CI-filter
gates: a source of truth that tooling silently declines to read.

**Third finding: `DescriptorSetGroup`'s pool-size override exists for exactly one
caller, and that caller passes a byte count where a descriptor count belongs.**
`setPoolSize` (`DescriptorSetGroup.ixx:41-43`, `.cpp:77-89`) is called from one
place in the whole tree — `VulkanRenderer.cpp:1415-1417`, `setPoolSize(eStorageBuffer,
sizeof(ObjectDescription) * MAX_OBJECTS)` = 40 × 40 = **1600** storage-buffer
descriptors, where the derived rule (`DescriptorSetGroup.cpp:108-127`,
`binding.descriptorCount * set_count`) gives 1 × 3. Batches II and III both saw
the wrong units and rejected a fix because "it only over-allocates". **The new
evidence is that the units are not the point**: the override is the sole reason
`pool_size_overrides` exists at all, and it drags a member
(`DescriptorSetGroup.ixx:95`), three move-constructor/assignment clauses
(`.cpp:29,47,55`), a `create()` branch (`.cpp:112-115,127`), a `cleanUp()` line
(`.cpp:270`) and two doc comments (`.ixx:19-20,70-71`) behind it. Delete the one
call and all of it is dead — and the derivation left behind is pure, device-free
logic that has **zero** test coverage today (`grep -rn DescriptorSetGroup Test/`
returns nothing). `MAX_OBJECTS` (`common/Globals.hpp:4`) then has no reader
either. Three never-called public accessors are in the same deletion class,
grep-confirmed at one occurrence each across `Src/` + `Test/`:
`VulkanDevice::getAllocator` (`VulkanDevice.ixx:43`),
`VulkanDevice::getPhysicalDeviceProperties` (`:20`) and
`Mesh::getMaterialIDBuffer` (`Mesh.ixx:42` — the *buffer* is live via
`Mesh.cpp:78`'s device address; only the accessor is dead).

**Ordering matters here, unusually.** Do task 1 first: task 3 adds a new suite
name and will trip the same gate if the array is still stale, and until task 1
lands a green Windows CI run is impossible for any task in this batch.

Candidates found but NOT tasked (checked, then rejected or deferred with a reason
— do not re-propose without new evidence): **`CascadedShadowMap::updateCascades`'s
two per-frame heap allocations** — real (`CascadedShadowMap.cpp:88` move-assigns a
fresh `std::vector<CascadeData>` from `computeCascadeData`, `:105` builds a
`std::vector<glm::mat4>` purely to `memcpy` into the mapped UBO, both on the
per-frame path via `VulkanRenderer.cpp:194`), and the fix is the
`std::array`/`std::span` shape the same module already uses at
`CascadedShadowMapMath.cpp:29` and `CascadedShadowMap.cpp:443` — deferred purely
for the three-task cap, **pick this up next cycle** (it wants a
`computeCascadeDataInto(std::span<CascadeData>, …)` overload so the fifteen
existing `computeCascadeData` call sites in `cascadedShadowMapSuite.cpp` keep
compiling); **18 source files carrying a UTF-8 BOM while the rest do not**
(`GUI.cpp:1`, `Mesh.ixx:1`, `VulkanSwapChain.cpp:1`, …) — cosmetic, no tool in
this project misreads a BOM, and stripping them is pure diff noise;
**`resolveModelPath` (`SceneConfig.cpp:19-41`) and `findResourcesBasePath`
(`:44-67`) running near-identical depth-8 walks for a `Resources/` root** — real
duplication, but they test different predicates (`Resources/<rel>` vs
`Resources/Models`) and collapsing them changes which ancestor wins in trees
where only one predicate holds, so it needs a decision rather than a mechanical
extraction; **`CascadedShadowMap::recordCommands`'s draw loop
(`CascadedShadowMap.cpp:485-517`) as a third copy of
`MeshDrawRecorder::recordSceneMeshDraws`** — the two differ in push-constant
struct *and* in single-frustum vs union-over-cascades visibility, so this is a
generalization, not the verbatim extraction the `MeshDrawRecorder.ixx:24-32`
comment describes; **the docs sweep** — every repo-rooted `` `path` `` reference
in `docs/`, `README.md` and `AGENTS.md` resolves, and the two
`Resources/Shaders/` hits (`shader-build-pipeline.md:53`,
`shader-sharing.md:140-148`) are both inside explicit "Historical note" sections.

### C++ Vulkan engine

## 2026-08-02 batch VII — planner (a uniform block the host and the shader lay out differently, the gate that would have caught it, two glTF loaders that trust index values, the per-frame allocation batch VI deferred)

**The headline is `SceneUBO`: the C++ struct and the compiled SPIR-V disagree
about where every field from `cascadeSplits` onward lives, by exactly 4 bytes.**
This was not inferred — it was read out of the compiled binaries this pass with
`C:\VulkanSDK\1.4.350.0\Bin\spirv-dis.exe`. `Resources/ShadersSlang/build/spirv/rasterizer/rasterizer.fs_main.spv`
declares `%SceneUBO_std140` with `OpMemberDecorate ... Offset` values
`0, 32, 36, 40, 48, 64, 256, 272, 288, 304, 320, 336`. Slang compiles
`ConstantBuffer<T>` as **std140** (the emitted type is literally named
`SceneUBO_std140`; `Scripts/Windows/compile-slang-shaders.ps1:191` passes no
`-fvk-use-scalar-layout`), so the `uint/float/uint` run at bytes 32/36/40 is
followed by 4 bytes of std140 padding before `cascadeSplits` at 48. The host
struct (`Src/GraphicsEngineVulkan/renderer/SceneUBO.hpp:29-52`) has no such pad,
and this project does **not** define `GLM_FORCE_DEFAULT_ALIGNED_GENTYPES`
(`Src/GraphicsEngineVulkan/CMakeLists.txt:51` sets only
`GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS`), so `glm::vec4`/`glm::mat4`
carry alignment 4 and `cascadeSplits` lands at 44. Consequence: the shader's
`cascadeSplits.x` reads the host's `cascadeSplits.y`, all three
`cascadeLightSpaceMatrices` are read 4 bytes off (i.e. garbage light-space
matrices), `view_dir`/`cam_pos`/every cloud vec4 are shifted, and
`cloudParameters` at 336..351 reads past the end of a buffer created and bound
at `sizeof(SceneUBO)` (`VulkanRenderer.cpp:1492-1493`, `:716`, `:723`). Five
shaders bind this block: `rasterizer.fs_main`, `deferred.lighting_fs_main`,
`clouds.clouds_main`, `path_tracing.path_tracing_main`,
`raytrace.rchit.rchit_main`.

**Why nothing caught it: `SceneUBO` is the only shared host/device struct with
no layout test, and it is the only one that mismatches.** An exhaustive sweep of
every laid-out block in `Resources/ShadersSlang/build/spirv/**/*.spv` this pass
found 13 distinct emitted types; every other one agrees with its host twin, and
most are already pinned by `Test/commit/VulkanEngine/pushConstantSuite.cpp`
(`PushConstantRasterizerUnit:36-104`, `PushConstantPathTracingUnit:110`,
`PushConstantPostUnit:125`, `PushConstantRaytracingUnit:137`,
`ObjMaterialLayoutUnit:151`). Verified agreeing this pass:
`ObjMaterial_natural` (0/12/24/36/48/60/64/68/72/76/80/84/92, `ArrayStride 100`
= `sizeof(ObjMaterial)`), `Vertex_natural` (0/12/24/36, `ArrayStride 44`),
`ObjectDescription_std430` (0/8/16/24/32, `ArrayStride 40`),
`PushConstantRasterizer_std430` (0/64/112), `GlobalUBO_std140` and
`CameraUBO_std140` (both 0/64/128/192 — all-`mat4` structs cannot drift),
`ShadowPushConstants_std430`, `DirectionalLightData_std140` (0/16). The rule the
sweep exposes is simple: a shared struct is safe exactly when every member is
16-byte-sized; `SceneUBO` is the only one that interleaves scalars, and it is
the only one that is wrong.

Tasks 1 and 2 are ordered: **do task 1 first**, then task 2 turns its
hand-written expectations into a gate that reads the `.spv` itself. Tasks 3–5
are independent of both. Every task in this batch is verifiable **without a
GPU** — the twelve `- [b]` entries above are still blocked on host GPU golden
verification, and task 1's oracle is deliberately a CPU `offsetof` test rather
than a rendered image. The actionable queue was empty when this batch was
written.

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **`skybox.slang:6-13`'s `CameraUBO` is a verbatim
re-declaration of `common/scene_types.slang:75-82`'s `GlobalUBO`** — real
duplication, but both emit identical offsets (all `mat4`), and deleting it means
editing a `.slang` and regenerating + committing every SPIR-V/WGSL artifact
(`BuildIntegrity.CompiledShadersAreNotOlderThanSharedIncludes`,
`CheckedInWgslIsNotOlderThanItsSlangSource`) for a zero-behaviour change; fold
it into the next shader-touching task instead; **`PushConstantSkyBox`
(`skybox.slang:18-21`) has no host struct at all** — `SkyBox.cpp:334,411` pushes
a bare `sizeof(uint32_t)`, which is correct at offset 0 and needs an allowlist
entry in task 2, not a fix; **`Frustum.cpp`** — read end to end this pass, the
Gribb-Hartmann extraction, the `GLM_FORCE_DEPTH_ZERO_TO_ONE` near-plane
derivation and the degenerate-plane guards are all correct and documented in
place; **18 source files carrying a UTF-8 BOM** — re-checked, still cosmetic,
still diff noise (batch VI rejected this for the same reason).

## 2026-08-02 batch VIII — planner (a key that never stops being held once ImGui takes focus, ten throwing `std::filesystem` calls in a `-fno-exceptions` build, a Rust test suite this repo's CI never runs, a skybox that hands `vkCreatePipelineLayout` a null set layout, four copies of the same "walk up for Resources/" search)

**The headline is the input path: hold `W`, click an ImGui widget, release `W` — the
camera keeps flying forever.** `Window::key_callback`
(`Src/GraphicsEngineVulkan/window/Window.cpp:122`) returns on
`ImGui::GetIO().WantCaptureKeyboard` **before** reaching
`handle_key_callback`, so the `GLFW_RELEASE` never clears
`keys[GLFW_KEY_W]` and `Camera::key_control` keeps translating every frame.
The same shape kills the right-drag look mode:
`handle_mouse_button_callback`
(`Src/shared/frontend/WindowInputCallbacks.ixx:77`) returns on
`WantCaptureMouse` before `glfwSetCursorPosCallback(window, nullptr)`, so
releasing the right button over a panel leaves the cursor callback installed.
`WindowInputUnit.ImGuiCaptureGateSwallowsInput`
(`Test/commit/VulkanEngine/frontendInputSuite.cpp:111`) pins the *swallow*
direction only — the release direction is the half that was never asserted,
and it is the half that leaves the camera moving.

The second finding is systematic rather than local. `cmake/ProjectOptions.cmake`
compiles everything with `-fno-exceptions` / `/EHs-`, and
`Src/shared/util/FileReader.ixx:13` already writes the rule down — "The
`error_code` overload is REQUIRED, not stylistic". A grep for
`std::filesystem::` across `Src/` this pass found **ten calls that use the
throwing overload anyway**, in four files. Under these flags a
`filesystem_error` is not an exception, it is `std::terminate` with no log
line. The `recursive_directory_iterator` one is the subtle member of the set:
passing `ec` to the *constructor* makes construction non-throwing, but the
range-for's `operator++` is still the throwing increment.

Tasks 1–4 are independent. Task 5 depends on task 2 (so the shared resolver is
exception-free by construction) and should land after task 4 (which fixes the
skybox crash; task 5 then folds the skybox's path lookup into the resolver).
Every task is verifiable **without a GPU** except task 4's end-to-end check,
which is a host launch rather than a golden. The actionable queue was empty
when this batch was written.

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **`Resources/ShadersSlang/.../bloom.wgsl`'s blur kernel**
— read end to end, it derives its texel size from `textureDimensions` rather
than a hard-coded constant, so the half-res chain is correct at every extent;
**`asset/hdr.rs`** — the whole RGBE decoder was audited against Radiance's
`freadcolrs`/`oldreadcolrs`, including the `MAX_PIXELS` cap, the `-8` mantissa
shift and every RLE overrun path, and its nine tests already cover them;
**`Clouds::dispatchNoiseGeneration`'s grid** — already pinned by
`CloudDispatch.hpp`'s `static_assert` plus
`BuildIntegrity.CloudDispatchGridsMatchTheShaderWorkgroupSizes`;
**`VulkanDevice::create_pipeline_cache`** — the stale-blob retry and the
"continue without a cache" fallback are both already correct;
**`keyframe_lerp_indices`'s linear keyframe scan**
(`crates/webgpu_renderer/src/render/animation.rs:22`) — genuinely O(n) per
channel per frame where `partition_point` would be O(log n), but the morph
loop already `continue`s before allocating for non-matching primitives, so the
measured cost is a few thousand comparisons per frame on any realistic scene;
not worth a task until something measures it.

## 2026-08-02 batch IX — planner (refactor: a sixth "one rule, five hand-rolled copies" — framebuffer creation; a positional 9-field aggregate spelled out three times; 15 accessors on the two shared scene structs that nothing calls)

**Every task in this batch is verifiable with no GPU**, deliberately: the `- [b]`
entries above are still blocked on host GPU golden verification. Every
`file:line` below was read out of the tree this pass, and every dead-code claim
was confirmed by a whole-tree grep over `Src/` + `Test/`. The actionable queue
was empty when this batch was written (the only remaining `- [ ]`-shaped entries
in this file are `- [b]`).

**The headline is that framebuffer creation is the one remaining member of the
`buildAttachmentDescription` / `fullExtentViewport` family that never got its
helper.** Five files each spell out the same six-field `vk::FramebufferCreateInfo`
block by hand: `Rasterizer.cpp:236-243`, `PostStage.cpp:304-311`,
`DeferredRasterizer.cpp:362-368`, `SkyBox.cpp:321-327` and
`CascadedShadowMap.cpp:210-216`. The five copies have already drifted in three
ways that a shared builder removes by construction: **SkyBox hard-codes
`attachmentCount = 2`** rather than deriving it from `attachments.size()`, so
adding a third attachment to its `std::array` there is silently ignored;
**DeferredRasterizer hoists the create-info out of the loop** and mutates the
`attachments` array it borrows through `pAttachments` on every iteration — correct
today, but it is precisely the borrowed-pointer shape batch XVII found dangling in
swapchain creation; and **PostStage alone uses the two-out-param
`createFramebuffer(&info, nullptr, &framebuffers[i])` overload** where the other
four use the `ResultValue` one. This is the same finding class as `a3b42dfc`
(depth aspect → `FormatHelper.hpp`), `f97712f4` (six shader-stage blocks →
`ShaderStagePair`), `29081658` (`ViewportHelper.hpp`) and `36937517`
(`RenderPassHelper.hpp`).

**Second: `Camera.cpp` constructs the same nine-member `CameraControllerState`
aggregate three times, positionally.** `key_control` (`:63-71`), `mouse_control`
(`:79-87`) and `update` (`:110-118`) each list `position, front, world_up, right,
up, yaw, pitch, movement_speed, turn_speed` in order, with no designated
initializers. Because the four `glm::vec3&` members are mutually interchangeable
to the compiler and so are the two leading `float&` members
(`CameraController.ixx:15-26`), **reordering the struct silently rebinds
`right`↔`up` or `yaw`↔`pitch` at all three call sites with no diagnostic** — the
camera would just start rolling instead of yawing. `apply_keyboard_input`
(`CameraController.ixx:39`) additionally takes a bare `const bool *keys` and
indexes it up to `GLFW_KEY_E` (69) with no length: `Window::get_keys()`
(`Window.ixx:24`) hands out `input_state.keys.data()` off a
`std::array<bool, 1024>` (`WindowInputState.hpp:7,11`) while
`cameraSceneConfigSuite.cpp:108` passes a `std::array<bool, GLFW_KEY_LAST + 1>`,
so two different array sizes already flow into the same unchecked pointer.

**Third, the dead code: 15 of the 19 accessors on the two shared scene structs
have no caller anywhere in `Src/` or `Test/`.** Grep-confirmed at exactly one
occurrence each (their own definition): `ObjMaterial::get_{ambient,diffuse,
specular,transmittance,emission,shininess,ior,dissolve,illum,alphaCutoff,
uv_scale,uv_offset}` (`Src/shared/scene/ObjMaterial.hpp:68-82`) and
`Vertex::get_{normal,color,tex_coors}` (`Src/shared/scene/Vertex.hpp:31-33`).
Only `ObjMaterial::get_textureID` and `Vertex::get_position` are live. Every
field is public, so each accessor is pure redundancy over the direct member
access the rest of the engine already uses.

Ordering: all three are independent and touch disjoint files.

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **the twelve hand-rolled `vk::ImageMemoryBarrier` blocks**
(`FrameCapture.ixx:90,122`, `PathTracing.cpp:62,94,148`, `Raytracing.cpp:92,123`,
`VulkanRenderer.cpp:882,914`, `SkyBox.cpp:147`, `Texture.cpp:288`,
`VulkanImage.cpp:124`) — the same "seven fields a copy-paste drops" shape as task
1, but the `- [b]` cloud-barrier entry above already owns two of those lines, and
a helper task would collide with it; pick this up once that unblocks;
**`ForwardRenderer::new` at 583 lines** (`crates/webgpu_renderer/src/render/forward.rs:364-947`)
— real, and the file already has the `create_*_pipeline` free-function shape to
extract into, but every extracted unit needs a live adapter to exercise, so the
refactor would ship with compile-only verification; **the accessor sweep across
`Src/**/*.ixx`** — re-run this pass over every `get*`/`is*`/`has*`/`set*`
declaration, and apart from the fifteen in task 3 every one has a live caller
(`getComputeQueue`→`Clouds.cpp:241`, `getPresentationQueue`→`VulkanRenderer.cpp:605`,
`getGBuffer{Normal,Albedo,Material}`→`VulkanRenderer.cpp:1587-1589`,
`getMipLevel`→`Model.cpp:72`, `getVertexCount`→`ASManager.cpp:496`,
`isDoubleSided`→`Scene.ixx:120`, …), so batches VI–VIII drained this seam;
**pinning `forward.wgsl`'s binding indices against `forward.rs`'s hand-built
layout** — the `histogram_constants.rs` gate shape would fit, but wgpu validates
the bind-group layout against the shader at pipeline-creation time and errors, so
this drift is loud, not silent; **`Texture::createDefaultTexture`
(`Texture.cpp:195-202`) discarding `uploadRgba`'s `bool`** — the one caller path
already tolerates a texture-less material, and making it propagate is a
behaviour change, not a refactor.

### C++ Vulkan engine

  **Context:** Deleting a member of a `#ifdef __cplusplus` host/device struct is
  only safe because these are member **functions**, not data members — the
  layout the shaders and the BLAS/vertex-input descriptions depend on
  (`ASManager.cpp:495`, `Vertex.cpp:40-49`) is untouched. If any `offsetof` or
  `sizeof` assertion moves, stop: that is a real finding, not a number to
  update.

## 2026-08-02 batch X — planner (a left click that cancels right-button look mode, a `map_Kd` backslash rule only one of the two OBJ loaders applies, a window whose creation failure nothing checks, two more dead accessors, a mip filter that always rounds down)

**Every task in this batch is verifiable with no GPU**, deliberately: the
fifteen `- [b]` entries above are still blocked on host GPU golden verification.
Tasks 1, 2 and 4 land device-free unit tests in suites that already run in
Windows CI (`WindowInputUnit`, `ObjParseUnit`); task 5 is `cargo test` on a pure
function that already has its own `#[cfg(test)]` module; task 3 is a
compile-verified dead-state deletion plus a one-line guard. Every `file:line`
below was read out of the tree this pass, and every dead-code claim was
confirmed by a whole-tree grep over `Src/` + `Test/`. The actionable queue was
empty when this batch was written.

**The headline is that pressing ANY button other than the right one ends
right-button look mode.** `handle_mouse_button_callback`
(`Src/shared/frontend/WindowInputCallbacks.ixx:87-103`) uses one predicate for
two decisions: `should_capture_cursor(button, action)` answers "should I START
look mode", and everything else falls into an `else` branch that does
`mouse_first_moved = true; glfwSetCursorPosCallback(window, nullptr)`. A left
press while the right button is still held is not a right-button press, so it
takes the `else` branch and tears the cursor callback down mid-orbit; the camera
stops turning until the user releases and re-presses the right button. The
existing test even pins the input half of it —
`CursorCaptureDecisionIgnoresImGuiState`
(`Test/commit/VulkanEngine/frontendInputSuite.cpp:178-186`) asserts
`should_capture_cursor(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS) == false`, which is
correct for "start" and is exactly what the `else` branch then misreads as
"stop". Same shape as the RELEASE-under-ImGui-capture bug `c37394b4` fixed one
callback over: one predicate asked to answer two different questions.

**Second: the two OBJ loaders do not resolve `map_Kd` the same way, and
`docs/model-loading.md:121` claims they do.** The Rust side normalises `\` to
`/` before resolving (`crates/webgpu_renderer/src/asset/obj_to_gltf.rs:132-147`,
`values.last().map(|name| name.replace('\\', "/"))`); the C++ side takes
`mp->diffuse_texname` verbatim (`Src/GraphicsEngineVulkan/scene/ObjLoader.cpp:181-192`)
and tinyobjloader does not translate separators either (grep-checked
`ExternalLib/TINY_OBJ_LOADER/tiny_obj_loader.h` — its only backslash handling is
the Windows UNC prefix at `:1284-1299`). So a Windows-authored
`map_Kd textures\wall.png` resolves in the Rust renderer on every platform and
silently degrades to the white default texture in the C++ one on Linux — i.e.
in the only CI lane that runs on every push. The doc's own bullet
(`docs/model-loading.md:144-146`) already records the normalisation as
"a deliberate deviation", which contradicts the headline sentence six lines
earlier. **No shipped asset under `Resources/Models` uses a backslash**
(grep-confirmed this pass), so nothing is broken today: this is parity plus the
doc fix, not a live defect. The same call site has a second, unrelated gap —
`Kataglyphis::Shared::getBaseDir` (`Src/shared/util/FileReader.ixx:73-79`)
returns `""` for a path with no separator, and `ObjLoader.cpp:191-192` then
builds `"" + "/" + name`, so an OBJ opened by bare filename resolves its
textures against the **filesystem root**.

**Third, the dead state: `Window` keeps two members nothing reads and throws
away the one value that says whether it worked.** `window_width` /
`window_height` (`Window.ixx:34`) are read exactly once, by `glfwCreateWindow`
(`Window.cpp:64-65`); `framebuffer_size_callback` (`Window.cpp:107-113`) keeps
writing the resized values into them and no reader exists anywhere in `Src/` or
`Test/` (grep-confirmed — every consumer calls `glfwGetFramebufferSize`
directly: `VulkanRenderer.cpp:646,648`, `VulkanSwapChain.cpp:52`). Separately,
`initialize()` returns `int` and **both constructors discard it**
(`Window.cpp:33,45`), so a failed `glfwInit`/`glfwCreateWindow` leaves
`main_window == nullptr` and `App::run()` (`App.cpp:35-41`) walks straight into
`glfwCreateWindowSurface(instance, nullptr, …)` (`VulkanRenderer.cpp:1214-1220`)
— GLFW's `assert(window != NULL)` is compiled out in Release, so the release
build crashes with no diagnostic while the message it should have printed
("GLFW Window creation failed!") is already sitting in `initialize()`.

**Fourth, two more dead accessors — which contradicts batch IX's claim that the
`Src/**/*.ixx` accessor sweep was drained.** `GpuTimingSubsystem::passRecordedMask`
(`GpuTimingSubsystem.ixx:239`) and `sliceWasRecorded` (`:249-252`) have exactly
one occurrence each in the whole tree (their own definitions). `passRecordedMask`
additionally indexes `gpu_timing_pass_mask[imageIndex]` with no bounds check
while both of its siblings (`setPassRecordedMask` at `:245`, `sliceWasRecorded`
at `:251`) check — and the vector is empty whenever `create()` took its
unsupported-timestamps early return (`:83-88`), so the one accessor without a
guard is the one that would fire.

Ordering: all five are independent and touch disjoint files. Task 2 edits
`docs/model-loading.md`; nothing else in this batch touches docs.

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **`Texture::createImage` not recording `in_mip_levels`
into `mip_levels`, so `createTextureSampler` builds `maxLod = 0` for Clouds,
SkyBox and CascadedShadowMap** — re-confirmed a third time and re-rejected for
the reason batches IV and V gave, with the extra note that all three of those
images are created with exactly one mip level, so `maxLod = 0` is the correct
value by accident and no sampling behaviour changes; **`BuildIntegrity.EveryCpuSuiteIsInTheWindowsCiFilter`**
— re-ran both of its parsers by hand this pass (45 defined suites vs 43 filter
entries plus `GoldenRender`/`Integration`); it is GREEN, unlike when batch VI
found it red, so do not re-file it; **the shader manifest vs the tree** —
cross-checked `Resources/ShadersSlang/shader-manifest.json` against every
`.slang` on disk: no manifest row names a missing file, and the eleven
unreferenced sources are all `import`-only (`common/*.slang`,
`raytracing/rt_types.slang`) or the `spike/` scratch pair, which is what
`EveryShaderSourceHasCompiledBinary` already encodes; **`sceneConfig::getModelMatrix()`'s
`#if NDEBUG` split** (`SceneConfig.cpp:116-131`) — both branches are
`glm::scale(identity, vec3(1,1,1))`, i.e. the same identity matrix, so the
conditional is dead; real, but a three-line deletion with no behavioural or
test consequence, so it is not worth an executor session on its own — fold it
into the next `SceneConfig` change; **`Scene::reloadModel` not calling
`resolveModelPath`** (`Scene.cpp:166-184`) unlike `loadAdditionalModel` — looks
like the same class as task 2, but its only caller already resolves
(`VulkanRenderer.cpp:390-394`), so the asymmetry is cosmetic;
**`DescriptorSetGroup::writeImageArray` re-calling `findBinding` and
dereferencing without a null check** (`DescriptorSetGroup.cpp:194-195`) —
`beginWrite` at `:192` already returned false if that lookup fails, so the
pointer cannot be null; a redundant lookup, not a defect.

### C++ Vulkan engine

### Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate`)

## 2026-08-02 batch XI — planner (a frame-sync counter that erases itself one line after it is computed, a glTF loader that keeps the previous parse's mesh ranges, eleven raw-handle log lines, a dead overload, a conditional whose two branches are identical)

**The headline is a live regression that stops the engine rendering, shipped
2026-08-01 in `62e56684`.** `FrameSync::create`
(`Src/GraphicsEngineVulkan/renderer/FrameSync.ixx:33-43`) computes
`frame_sync_count = min(MAX_FRAME_DRAWS, imageCount)` on line 35 and then calls
`cleanUp(logicalDevice)` on line 38 — and `cleanUp` now ends with
`frame_sync_count = 0; current_frame = 0;` (`:117-118`), which `62e56684` added
so the failure paths could reuse it. The ordering was never revisited: the
count is wiped immediately, `image_available.resize(frame_sync_count)` and
`in_flight_fences.resize(frame_sync_count)` resize to **0**, and the
`for (i = 0; i < frame_sync_count; i++)` loop that creates the per-frame
semaphores and fences never executes. `VulkanRenderer::drawFrame` then takes
its `if (frameSync.frameSyncCount() == 0)` early return
(`VulkanRenderer.cpp:443`) on every single frame. `git show 62e56684^` confirms
the `cleanUp` call already sat above the resizes before that commit — it was
harmless only because `cleanUp` did not touch the counter then. **This survived
a full day of executor sessions precisely because host GPU golden verification
is the blocked path** (the fifteen `- [b]` entries above): nothing in the CPU
suites calls `create()`, so `FrameSyncUnit`'s three tests all still pass.

**Second, `GltfLoader::parseCpu` does not reset `meshRanges`, and
`sliceMeshRange` has no bounds guard to survive that.** `parseCpu`
(`GltfLoader.cpp:405-411`) clears `vertices`, `indices`, `materials`,
`materialIndex` and `textureImages` — five of the six arrays `adoptParsed`
moves (`:42-50`). `ObjLoader::parseCpu` clears all six, `meshRanges` included,
under an explicit comment (`ObjLoader.cpp:43-49`). So a second `parseCpu` on
one `GltfLoader` appends the new document's ranges after the previous
document's, and `uploadParsed` (`:99-109`) then feeds every one of them to
`sliceMeshRange` (`MeshRange.ixx:43-63`), which does
`vertices.begin() + range.vertexBase` and `indices[range.indexStart + i]` with
no size check — an out-of-bounds read, not a diagnosable error. Today every
`GltfLoader` is constructed fresh per parse (`AsyncModelParse.ixx:56`,
`Scene.cpp:37`, `:116`), so this is latent rather than firing; `loadModel`
(`:33-40`) is the reachable second-call path the moment anyone reuses an
instance, which is exactly what `ObjLoader`'s comment says it was written to
survive.

**Third, eleven `spdlog::info` lines print raw Vulkan handles.** Grep-confirmed
across exactly three files: `Rasterizer.cpp:113,146,243,348`,
`DeferredRasterizer.cpp:119,166,330,355,377`, `CascadedShadowMap.cpp:227,436`.
They fire once per swapchain image at startup **and again on every window
resize** (`recreateSwapChain` → `destroyFramebuffers` → `recreateFrameResources`),
so an ordinary resize on a triple-buffered swapchain emits a dozen
`0x7f…`-style lines at info level. No other stage does this — `PostStage`,
`SkyBox` and `Clouds` create and destroy the same objects silently — and a
handle address is useful only to a debugger, never to a user reading `logs/`.

**Fourth and fifth, two pieces of confirmed-dead code.**
`VulkanBufferManager` declares `createBufferAndUploadVectorOnDevice` twice
(`VulkanBufferManager.ixx:40-48` const-ref, `:50-57` non-const-ref); the
non-const body (`:111-128`) does nothing but `static_cast` to
`const std::vector<T>&` and call the other one. All five call sites
(`Mesh.ixx:130`, `CascadedShadowMap.cpp:320`, `VulkanRenderer.cpp:1315`,
`:1323`, `ASManager.cpp:273`) bind fine to the const overload. And
`sceneConfig::getModelMatrix` (`SceneConfig.cpp:116-131`) has an `#if NDEBUG`
split whose two branches are byte-identical `glm::scale(identity, vec3(1,1,1))`
— batch X found this, rejected it as too small to stand alone, and asked for it
to be folded into the next `SceneConfig` change; this is that change, paired
with the stale claim in `Camera.cpp:44-48` that "cascade splits are computed as
farPlane * (i / numCascades)", which `computeCascadeDataInto` stopped doing when
`shadowDistance` and the practical split scheme landed
(`CascadedShadowMapMath.cpp:124-161` — `shadowFar = min(shadowDistance, farPlane)`,
so the comment's 4000 → 1333 arithmetic describes code that no longer exists).

**Every task in this batch is verifiable with no GPU**, deliberately, for the
same reason batch X gave. Tasks 1, 2 and 5 land device-free unit tests in
suites that already run in Windows CI (`FrameSyncUnit`, `GltfParseUnit`,
`MeshRangeSlice`, `CameraSceneConfigUnit`); task 3 adds a grep-based
`BuildIntegrity` gate in the shape of the existing
`EngineSourcesUseNonThrowingFilesystemOverloads`; task 4 is a compile-verified
deletion. Every `file:line` below was read out of the tree this pass. The
actionable queue was empty when this batch was written.

Ordering: **task 1 first, and on its own** — it is a live rendering outage and
the other four are cleanups. Tasks 2–5 are independent and touch disjoint
files.

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **`GpuTimingSubsystem::create`** — checked for the same
assign-then-`destroy()` ordering hazard as task 1; it calls `destroy(device)`
FIRST (`GpuTimingSubsystem.ixx:73`) and `destroy` touches no counters, so it is
correct; **`PostStage`/`Rasterizer`/`DeferredRasterizer`/`SkyBox`
`recreateFrameResources` leaking framebuffers** — traced
`VulkanRenderer.cpp:664-689`; all four `destroyFramebuffers()` calls run before
the corresponding `recreateFrameResources()`, so nothing leaks;
**`FrameCapture::invalidateFence` leaving `pending == true` across a swapchain
recreate** — real, but `buffer_size` only ever grows and `width`/`height` are
the *old* (already-fitting) extent, so `take()`'s memcpy stays in bounds; the
class comment already states this is intended; **`Texture::uploadRgba`'s
`mip_levels` vs `createImage`'s `in_mip_levels`** — the third-time-rejected
candidate from batches IV/V/X; `uploadRgba` sets the member before calling
`createImage`, so the sampler's `maxLod` is right on this path, and the three
single-mip callers are correct by accident as batch X recorded;
**`Shared::getBaseDir` returning `""` for a bare filename** (batch X's
second-half finding) — already fixed: `resolveObjTexturePath`
(`ObjLoader.ixx:20-28`) treats an empty base dir as `"."`, and
`ObjParseUnit.EmptyBaseDirStaysRelative` (`objParseSuite.cpp:403-411`) pins it;
**`GUISceneSharedVars::shadow_distance` / `cascade_split_lambda` having no GUI
control** — genuinely unexposed (`GUI.cpp:185-201` offers only resolution,
cascade count, PCF radius and intensity) while both are plumbed through to the
cascade math (`VulkanRenderer.cpp:198-199`) and documented as knobs worth
turning; left untasked because an ImGui panel addition has no device-free
oracle, so it cannot be verified while GPU goldens are blocked — pick it up in
the first batch after that unblocks; **`extractImageBytes` counting a padding
byte at `b64[len-2]` without checking `b64[len-1]`** (`GltfLoader.cpp:228-231`)
— only misreads input that is already invalid base64, and the length guard
above it makes the result harmless.

### C++ Vulkan engine

### Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate`)

## 2026-08-02 batch XII — planner (refactor: a sixth "one rule, five hand-rolled copies" — render-pass begin; four copies of the extension/layer lookup that already produced one shipped bug; a descriptor write that looks its binding up twice and dereferences the second one unchecked)

**Every task in this batch is verifiable with no GPU**, deliberately: the `- [b]`
entries above are still blocked on host GPU golden verification in this RDP
session. Tasks 1 and 2 land device-free `constexpr`/pure helpers with new gtest
suites that run in the container CPU lane; task 3 is a compile-verified
restructure of private helpers with no behaviour change. Every `file:line`
below was read out of the tree this pass, and every duplication count was
confirmed by a whole-tree grep over `Src/` + `Test/`. The actionable queue was
empty when this batch was written (the only `- [ ]`-shaped entries in this file
are `- [b]`).

**The headline is that `vk::RenderPassBeginInfo` is the sixth member of the
`buildAttachmentDescription` / `fullExtentViewport` / `buildFramebufferCreateInfo`
family, and it has drifted in exactly the way that family exists to prevent.**
Five files each spell out the same six-field block by hand:
`Rasterizer.cpp:73-85`, `PostStage.cpp:68-81`, `DeferredRasterizer.cpp:381-395`,
`SkyBox.cpp:412-423` and `CascadedShadowMap.cpp:461-471`. All five set
`renderArea.offset = {0, 0}` and a full-extent `renderArea.extent`, and all five
are followed immediately by `setFullExtentViewportAndScissor` on the same
extent. **Two of the five hard-code `clearValueCount` instead of deriving it**
— `SkyBox.cpp:422` writes `clearValueCount = 2` next to a two-element
`std::array` and `CascadedShadowMap.cpp:469` writes `clearValueCount = 1` — which
is byte-for-byte the bug `buildFramebufferCreateInfo` was introduced to kill
(`SkyBox`'s `attachmentCount = 2`, pinned today by
`FramebufferHelperUnit.AttachmentCountIsDerivedFromTheSpan`). Two others
(`Rasterizer.cpp:73`, `PostStage.cpp:68`) declare the struct without `{}` while
the other three value-initialize.

**Second: "is this name in the list vulkan-hpp just enumerated" is written four
times, and one of the four already shipped a bug.** `VulkanInstance.cpp:91-102`
(layers), `VulkanInstance.cpp:113-127` (instance extensions),
`VulkanDevice.cpp:422-427` (the `isExtensionSupported` lambda) and
`VulkanDevice.cpp:659-670` (`check_device_extension_support`) are four
independent nested loops around `strcmp(..., props.{extension,layer}Name) == 0`.
The 2026-07-23 deep-review pass fixed `check_instance_extension_support`'s
`strcmp(...) != 0 != 0` — which parsed as `(strcmp != 0) != 0` and made the
function effectively always return true — in **one** of those four copies; the
other three were correct by luck, not by construction. `check_device_extension_support`
also carries an `extensions.empty()` early-return the other three lack, and
`check_instance_extension_support` still takes its input as a mutable
`std::vector<const char *> *` (`VulkanInstance.ixx:27`) where the body only
iterates it.

**Third: `DescriptorSetGroup::writeImageArray` looks its binding up twice and
dereferences the second result without a null check.** `beginWrite`
(`DescriptorSetGroup.cpp:136-149`) already calls `findBinding` and returns false
when it misses, but `writeImageArray` (`:187-207`) throws that away, calls
`findBinding(binding)` again at `:194` and reads `layout_binding->descriptorCount`
at `:195` with no guard. It is unreachable today only because the first lookup
succeeded — the deref is a static-analysis finding waiting to be re-derived, and
the second lookup is a second linear scan. `checkWritePreconditions` (`:127-134`)
has exactly one caller, `beginWrite`. Grep-confirmed: `findBinding`,
`checkWritePreconditions` and `beginWrite` have zero callers outside
`DescriptorSetGroup.cpp`.

Ordering: all three are independent and touch disjoint files.

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **the twelve hand-rolled `vk::ImageMemoryBarrier`
blocks** — still owned by the `- [b]` cloud-barrier entry above, unchanged since
batch IX rejected it; pick it up once host GPU verification unblocks;
**`Src/KomputePlayground`** — `KATAGLYPHIS_BUILD_KOMPUTE_PLAYGROUND` is OFF by
default (`Src/CMakeLists.txt:103`) and its `CMakeLists.txt` has the entire
kompute acquisition block commented out while still linking `kompute::kompute`,
so turning the option ON cannot configure; and `src/main.cpp:40` throws
`std::runtime_error` with no `<stdexcept>` include. Real, but "repair it" vs
"delete it" is an owner decision, not an executor one — raise it, do not task
it; **`FormatHelper.hpp:5`'s unused `#include <stdexcept>`** in a
`-fno-exceptions` build and **the ~25 redundant `= false` assignments on the
value-initialized `vk::PhysicalDevice*Features` structs**
(`VulkanDevice.cpp:331-362`) — both genuine noise, both too small to spend a
task on alone; fold them into whichever of the above touches those files;
**batching the four `updateDescriptorSets(1, &write, ...)` calls in
`DescriptorSetGroup`** — one vkUpdateDescriptorSets per descriptor is real, but
it is initialization-time only and the API change would ripple into every write
call site for no measurable gain; **the Rust `render/{bloom,ssao,tonemap,
overlay,gpu_occlusion}.rs` modules having no tests** — every one of them needs a
live wgpu adapter to exercise, so a coverage task there would ship
compile-only verification (`obj_to_gltf.rs` looked untested by inline
`#[test]` count but has a 650-line integration suite at
`crates/webgpu_renderer/tests/obj_to_gltf.rs`; do not re-flag it).

### C++ Vulkan engine


## 2026-08-02 batch XIII — planner (the Slang port of `ibl.wgsl` dropped an entry point the Rust code still creates a pipeline for, and the GGX importance sampling under it; a frame capture that assumes every swapchain format is 4 bytes; a G-buffer format list written twice)

The actionable queue was empty when this batch was written (0 `- [ ]`, 15
`- [b]` across the whole file). Every `file:line` below was read out of the tree
this pass.

**The headline is a shipped regression in `ExternalLib/Kataglyphis-RustProjectTemplate`,
introduced by `2a4ae68 "Replace hand-written WGSL shaders with Slang-emitted
output"`.** Comparing every generated WGSL against its pre-Slang predecessor
(`git show 2a4ae68^:crates/webgpu_renderer/src/shaders/<name>.wgsl`), nine of
the ten kept exactly their old entry-point count. `ibl.wgsl` went **6 → 5**:

| shader | lines before → after | entry points before → after |
| --- | --- | --- |
| forward | 613 → 692 | 5 → 5 |
| bloom | 60 → 93 | 4 → 4 |
| ssao | 137 → 217 | 3 → 3 |
| occlusion_bbox | 77 → 37 | 2 → 2 |
| **ibl** | **312 → 254** | **6 → 5** |

The one that vanished is `fs_downsample_cube`, and
`crates/webgpu_renderer/src/render/ibl.rs:321` still does
`downsample_cube: make("fs_downsample_cube", CUBE_FORMAT, "ibl_downsample_cube")`
inside `Precompute::new`, against a module built from
`include_str!("../shaders/ibl.wgsl")` (`:236`). `grep -c downsample
src/shaders/ibl.wgsl` = **0**. So constructing `Precompute` — which
`IblEnvironment::bake` (`forward.rs:1172`) does on every environment bake —
names an entry point that does not exist in the module.

Under that, three more things were lost in the same rewrite, all of them
load-bearing and all of them documented as such in the deleted comments:

1. **`fs_prefilter` reads `roughness` and never uses it.** `ibl.slang:93` binds
   `float roughness = params.face_roughness_samples_mip.y;` and lines 100-114
   never mention it again — confirmed in the emitted WGSL, where
   `fs_prefilter` (`ibl.wgsl:169-200`) contains no `.y` read at all. `tangent`
   (`:96`) and `bitangent` (`:97`) are likewise dead: `h` is built in a local
   frame and then used directly against `dir` in world space. And
   `hammersley` is hard-coded to `float2(float(i)/float(sampleCount), 0.0) //
   simplified` (`:102`, `:129`), so `phi` is always 0 and all N samples lie on
   one arc. Net: `ibl.rs:596` computes a per-mip roughness, packs it into the
   uniform, and the shader throws it away — **all five mips of the prefiltered
   cube are the same convolution.**
2. **The equirect source is sampled, not loaded.** `ibl.slang:60` is
   `srcEquirect.Sample(srcSampler, uv)`. The bind-group layout at
   `ibl.rs:245-256` declares binding 1 as
   `TextureSampleType::Float { filterable: false }` with the comment "Not
   filterable, and not sampled: `sample_equirect` uses textureLoad" — the
   function `sample_equirect` no longer exists. The old shader hand-rolled a
   bilinear fetch that **wrapped** longitude, specifically so ClampToEdge would
   not darken a one-texel seam down the -X meridian of every derived map.
3. **`fs_irradiance` went from midpoint to left-endpoint quadrature** and from
   128×64 to 64×32 steps (`ibl.slang:72-79`: `phi = float(phi_i) * phiStep`).
   The deleted comment states the measured cost of exactly this: a constant
   environment convolves to ~0.997 of itself instead of ~0.9997.

**The tests that would have caught (1) already exist and are correct.**
`crates/webgpu_renderer/tests/ibl.rs:184`
`higher_roughness_prefilter_mips_are_strictly_blurrier` asserts variance falls
with every mip and that mip 4 is under 25% of mip 0's variance — it cannot pass
against a roughness-independent prefilter. It never ran, because every test in
that file opens with `let Ok(gpu) = GpuContext::new_headless() else {
eprintln!("SKIP: no GPU adapter available in this environment"); return; };`
and a skip is indistinguishable from a pass in the CI log. That is why tasks 1
and 3 below exist as separate items: one fixes the shader, the other makes the
absence of a GPU stop being silent.

Candidates found but NOT tasked this cycle (re-verify next pass): `Model::cleanUp`
(`Model.cpp:26-37`) still does not `meshes.clear()` while clearing every other
container, and `Scene::cleanUp` (`Scene.cpp:180`) clears nothing at all, so the
destructor re-walks already-released objects — safe only because every leaf
`cleanUp()` happens to be idempotent, an invariant nothing tests; `Model::addSampler`
(`Model.cpp:66-80`) still creates one `vk::Sampler` per texture differing only in
`maxLod`; `VulkanDevice.cpp:586/589/592` take `.value` off three
`getSurface*KHR` calls with no result check, which is the tail of batch III's
unchecked-results finding.

### C++ Vulkan engine


## 2026-08-02 batch XIV — planner (the same Slang port that broke `ibl` also dropped the forward pass's analytic ambient specular and two host-side strength controls; the irradiance quadrature it downgraded; the gate that would have caught all of it; six unchecked surface queries; one sampler per texture)

The actionable queue was empty when this batch was written (0 `- [ ]`, 15
`- [b]` across the whole file). Every `file:line` below was read out of the tree
this pass; the "before" side comes from
`git show 2a4ae68^:crates/webgpu_renderer/src/shaders/<name>.wgsl` inside
`ExternalLib/Kataglyphis-RustProjectTemplate`.

**Batch XIII found three semantic regressions in `ibl.slang` and closed two of
them. It stopped at the entry-point count, and that is why it missed the
bigger one.** Comparing *reachable functions* instead of entry points,
`forward.wgsl` lost two whole functions that its `.slang` source still defines:

| helper | in `forward.slang` | in emitted `forward.wgsl` | called by |
| --- | --- | --- | --- |
| `hemisphere_irradiance` | `:277` | `hemisphere_irradiance_0` (`:575`) | `:420` |
| `sky_radiance` | `:260` | **absent** | **nothing** |
| `env_brdf_approx` | `:283` | **absent** | **nothing** |

Slang dropped them because nothing calls them — they are dead in the source.
`grep -n "sky_radiance\|env_brdf_approx" Resources/ShadersSlang/forward/forward.slang`
returns only the two definitions, no call sites. `occlusion_bbox` was checked
the same way and is faithful (77 → 37 lines is comment loss only), so this is
`ibl` and `forward`, not a systematic emitter fault.

What the old `fs_main` did between the pre-Slang lines 572 and 605, and what
`forward.slang:407-421` does now:

1. **The analytic specular ambient is gone.** Old:
   `env_analytic = mix(sky_radiance(reflected, true), irradiance_analytic, roughness*roughness)`,
   then `specular_analytic = env_analytic * env_brdf_approx(f0, roughness, n_dot_v)`,
   `select`ed against the split-sum path on `env_enabled`. New: the `else`
   branch is `ambient = hemisphere_irradiance(n) * albedo.rgb * (1.0 - metallic)`
   and nothing else. For `metallic = 1` that is **identically zero** — with no
   IBL environment bound, a metal renders black except for direct and punctual
   light.
2. **`enabled_maxmip_intensity.z` (environment intensity) is never read.** The
   host writes it (`crates/webgpu_renderer/src/render/forward.rs:2619`,
   `[1.0, max_mip, intensity, 0.0]`); the shader reads `.x` at `:409` and `.y`
   at `:413` and stops. Old code multiplied both `irradiance_env` and
   `prefiltered` by it. The control is inert.
3. **`light_dir_ambient.w` (ambient strength) is never read.** Old:
   `ibl_strength = frame.light_dir_ambient.w` scaling the whole ambient term.
   `forward.slang:28` still documents the field as "w: ambient" and
   `forward.rs:335` still documents it as "scales both IBL paths alike"; the
   default is `0.35` (`forward.rs:931`). Also inert.
4. **The `(1 - k_s)` Fresnel factor on the diffuse ambient is gone.** Old:
   `diffuse_ibl = (1 - fresnel_schlick(n_dot_v, f0)) * (1 - metallic) * albedo * irradiance`.
   New drops the Fresnel term entirely.

The deleted comment on the old `select`-not-`mix` choice states the reason the
fallback path had to stay bit-exact: "every golden test in the suite depends on
it."

Candidates found but NOT tasked this cycle (re-verify next pass): `Model::cleanUp`
(`Src/GraphicsEngineVulkan/scene/Model.cpp:26-38`) still clears `modelTextures`
and `modelTextureSamplers` but not `meshes`, and `Scene::cleanUp`
(`Scene.cpp:186-189`) clears nothing, so `Scene::reloadModel` (`:166-169`) runs
`cleanUp()` and then destroys the models, re-walking every already-released
`Mesh` — safe only because each leaf `cleanUp()` happens to be idempotent, an
invariant nothing tests; `ssao.slang` and `bloom.slang` were not compared
function-by-function against their pre-Slang originals (both *grew*, so they are
lower risk, but task 3 below would settle them mechanically).

### Rust WebGPU renderer

### Cross-renderer

### C++ Vulkan engine

## 2026-08-03 batch — planner (refactor: the seventh and eighth members of the "one rule, five hand-rolled copies" family — render-pass creation and pipeline-layout creation, both of which SkyBox again hard-codes a count in; 22 generated Sphinx artifacts that `.gitignore` already claims to exclude but git still tracks)

The actionable queue was empty when this batch was written (0 `- [ ]`, 15
`- [b]` across the whole file). Every `file:line` below was read out of the tree
this pass; the git-state claims in task 2 come from
`git ls-files -i -c --exclude-standard` run against this checkout.

**Every task in this batch is verifiable with no GPU**, deliberately: the
fifteen `- [b]` entries above are still blocked. Tasks 1 and 3 land device-free
`constexpr` helpers with new gtest suites that run in the container CPU lane;
task 2 is a git-tracking + `.gitignore` change gated by a new Pester suite.

**The headline is that the `buildAttachmentDescription` /
`fullExtentViewport` / `buildFramebufferCreateInfo` / `buildRenderPassBeginInfo`
family has two more members that never got their helper, and SkyBox hard-codes a
count in both of them.** `SkyBox.cpp` now has *four* places where a literal count
sits next to the container it should have been derived from: the framebuffer
`attachmentCount = 2` (fixed by `4f799788`), the render-pass-begin
`clearValueCount = 2` (fixed by `c041b756`), and the two still open below —
`renderPassInfo.attachmentCount = 2` (`SkyBox.cpp:303`) next to
`std::array attachments = {colorAttachment, depthAttachment}` (`:260`), and
`pipelineLayoutInfo.setLayoutCount = 2` (`SkyBox.cpp:347`) next to
`std::array<vk::DescriptorSetLayout, 2> combinedLayouts` (`:345`). That is the
same defect class the family exists to kill, in the same file, twice more.

**Second, the same two seams carry the same result-handling drift as the
framebuffer one did.** `vk::RenderPassCreateInfo` is spelled out by hand in
`Rasterizer.cpp:206-212`, `PostStage.cpp:246-252`,
`DeferredRasterizer.cpp:276-282`, `SkyBox.cpp:302-308` and
`CascadedShadowMap.cpp:177-184`; **PostStage alone** uses the two-out-param
`createRenderPass(&info, nullptr, &render_pass)` overload (`:254-255`) where the
other four use `ResultValue` — byte-for-byte the deviation batch IX found in the
framebuffer copies. `vk::PipelineLayoutCreateInfo` is worse: nine hand-written
blocks across eight files (`Clouds.cpp:178`, `Raytracing.cpp:253`,
`SkyBox.cpp:346`, `Rasterizer.cpp:318`, `PathTracing.cpp:197`,
`PostStage.cpp:271`, `CascadedShadowMap.cpp:384`, `DeferredRasterizer.cpp:305`
and `:330`) with **four different result-handling shapes** —
`ResultValue` + `ASSERT_VULKAN(VkResult(...))`, `ResultValue` +
`ASSERT_VULKAN(...result)`, the two-out-param overload, and Rasterizer's
hand-rolled `if (result == eSuccess) {...} else { ASSERT_VULKAN(...) }`
(`Rasterizer.cpp:324-329`), which is the only site where the assert is reached
through a branch rather than unconditionally.

**Third: 22 generated files are tracked in git that `.gitignore` already tries
to exclude.** `git ls-files -i -c --exclude-standard` returns 21 paths, every one
of them under `docs/build/` — Sphinx output, last touched 2026-05-16 (`c69e03b8`)
while `docs/source/` moved on 2026-07-31 (`16020e28`), so the committed HTML is
two months stale relative to the sources that generate it. `.gitignore` names the
directory **twice** (`docs/build/**/*` at line 11, `docs/build/*` at line 85), and
neither rule can take effect because git ignores exclude patterns for files
already in the index. A 22nd generated file,
`docs/source/__pycache__/conf.cpython-313.pyc`, is tracked with **no** ignore rule
covering it at all.

Ordering: **task 1 before task 3** — both touch `Rasterizer.cpp`, `PostStage.cpp`,
`DeferredRasterizer.cpp`, `SkyBox.cpp` and `CascadedShadowMap.cpp`, so doing them
in the other order or interleaved invites conflicts. Task 2 is disjoint.

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **the twelve hand-rolled `vk::ImageMemoryBarrier`
blocks** — still owned by the `- [b]` cloud-barrier entry above (the two cloud
barriers are `VulkanRenderer.cpp:882-900` and `:914-932`), unchanged since
batches IX and XII rejected it; **the five copies of the blocking
`map_async` + channel + `poll` + `get_mapped_range` + `unmap` readback in the
Rust crate** (`render/forward.rs:2315`, `render/ibl.rs:898`,
`render/histogram.rs:329` and `:367`, `render/gpu_timing.rs:476`) — a real
five-way duplication, but every extracted unit needs a live wgpu adapter, so the
refactor would ship with compile-only verification; **`render/histogram.rs` and
`render/gpu_occlusion.rs` having zero inline tests** — same adapter problem, and
the CPU-side maths they mirror is already covered by `render/auto_exposure.rs`'s
16 tests; **`CommandBufferManager::beginCommandBuffer` allocating a
`std::vector<vk::CommandBuffer>` for exactly one handle**
(`CommandBufferManager.cpp:29`) — a genuine needless heap allocation, but it is
upload/transition-time only, never on the frame path, and too small to spend a
task on alone; fold it in if something else touches that file.
**`Src/KomputePlayground`** — unchanged since batch XII raised it; still an owner
decision ("repair" vs "delete"), still not an executor task.

### C++ Vulkan engine

### Docs / repo hygiene

## 2026-08-03 batch II — planner (both model loaders upload the same image once per material that references it, straight into a 128-slot cap; a glTF walk that renders every scene in the document at once while the Rust twin renders one; three subsystems that hand-roll the descriptor triad `DescriptorSetGroup` exists to own; a config transform stamped onto model 0 whatever was loaded; a fourth copy of the shadow-resolution table size)

The actionable queue was empty when this batch was written (0 `- [ ]`, 15
`- [b]` across the whole file). Every `file:line` below was read out of the tree
this pass.

**Every task in this batch is verifiable with no GPU except task 3**, which is a
Vulkan-object refactor and says so in its own Test section. The other four are
provable with device-free gtest suites that already exist and already construct
the objects under test without a `VulkanDevice`: `GltfParseUnit.*`
(`gltfParseSuite.cpp` — 28 tests, all on `parseCpu` + the device-free accessors),
`ObjParseUnit.*` (`objParseSuite.cpp` — 18 tests, same shape),
`SceneAccessorUnit.*` (`sceneAccessorSuite.cpp:18-31` builds a bare `Scene` and
`std::make_shared<Model>()` with no device) and `ShadowResolutionUnit.*`
(`guiSceneVarsRoundTripSuite.cpp:193`).

**The headline is that neither model loader deduplicates an image that several
materials share, and the global texture array they feed is capped at 128 slots.**
`GltfLoader::parseCpu` (`GltfLoader.cpp:434-449`) extracts encoded bytes per
*material*: `objMaterial.textureID = textureImages.size(); textureImages.push_back(bytes)`.
Two materials pointing at the same `cgltf_image` therefore produce two copies of
the same PNG in `textureImages`, which `uploadParsed` (`:69-83`) decodes and
uploads as two independent `Texture`s. `ObjLoader::loadTexturesAndMaterials`
(`ObjLoader.cpp:223-225`) does the identical thing keyed on `diffuse_texname`:
`textures.push_back(resolveObjTexturePath(...)); material.textureID = texture_id++`,
so an `.mtl` where five materials share `wood.png` reads, decodes and uploads that
file five times. The cost is not only startup time and VRAM: every duplicate
consumes one entry of the flattened global array whose cap is
`MAX_TEXTURE_COUNT = 128` (`common/host_device_shared_vars.hpp:8`), enforced by
`planFlattenedTextureSlots` (`scene/ObjectDescription.ixx:66-84`) and reported by
`VulkanRenderer.cpp:1537-1544` as "models past the cap will sample the wrong
slots". A 40-material glTF with 8 distinct images currently spends 40 slots
instead of 8. This is the same defect class as `dca11022` (dedup `Model` texture
*samplers* by mip level) one level up the pipeline — that change deduplicated the
samplers while leaving the images they sample duplicated.

**Second, the two glTF loaders disagree about which nodes are in the scene.** The
Rust loader takes `document.default_scene().or_else(|| document.scenes().next())`
and recurses that scene's roots (`asset/gltf_loader.rs:163-170`); the C++ loader
iterates `data->nodes` — every node in the *document*
(`GltfLoader.cpp:457-475`). For a single-scene file the two agree. For a
multi-scene file the C++ renderer merges every scene into one, and a node present
in the document but referenced by no scene (legal glTF, and what most DCC tools
leave behind when a collection is disabled on export) is rendered by C++ and not
by Rust. This is the fifth entry in the cross-renderer-divergence family
(`bd315707` `map_Kd` backslashes, `6aa5eec6` the `Resources/` search,
`d25cd1e5` index validation, `92df2e7f` adapter absence).

Tasks 3, 4 and 5 are cleanups with a real failure mode behind each: three
subsystems hand-rolling the descriptor layout/pool/allocate triad that
`DescriptorSetGroup` was extracted to own — including a *hand-computed* pool size
with the arithmetic left in a comment (`Clouds.cpp:83`, `descriptorCount = 2; // +1 for noise`);
a scene-config transform applied to model index `0` no matter which model was just
appended; and a fourth hand-written copy of "the shadow-resolution table has four
entries".

Ordering: tasks 1 and 2 both edit `GltfLoader.cpp` — **do task 1 first**, since it
changes the material loop task 2 leaves alone, and doing them in the other order
puts task 2's traversal rewrite under task 1's diff. Tasks 3, 4 and 5 are disjoint
from those and from each other.

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **`GUI.cpp:319`'s hand-rolled `vk::DescriptorPoolCreateInfo`**
— it is the pool handed to `ImGui_ImplVulkan_Init`, which allocates its own sets
outside `DescriptorSetGroup`'s model, so it cannot use the helper and must stay;
**`CommandBufferManager::beginCommandBuffer` allocating a `std::vector<vk::CommandBuffer>`
for one handle (`CommandBufferManager.cpp:29`)** — re-confirmed and re-rejected for
the reason the 2026-08-03 batch gave (upload/transition-time only, never on the
frame path); **`MeshDrawRecorder` computing `glm::inverse(glm::transpose(model))`
per model per pass (`MeshDrawRecorder.cpp:35`)** — real redundant work, but
`recordSceneMeshDraws` has exactly two callers (`Rasterizer.cpp:91`,
`DeferredRasterizer.cpp:394`) and only one runs per frame, so it is one 4x4
inverse per model per frame; **the C++ glTF loader reading only `TEXCOORD_0` and
ignoring each texture's `texcoord` index (`GltfLoader.cpp:277-279`)** — a genuine
divergence from `uv_set_bit` in the Rust loader, but `Vertex` carries a single UV
set, so closing it is a vertex-format change, not a loader fix; **the
checked-in generated WGSL having no *content* gate** (`CheckedInWgslIsNotOlderThanItsSlangSource`
compares mtimes only, `buildIntegritySuite.cpp:1220`) — a real hole, but closing it
means running `slangc` inside the test or diffing after a compile, and the
combined WGSL emit is skipped below `minSlangcVersionForWgsl`, which the
`- [b]` ContainerHub SDK-bump entry above still blocks.

### C++ Vulkan engine

## 2026-08-03 batch III — planner (the one scene-changing path that forgets to rebind the acceleration structure it just destroyed, plus the gate that would have caught it; a GUI transform that re-applies the 60× scale SceneConfig deleted; a path-tracing history that ignores the light it integrates; an OBJ→glTF converter that drops the vertex colours the C++ loader honours)

The actionable queue was empty when this batch was written (0 `- [ ]`, 15
`- [b]` across the whole file). Every `file:line` below was read out of the tree
this pass.

**The headline is that three code paths change the scene and only two of them
rebind the descriptors afterwards.** `finishModelLoad`
(`VulkanRenderer.cpp:243-253`) and `addModel` (`:727-758`) both run
`createASForScene` → `rebuildObjectDescriptions` → **`updateAllDescriptorSets()`**.
`handleModelReloadRequest` — the GUI model-picker path (`:380-405`) — runs
`createASForScene` → `rebuildObjectDescriptions` →
**`updateTexturesInSharedRenderDescriptorSet()`** and stops there.
`updateRaytracingDescriptorSets()` (`:1346-1362`) is reachable *only* through
`updateAllDescriptorSets()` (`:760-769`), so after picking a different model
from the combo the raytracing descriptor sets still hold the
`vk::AccelerationStructureKHR` that `ASManager::createTLAS` destroyed at
`ASManager.cpp:230-233` — a destroyed handle bound at `TLAS_BINDING` for every
swapchain image, dispatched against on the next frame in which
`guiRendererSharedVars.raytracing || .pathTracing` is set. The same omission
also skips `updatePostDescriptorSets`, `updateGBufferDescriptorSets` and the
`pathTracingAccumulatedFrames = 0` reset that `updateRaytracingDescriptorSets`
performs precisely because "any accumulated history predates that world"
(`:1353-1359`). `goldenRenderSuite.cpp:2280-2341` already pins the equivalent
invariant for `addModel` ("red-proven by removing the addModel AS rebuild"); the
reload path has no such test.

**Second, the GUI model transform re-applies a scale the engine deliberately
removed.** `sceneConfig::getModelMatrix()` returns identity, and its comment
(`SceneConfig.cpp:116-124`) explains why: "the old 60x scale for the tiny
viking_room made the camera start INSIDE the geometry (all backfaces, culled ->
black viewport) and stretched the scene far beyond a cascade's useful
resolution". `handleModelTransformChange` (`VulkanRenderer.cpp:342-343`) still
opens with `glm::scale(modelMatrix, glm::vec3(60.0f, 60.0f, 60.0f)); //
Apply original scale`. `model_position`/`model_rotation` default to all-zero
(`GUISceneSharedVars.ixx:90-91`), so the *first* one-pixel drag of the Position
or Rotation widget (`GUI.cpp:101-106`) jumps the loaded model from 1× to 60×.

**Third, the path tracer's temporal accumulation is invalidated by camera and
quality changes but not by the light it integrates.**
`recordRaytracingOrPathTracing` (`VulkanRenderer.cpp:1102-1110`) resets
`pathTracingAccumulatedFrames` when the view matrix, samples-per-pixel or
max-bounces changed. `path_tracing.slang` reads `sceneUBO.dirLight.direction`
(`:227`) and `sceneUBO.dirLight.color` rgb + w-as-intensity (`:253-255`) inside
the NEE loop and folds the result into the running mean at `:282-292`. Dragging
the directional-light direction, colour or radiance in the GUI therefore leaves
frames lit by the *old* light weighted into the mean, decaying only as `k/N`
with no reset — the same defect the view-matrix check exists to prevent.

**Fourth, the OBJ→glTF converter drops per-vertex colours that the C++ loader
honours.** `ObjLoader::loadVertices` reads `attrib.colors` into `Vertex::color`
(`ObjLoader.cpp:336-342`) and the Rust glTF loader reads `COLOR_0`
(`asset/gltf_loader.rs:492-495`, `:530`), pinned on the C++ side by
`GltfParseUnit.ReadsColor0VertexColours` (`gltfParseSuite.cpp:682`). But
`asset/obj_to_gltf.rs`'s `"v"` arm (`:200-209`) reads exactly three components
and silently ignores the optional `r g b` of `v x y z r g b`, and `to_gltf`
emits `"attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 }` (`:455`)
with no colour accessor (`:537-541`). A vertex-coloured OBJ renders tinted in
the Vulkan engine and flat-white in the WebGPU one — the sixth entry in the
cross-renderer-divergence family (`bd315707` `map_Kd` backslashes, `6aa5eec6`
the `Resources/` search, `d25cd1e5` index validation, `92df2e7f` adapter
absence, `c7b66fa5` the default-scene walk).

Ordering: **task 1 before task 2** — task 2's gate asserts the helper task 1
introduces. Tasks 3 and 4 also edit `VulkanRenderer.cpp` but in disjoint regions
(`:336-378` and `:1096-1122` versus task 1's `:243-253`/`:380-405`/`:760-769`);
still, do them one at a time. Task 5 is in the Rust submodule and disjoint from
all four.

**GPU verification is currently unavailable** (see the `- [b]` RDP entry above),
so every task below lands a device-free gate or unit test as its primary
evidence, and names the GPU golden only as the secondary check to run once a
console session exists.

Candidates found but NOT tasked (checked, then rejected — do not re-propose
without new evidence): **`ASManager::cleanUp()` is not idempotent**
(`ASManager.cpp:382-402` destroys `tlas.vulkanAS` and every `blas[i].vulkanAS`
without nulling them or clearing `blas`, so a second `cleanUp()` — or a
`createASForScene()` after one — double-destroys) — a real latent double-free
and the odd one out against the "Idempotent: safe to call again" convention
`Clouds::cleanUp`, `CascadedShadowMap::cleanUp` and `DeferredRasterizer::cleanUp`
all state, but no call site reaches either sequence today and the fix has no
device-free test; **`CascadedShadowMap::recordCommands` binding the light-matrices
set at index 0 when the shared span is empty** (`CascadedShadowMap.cpp:421-423`,
a pipeline-layout mismatch where `Clouds::recordComputeCommands` bails instead,
`Clouds.cpp:149-152`) — same unreachability, same lack of a device-free test;
**an absolute `map_Kd` path** (`resolveObjTexturePath` prefixes `base + "/"`
unconditionally, `ObjLoader.cpp:174`; the Rust twin does the same with
`obj_path.with_file_name(uri)`, `obj_to_gltf.rs:635`) — both renderers are wrong
*identically*, so it is a shared limitation, not a divergence, and no shipped
asset uses one; **`assignTextureOffsets` advancing past `MAX_TEXTURE_COUNT`**
(`scene/ObjectDescription.ixx:29-36`) — every shader already clamps
(`int textureId = clamp(int(obj.texture_offset) + material.textureID, 0,
MAX_TEXTURE_COUNT - 1)` in all five of `rasterizer`, `deferred`, `path_tracing`,
`raytrace.rchit`, `shadow_map`), so an over-cap model samples a wrong slot, not
out of bounds, which is exactly what `VulkanRenderer.cpp:1537-1544` already
warns; **`CommandBufferManager::beginCommandBuffer` allocating a
`std::vector<vk::CommandBuffer>` for one handle** — re-confirmed and re-rejected
for the third time (upload-time only, never on the frame path).
**`Src/KomputePlayground`** — unchanged; still an owner decision.

### C++ Vulkan engine

- [ ] **(S) Make the path-tracing accumulation history include the directional light, not just the camera** — changing the light while path tracing keeps averaging frames lit by the old one.

  **Files to read:**
  - `Src/GraphicsEngineVulkan/renderer/VulkanRenderer.cpp` — `recordRaytracingOrPathTracing` (`:1077-1123`), the existing reset at `:1102-1110`; `updateUniforms` (`:177-187`) for where `sceneUBO.dirLight` is filled from the GUI
  - `Src/GraphicsEngineVulkan/renderer/VulkanRenderer.ixx` — the `pathTracingLastView` / `pathTracingLastSamples` / `pathTracingLastBounces` members
  - `Resources/ShadersSlang/path_tracing/path_tracing.slang` — `:227` and `:253-255` (the NEE terms that read `sceneUBO.dirLight`), `:282-292` (the running mean the stale samples land in)
  - `Src/GraphicsEngineVulkan/renderer/SceneUBO.hpp` — the `dirLight` layout
  - `Test/commit/VulkanEngine/renderModesSuite.cpp` — an existing small device-free suite over renderer-side state to follow

  **Steps:**
  1. Add `Src/GraphicsEngineVulkan/renderer/PathTracingHistory.hpp` with a `struct PathTracingHistoryKey { glm::mat4 view; glm::vec4 lightDirection; glm::vec4 lightColorAndRadiance; int samplesPerPixel; int maxBounces; }` and `bool operator==` / `!=` (defaulted is fine — the members are compared exactly, matching today's `current_view != pathTracingLastView`).
  2. Replace the three `pathTracingLast*` members in `VulkanRenderer.ixx` with a single `PathTracingHistoryKey pathTracingLastHistory{};`.
  3. In `recordRaytracingOrPathTracing`, build the current key from `camera->calculate_viewmatrix()`, `sceneUBO.dirLight.direction`, `sceneUBO.dirLight.color` (whose `.w` already carries the radiance, see `:182-185`) and the two GUI quality values; reset `pathTracingAccumulatedFrames = 0` and store the key when it differs.
  4. Update the comment at `:1100-1101` to say "camera, light or quality change" and name the shader terms (`path_tracing.slang:227`, `:253-255`) that make the light part of the integrand.

  **Test:** Add `Test/commit/VulkanEngine/pathTracingHistorySuite.cpp` with `PathTracingHistoryUnit.IdenticalKeysCompareEqual`, `PathTracingHistoryUnit.ALightDirectionChangeInvalidatesTheHistory`, `PathTracingHistoryUnit.ARadianceChangeInvalidatesTheHistory` (the `.w` channel specifically — it is easy to drop when only comparing rgb) and `PathTracingHistoryUnit.ACameraMoveStillInvalidatesTheHistory` (the behaviour that must not regress). Register the file in `Test/commit/VulkanEngine/CMakeLists.txt` **and** the Windows CI suite filter (see `BuildIntegrity.EveryCpuSuiteIsInTheWindowsCiFilter`).

  **Build:** `clangcl-debug`. Run:
  `pwsh -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows-Container.ps1 -Configurations clangcl-debug`
  then `.\build-clangcl-debug\commitTestSuite.exe --gtest_filter=PathTracingHistoryUnit.*`.

  **Context:** `updateRaytracingDescriptorSets` (`:1346-1362`) already resets the counter for the "traced world changed" case and its comment explains the failure mode in detail — frames traced against a stale world "stay blended into the running mean until the camera happens to move". This is the same failure mode for a stale *light*, and the fix belongs at the same granularity. Do not widen the key to the whole `SceneUBO`: cloud, shadow and PCF parameters are not read by `path_tracing.slang`, and resetting on them would throw away valid samples.

### Rust WebGPU renderer

- [ ] **(M) Carry OBJ per-vertex colours through the OBJ→glTF converter as `COLOR_0`** — a vertex-coloured OBJ renders tinted in the Vulkan engine and flat white in the WebGPU one.

  **Files to read:**
  - `ExternalLib/Kataglyphis-RustProjectTemplate/crates/webgpu_renderer/src/asset/obj_to_gltf.rs` — the `"v"` arm (`:200-209`), the `ObjMesh` fields (`:48-66`), `to_gltf` (`:399-459` for the buffer layout and the `"attributes"` string at `:455`), the JSON template's `bufferViews`/`accessors` arrays (`:531-542`), and the module doc (`:15-18`)
  - `.../src/asset/gltf_loader.rs` — `:492-495` and `:530`, the `COLOR_0` read (vec3 → vec4 with a = 1) that the converted document must feed
  - `Src/GraphicsEngineVulkan/scene/ObjLoader.cpp` — `:332-342`, the C++ behaviour this must match (absent colour ⇒ identity `(1,1,1)`, never a sentinel)
  - `Test/commit/VulkanEngine/gltfParseSuite.cpp` — `:682`, `GltfParseUnit.ReadsColor0VertexColours`, the C++ half of the invariant

  **Steps:**
  1. Add `pub colors: Vec<[f32; 4]>` to `ObjMesh` and a `pub has_vertex_colors: bool` (or make the field `Option<Vec<...>>`) so a colourless OBJ converts byte-identically to today — existing converted assets and the comparison harness must not move.
  2. In the `"v"` arm, accept 6 (and 7, for `v x y z r g b a`) components: push the position as now, and push the colour into a parallel `vertex_colors` array, defaulting to `[1.0, 1.0, 1.0, 1.0]` when the line carried only 3. Reject a length that is neither 3, 6 nor 7 with the existing `bail!` style.
  3. In the `"f"` arm, where a new unique vertex is pushed (`:241-265`), push the corresponding entry into `mesh.colors` alongside `mesh.uvs` / `mesh.normals` so the arrays stay parallel per unique vertex.
  4. In `to_gltf`, append the colour floats to `bin` after the UVs (before the 4-byte pad at `:423-425`), add a fifth `bufferView`, and emit a `VEC4` `componentType: 5126` accessor. **The index accessors are addressed as `3 + run` (`:456`)** — replace that literal with a computed base so adding an attribute accessor cannot silently repoint every primitive's `indices`. Emit `"COLOR_0": 3` in the primitive attributes only when the mesh actually carries colours.
  5. Fix the module doc at `:15-18`: it claims smoothing groups are "rejected rather than silently mangled", but `"s"` is in the ignore list at `:307`. State what actually happens (ignored, because every real OBJ carries them) and leave negative indices as the genuinely rejected case (`resolve`, `:381-393`).

  **Test:** In `obj_to_gltf.rs`'s test module, add: a parse test that `v 0 0 0 1 0 0` yields `colors[i] == [1.0, 0.0, 0.0, 1.0]`; a test that a colourless OBJ produces `has_vertex_colors == false` and a `to_gltf` output whose primitive attributes contain no `COLOR_0`; and a round-trip test that loads the emitted document back with the real `gltf` crate (the pattern the module doc at `:6-7` already describes) and asserts `read_colors(0)` returns the source colours. Add one test asserting the index accessor indices are unchanged for a colourless mesh, so step 4's renumbering is pinned.

  **Build:** No C++ build needed. Run:
  `cargo test -p kataglyphis_webgpu_renderer` from `ExternalLib/Kataglyphis-RustProjectTemplate`.
  This crate's suite runs in the `ubuntu-24.04` leg of Linux CI via `Scripts/Linux/run-cargo-tests.sh` (see AGENTS.md), so a push gets signal without `[build-win]`. Commit the submodule and bump the gitlink in the same change.

  **Context:** See `docs/shader-sharing.md` and the divergence family listed in this batch's preamble — the rule is that the two renderers may differ only in ways that are written down. `Vertex::color` is already multiplied into the C++ fragment output with glTF `COLOR_0` semantics (`ObjLoader.cpp:332-335` spells out why the absent case must be `(1,1,1)`), and `forward.wgsl` already consumes `vertexColor`, so this closes the gap entirely on the converter side — no shader change is needed on either renderer.

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

## 2026-08-02 — reuse-sweep residuals (the sweep itself shipped)

The 2026-08-02 reuse sweep landed: WindowsCMake/Config/Formatting/WebDav/
MSIX modules + download script upstreamed to ContainerHub; uv-venv logic
consolidated (4 copies -> upstream python_uv.sh / WindowsUv.Common); Slang
manifest single-sourced as `Resources/ShadersSlang/shader-manifest.json`
with the four PS/sh drifts fixed; app runners consolidated over ContainerHub
`app-runner.sh` + `Resolve-AppExecutablePath`; CI docker-run boilerplate
moved into ContainerHub composite actions; Pester wired into CI
(`pester-tests` job); cargo-retry upstreamed; LICENSES-README rewritten;
CHANGELOG.md deleted (git history + this file are the record). What remains:

- [b] **ContainerHub has no LICENSE file** (S, **blocked on owner
  decision**) — surfaced by the license audit; consumers cannot state its
  terms. Pick a license (sibling Kataglyphis repos use MIT) and add the
  file in ContainerHub. Also queued in ContainerHub's
  docs/refactoring-backlog.md.

## 2026-08-02 batch II — findings from the Stevedore + Rancher verification pass

- [b] **Host GPU golden verification is unusable over RDP** (M, **blocked on
  a console/physical login**) — re-confirmed 2026-08-02: 28 of 30
  `GoldenRender.*` fail from the repo root on the RX 9070 XT, every frame
  logging "No synchronization frames available; skipping draw frame"
  (`VulkanRenderer.cpp:443`) because `createSynchronization()` (`:1470`)
  sizes FrameSync from `vulkanSwapChain.getNumberSwapChainImages()` and the
  swapchain reports **0 images** under this session. `query session` shows
  the work running in `rdp-tcp#0` while `console` (ID 1) is a separate
  session — the same environmental failure first recorded 2026-08-01, in
  both active and disconnected RDP states.
  **This is NOT the FrameSync extraction (`e7e7579d`) misbehaving** — that
  guard only reports the empty swapchain — and it is not the 2026-08-02
  dedup/shader work either (which touched no engine source and whose shader
  outputs are byte-identical). Do not "fix" it by weakening the guard or
  rewriting FrameSync.
  What is actually needed: run the suite from a console/physical login and
  confirm 30/30, then decide whether host GPU verification needs a documented
  console-session prerequisite (docs/gpu-golden-testing.md) and/or whether
  the offscreen golden path should stop depending on a real swapchain at all.
  Until then treat host GPU goldens as unavailable and do not burn executor
  retries on them.

- [b] **Bump the ContainerHub Linux image's Vulkan SDK past slangc 2026.8**
  (S) — `ExternalLib/Kataglyphis-ContainerHub/linux/Dockerfile.base` already
  carries `ARG VULKAN_VERSION=1.4.350.0` (landed in ContainerHub `709756e`,
  "bump Vulkan SDK to 1.4.350.0"), and this repo's submodule pin
  (`6aeb0f6`) is already past that commit — the Dockerfile edit itself is
  done, nothing left to change there.
  BLOCKER: what remains is rebuilding and pushing the multi-arch
  `:latest-cross` image to `ghcr.io/kataglyphis/kataglyphis_beschleuniger`.
  There is no CI job that does this automatically (ContainerHub's
  `ubuntu24.04.yml` only runs `preflight` + `build-docs`); it's a manual,
  local run of `linux/scripts/build-cross-chain.sh`, which `preflight.sh`
  itself documents as taking **hours under QEMU** across 3 architectures,
  requires `GHCR_PAT` push credentials, and mutates a shared registry tag
  that CI (`Linux.yml`) pulls for every build. That's an hours-long,
  hard-to-reverse, shared-infrastructure action outside a one-shot headless
  executor turn — needs to be run by the owner (or a long-lived session)
  with registry credentials, not attempted unattended.
  Once pushed, confirm a container run of `compile-slang-shaders.sh` emits
  10 combined WGSL files and leaves
  `git -C ExternalLib/Kataglyphis-RustProjectTemplate status` clean.
  Build preset: none (container tooling).

