# ROADMAP

Consolidated future work across the whole project. Detailed per-area status
lives in `docs/` (`cpp-renderer-improvements.md`,
`webgpu-renderer-roadmap.md`, `shader-sharing.md`); this file is the
single list of what is still open. Sizes: S (< half a day), M (a day-ish),
L (multi-day), XL (multi-week).

## C++ Vulkan engine

- [x] **Stage/renderer-level RAII** (L, stages done 2026-07-19; renderer-level cleanup consolidation still open) — leaf types (`VulkanBuffer`/`VulkanImage`)
  are move-only with destructor release; extend the pattern upward through the
  render stages and `VulkanRenderer` so the hand-ordered 48-line `cleanUp()`
  and the device-lost special-casing in `App.cpp` disappear.
- [x] **Sync-validated barrier removal** (S, done 2026-07-19 — and sync validation first exposed 10 real depth-sync hazards, fixed in the same unit) — the same-layout swapchain barrier
  after the skybox pass looks redundant; remove only after a run with
  synchronization validation enabled confirms the post pass's external
  dependency covers it.
- [x] **GPU timestamps + debug labels per pass** (M, done 2026-07-19; GUI panel visual check pending next unlocked desktop session) — nothing is measured
  on-device today (one unused query pair in PathTracing). Prerequisite for
  honest performance work; label every pass for RenderDoc while at it.
- [x] **`DescriptorManager` extraction** (M, done 2026-07-19 as `DescriptorSetGroup`; VulkanRenderer -617 lines) — `VulkanRenderer` still owns four
  structurally identical descriptor layout/pool/update triads (~55 members,
  ~71 methods overall); mirror the PipelineBuilder move.
- [ ] **Async asset loading** (L) — model load/reload and AS builds block the
  main thread; move to a worker with fence-based handoff (staging ring
  already removed the per-upload queue stalls). **Now quantified**: OBJ
  parsing alone is ~7 ms/MB (`BM_ObjParse_Suzanne`), so the bundled 27 MB
  model implies ~200 ms of frozen main thread.
- [x] **Clouds compute cost** (M, inversions hoisted 2026-07-19, A/B verified; half-res dispatch still open as a quality tradeoff) — per-pixel `inverse()` of two matrices at
  full resolution with up to 128 march steps; move inversions into the UBO,
  render at half resolution, before anyone flips `clouds_enabled` on.
- [ ] **glTF loading** (L) — reuse the Rust renderer's test assets and enable
  the cross-renderer comparison harness below.
- [x] **CMake preset diet** (M, done 2026-07-19: 26/24/1 -> 23/22/6; the real problem was one test preset, not preset count) — 57 presets, 48 build vs 1 test preset,
  asymmetric compiler coverage; halve it and align MSVC/ClangCL variants.
- [x] **CI sanitizers** (M, done 2026-07-19: ASan+UBSan and TSan steps on Linux CI + new linux-debug-asan-clang preset; CI result not yet observed from here - no gh CLI) — TSan presets exist but no workflow runs them; the
  new ASan preset should gate at least the unit tests on Linux.
- [ ] **C++ golden rendering tests** (L) — port the Rust pattern: headless
  render-to-texture + structural pixel assertions (would have caught the
  unused-shadow-map bug); requires an offscreen path in `VulkanRenderer`.
- [x] **Perf suite that measures the engine** (M, done 2026-07-19: camera/projection/scene-config/OBJ-parse benchmarks, baseline in BACKLOG.md; CTest registration still open) — Google Benchmark currently
  benchmarks `std::string`; benchmark frame recording / upload paths instead
  and register with CTest.
- [ ] **Fuzz more surfaces** (S each) — SceneConfig parsing, shader-file
  reader, GUI state round-trip; the harness is proven on OBJ parsing.

## Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate`)

- [ ] **Basis ETC1S/UASTC transcoding** (M) — KTX2 BCn passthrough is done;
  supercompressed files are rejected with a clear error until a transcoder
  dependency lands (also unlocks compressed textures on the web path).
- [ ] **meshoptimizer-grade decimation** (S integration) — swap the
  vertex-clustering `simplify_primitive` for quadric-error simplification;
  API already isolates the swap to one function.
- [ ] **Web swapchain sRGB fix** (S) — browsers expose no sRGB surface format,
  so the web demo renders slightly dark; add a shader-side encode when the
  target is non-sRGB (documented in `docs/webgpu-srgb-audit.md`).
- [ ] **Auto-exposure** (M) — manual EV shipped; histogram-based auto next.
- [ ] **Per-pixel alpha-tested shadows** (S) — textured MASK materials
  currently cast by base-alpha only.
- [ ] **HDR-cubemap IBL** (M) — replace the analytic hemisphere approximation
  with a real prefiltered environment map; MikkTSpace tangents alongside.
- [ ] **Web drop-zone / model picker** (S) — native drag-and-drop shipped;
  browser File API pending.
- [ ] **Touch controls** (S) — orbit/zoom gestures for the embedded demo.
- [ ] **GPU instancing + indirect draws** (M) — structure exists for neither;
  needed before large scenes.
- [ ] **Clustered/tiled lighting** (L) — 4-light cap is fine today; lift it
  when a real scene needs it.
- [ ] **GPU occlusion culling** (L) — frustum culling shipped; depth-pyramid
  occlusion later.
- [ ] **WebXR** (XL) — parked.
- [ ] **Colosseum demo scene** (blocked on you) — pick a licensed photogrammetry
  scan; LOD + KTX2 machinery is ready, keep the asset out of git.

## Cross-renderer

- [ ] **Shader export in the C++ build** (S) — wire
  `cargo run --example export_shaders` into `Build-Windows.ps1` emitting to
  `Resources/Shaders/generated/` so the Vulkan engine picks up WGSL changes
  automatically (`docs/shader-sharing.md`).
- [ ] **Side-by-side comparison harness** (M) — same scene, same camera,
  Vulkan vs WebGPU screenshot diff; with shared BRDF math this becomes a
  regression net for both renderers (needs C++ glTF + offscreen path above).
- [ ] **OBJ→glTF conversion** (S) — make `Resources/Models` consumable by the
  Rust renderer.

## Dependencies / housekeeping

- [ ] **cargo-deny advisories** (blocked upstream) — `quick-xml 0.39.4` CVEs
  (Linux/Wayland only, pinned via winit) and unmaintained `ttf-parser` (egui
  fonts); revisit on winit/egui releases. Deliberately not ignored in
  `deny.toml` so they stay visible.
- [ ] **FUZZTEST checkout watcher** (S investigation) — an unidentified host
  process occasionally re-checks-out `ExternalLib/FUZZTEST` to the latest
  date tag; find and disarm it (pin is currently correct at `ad66c13`).
- [ ] **Rust renderer roadmap tail** — anything not listed here lives with
  status in `docs/webgpu-renderer-roadmap.md`.
