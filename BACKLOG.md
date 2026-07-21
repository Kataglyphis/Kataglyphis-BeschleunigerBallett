# BACKLOG

The single list of open work across the whole project — sized commitments and
unsized ideas together. Detailed per-area status lives in `docs/`
(`cpp-renderer-improvements.md`, `webgpu-renderer-roadmap.md`,
`shader-sharing.md`); this file is what is still to do.

Sizes: S (< half a day), M (a day-ish), L (multi-day), XL (multi-week).
Checkbox items are sized and agreed; the prose sections below the fold are
candidates that have not been sized yet. A candidate graduates by acquiring a
size and a decision, or gets dropped.

> Merged from the former `ROADMAP.md` on 2026-07-20. There is no longer a
> separate roadmap file — one list, so a stale entry in one place cannot
> contradict a fresh one in the other. That had already happened: the roadmap
> still described cascaded shadows as completely broken a day after they were
> fixed.

## C++ Vulkan engine

- [x] **Cascaded shadows work** (settled 2026-07-20) — the faintness was the
  test scene, not the renderer. Measured on the same build: 0.13% of pixels
  darkened with the dinosaur SKELETON as caster, **6.45% with a solid box**.
  Thin bones leave most of the 5x5 PCF kernel's 25 taps unoccluded, so the
  shadow never reaches full strength. `GoldenRender.ShadowsDarkenSomePixels`
  is enabled again, runs against a purpose-built rig
  (`Resources/Models/ShadowTest/shadow_rig.obj`, a solid box over a plane) via
  the new `KATAGLYPHIS_MODEL_OVERRIDE` hook, and asserts >2% against a
  measured 5.42%. Verified in BOTH directions: reintroducing the shadow-pass
  culling bug drops it to 2.43% and the test fails.

  Two corrections to what I first claimed here, both caught by re-running:
  the rig's first version used a small centred box, and the ImGui panel
  covers the middle of the viewport - so whether the box was visible depended
  on the granted window size, and the same binary measured 6.45% once and
  0.01% later. The occluder is now a broad slab whose shadow band survives any
  framing (5.41-5.44% over four runs). And the first "verified to fail"
  reading was that hidden-box artifact, not the culling bug: over a CLOSED
  occluder, back-face culling still records the slab's far side, so the bug
  halves the signal rather than erasing it. The threshold is 4% because that
  is what separates a halved signal from a correct one - measured, not
  chosen.

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
- [x] **CPU frustum culling** (done 2026-07-20) — plane extraction, a
  conservative AABB test and object->world AABB transform as free functions
  (`scene/Frustum.ixx`, 8 CPU-only tests), mesh bounds computed from vertex
  positions at construction, and both raster paths skipping meshes that are
  provably outside the view. Toggle:
  `GUIRendererSharedVars::frustum_culling_enabled`.

  The shadow pass is culled too, but against **each cascade's own light
  frustum**, never the camera's. The distinction is the whole point: geometry
  beside or behind the camera still casts into view, so a camera-frustum test
  would delete shadows, whereas geometry outside a cascade's ortho box cannot
  affect that cascade's depth map.

  That test also ignores the near plane (`isVisibleAsShadowCaster`). A caster
  between the light and the box - tall geometry, a ceiling - sits outside the
  near plane and still casts into the box, because its shadow travels along
  the box's depth axis. Dropping only the near plane is safe precisely because
  the cascade projection is orthographic, so the side planes run parallel to
  the light; under a perspective frustum the same trick would not work.

  Honest scope: the debug scene is one model with one mesh, so this saves
  nothing measurable today. It pays off with the multi-object work below, and
  it was verified live rather than assumed - inverting the test (cull what is
  visible) fails `GoldenRender.ShadowsDarkenSomePixels`.
- [x] **Per-mesh visibility statistics** (done 2026-07-20) — drawn/considered
  counters written by whichever raster path recorded the frame, surfaced in a
  GUI "Visibility" panel alongside the culling toggle. They also made the
  first end-to-end culling test possible
  (`GoldenRender.FrustumCullingDropsOffscreenMeshesOnly`): without a counter,
  a test can only observe that the picture still looks right, which is equally
  true when culling is a no-op.
- [x] **Model loading parsed the OBJ twice** (fixed 2026-07-20) — measured on
  the bundled 27 MB `dinosaurs.obj` in a debug/ASAN build: **5.15 s with the
  duplicate parse, 2.98 s without**. `loadTexturesAndMaterials` and
  `loadVertices` each called `ParseFromFile`; they now share one parse.

  The same change removed an `exit(EXIT_FAILURE)` on a malformed asset. The
  two functions disagreed about it - `loadVertices` returned gracefully with
  a comment saying the GUI can feed arbitrary files, while
  `loadTexturesAndMaterials` killed the process and ran first, so the graceful
  path was unreachable.
- [x] **Async asset loading** (done 2026-07-20) — the window no longer freezes
  for the whole model load. **Measured on the bundled 27 MB model: 2800 ms of
  CPU parse moved off the render thread**, leaving the ~15 ms GPU upload, which
  must stay on the thread owning the device.

  `ObjLoader::parseCpu` performs the whole CPU side and touches no Vulkan;
  `ObjLoader{}` constructs without a device for exactly this.
  `AsyncModelParse` (`scene/AsyncModelParse.ixx`) runs it on a `std::thread`
  with start/poll/take, and its destructor JOINS rather than detaches - a
  worker writing into a dead loader would corrupt geometry rather than crash.
  `ObjLoader::uploadParsed` is the matching GPU half.

  **Moving the parse was the small part.** The blocking load was immediately
  followed by three things that read scene CONTENTS: the acceleration
  structures, the object-description buffer, and the descriptor sets that point
  at it. Those now run in `VulkanRenderer::finishModelLoad()` on the frame the
  model lands. Descriptors are still written once during init, or the first
  frames sample bindings that were never written at all.

  Two things fell out of it:

  - `ASManager::cleanUp()` dereferenced a null device whenever
    `createASForScene` had not run. Unreachable before, because init always
    built the AS; it is now simply what shutting down mid-load looks like.
  - The three suites that drive the engine asserted on geometry that now
    arrives several frames later.
    `Test/commit/VulkanEngine/EngineLoadWait.hpp` pumps frames until the model
    is installed, capped so a parse that never finishes fails the test rather
    than hanging CI.

  Still deliberately one parse at a time - a queue needs cancellation semantics
  for "user picks a third model while the second loads", and nothing asks for
  them yet.

- [ ] **glTF loading** (L) — reuse the Rust renderer's test assets and enable
  the cross-renderer comparison harness below.
- [x] **Fuzz the untrusted input surfaces** (done 2026-07-20) — SceneConfig,
  OBJ parsing, the shader-file reader and texture decoding all have targets,
  and all four run their seed corpora in Windows CI. KTX2 is deliberately not
  covered: the C++ engine does not use it (the KTX dependency belongs to the
  Rust renderer, which has its own tests).
- [ ] **Renderer-level RAII cleanup consolidation** (M, **blocked on being
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

- [ ] **Basis ETC1S/UASTC transcoding** (M) — KTX2 BCn passthrough is done;
  supercompressed files are rejected with a clear error until a transcoder
  dependency lands (also unlocks compressed textures on the web path).
- [x] **LOD is on the render path** (done 2026-07-20) — levels are built once
  in `upload_scene` with `Simplifier::Quadric` and pre-uploaded as their own
  vertex/index buffers; selection is per-primitive per-frame on camera distance
  to `world_center`, through the same `select_lod` rule rather than a second
  one grown on the render path. Measured on the bundled cube (12 tris):

  | camera distance | selected | indices |
  |---|---|---|
  | 3.0 | full detail | 36 |
  | 12.0 | level 0 | 18 |
  | 60.0 | level 1 | 6 |

  **Off by default**, so every existing test keeps its meaning; with it off no
  levels are built at all and the count stays 36 from 0.5 to 10000.

  **Shadow casters deliberately stay at full detail.** Camera distance is the
  wrong metric there — the cascade renders from the light, so a primitive far
  from the camera can be the occluder filling a *near* cascade — and a popping
  shadow silhouette is far more visible than a popping mesh, since the mesh
  pops when it is a few pixels while its shadow can land beside the viewer at
  full size. Shadow LOD would need its own per-cascade, light-relative metric.

  Also pinned as an executable fact: `VertexClustering` at ratio 0.02 returns
  the cube's 12 triangles **unchanged**, which is why the render path uses
  Quadric.

- [x] **meshoptimizer-grade decimation** (done 2026-07-20, but see the
  integration item above) — quadric-error simplification shipped in
  `scene/qem.rs`, selectable via `build_lod_chain_with(.., Simplifier::Quadric)`.
  Held to the SAME 18-triangle budget as clustering on a 512-triangle grid with
  one raised vertex: QEM keeps the spike at peak 2.000 (max deviation 6.66e-8),
  clustering reports peak 0.000 — it does not shorten the spike, it loses it,
  because the tip lands alone in its cell and every triangle using it is then
  dropped as degenerate. A co-planar grid goes 450 -> 4 triangles at 5.06e-6.
  **`build_lod_chain` still defaults to clustering**, and neither is called by
  the renderer.

  Historical note on the clusterer it replaces:

  Partly improved 2026-07-20: clustered vertices now merge to their cell
  CENTROID rather than the first vertex seen, which removes a vertex-order
  dependency and pulls the simplified surface toward the middle of the
  geometry instead of an arbitrary cell corner. That is a better clusterer,
  not decimation.

  What QEM would add and this cannot: merged positions placed to minimise
  distance to the original SURFACE rather than to the original vertices,
  preserving silhouettes and creases that clustering rounds off. Re-sized S ->
  M: a subtly wrong QEM looks fine on a cube and falls apart on real meshes,
  so this wants a photogrammetry-scale asset to validate against before it is
  worth attempting.
- [x] **Web swapchain sRGB fix** (done 2026-07-20) — the tonemap shader now
  applies the sRGB transfer function itself when the target is non-sRGB.
  Guarded by a headless test comparing an sRGB and a non-sRGB render; without
  the encode the two means differ by 49 levels (177.17 vs 127.77).
- [x] **Auto-exposure** (done 2026-07-20) — histogram compute pass, GPU
  reduction to an adapted EV, and the tonemap reading it from a buffer. No
  per-frame readback anywhere on the frame path. Manual EV survives as an
  override and routes through the same buffer. 13 tests across the CPU maths,
  the compute passes and the end-to-end wiring; the last two verified to fail
  when the exposure is disconnected.

  Defaults OFF (`ForwardRenderer::auto_exposure`). Turning it on by default is
  a look decision, not a technical one, and wants eyes on a few real scenes
  first. `frame_delta_seconds` defaults to a nominal 60 Hz - callers driving
  real frames should set it, or adaptation runs at the wrong rate on any other
  refresh.

- [ ] **Per-pixel alpha-tested shadows** (M, not S — attempted and reverted
  2026-07-20) — textured MASK materials cast by base-alpha only, so a foliage
  card (white base-color factor, cut-out entirely in the texture) casts the
  shadow of the solid quad it is modelled as.

  A full implementation was written and then **reverted because it could not
  be shown to work**: `vs_shadow_masked`/`fs_shadow_masked` in `forward.wgsl`,
  a `shadow_masked_bind_group_layout` carrying base color at bindings 3/4, a
  second shadow pipeline used only for MASK primitives, and per-primitive
  routing. It compiles, runs, and changes nothing measurable.

  What the next attempt does NOT need to re-derive:

  - The masked pipeline **does** run: an unconditional `discard` in
    `fs_shadow_masked` removes the shadow entirely (0 shadowed pixels).
  - Routing is correct: printing `casts_shadow`/`alpha_masked` per primitive
    shows the MASK card on the masked pipeline.
  - The interpolated UV reaches the fragment stage and varies:
    `if (in.uv.x > 0.5) { discard; }` halves the shadow (496 -> 260 pixels).
  - `base_uv` equals `in.uv` (identity KHR_texture_transform), so the
    transform is not at fault.
  - **The sampled alpha is >= 0.5 everywhere even for a half-transparent
    texture**, at mip 0 via `textureSampleLevel`. The same texture on the same
    primitive cuts out correctly in the FORWARD pass (bright pixels
    60482 -> 56913), so the texture reaches the GPU and the forward alpha test
    works. Everything points at the view bound at binding 3 of the masked
    shadow bind group not being the material's base color texture, but
    `views[0]` is demonstrably the base color slot and I could not prove it.

  Also worth keeping: **a closed cube is useless as the test caster.** Its
  shadow is the union of six faces' projections, so discarding half of every
  face leaves the silhouette unchanged — an alpha test that provably ran moved
  the shadowed pixel count by under 10%. Use a single-sided card (the plane
  mesh, cloned and raised) as the caster.

  One correction to the old note here: `casts_shadow` skipping MASK
  primitives whose `base_color[3] < cutoff` was CORRECT — such a material is
  invisible in the forward pass too. The defect is the SHAPE of a visible
  cut-out's shadow, not its absence.
- [x] **HDR-cubemap IBL** (done, audited stale 2026-07-21) — the real prefiltered
  environment map already exists and is the primary path: full split-sum IBL in
  `render/ibl.rs` (irradiance-convolved cubemap + roughness-prefiltered specular
  cubemap + a `BrdfLut` integration texture), fed by `asset/hdr.rs` decoding real
  Radiance `.hdr`/RGBE files into an `EquirectImage`, and sampled in `forward.wgsl`
  (`irradiance_map`/`prefiltered_map`/`brdf_lut`). `tests/ibl.rs` proves the
  convolution and prefilter against a constant environment at every roughness.
  The `hemisphere_irradiance`/`env_brdf_approx` analytic path the entry meant to
  "replace" is now only the graceful fallback when NO environment is bound - a
  deliberate default, not an approximation standing in for the real thing. The
  one piece the entry bundled that is genuinely NOT done is split out below.
- [ ] **MikkTSpace tangents** (S/M) — glTF-baseline tangent generation. Current
  `compute_tangents` (gltf_loader.rs) is the accumulate-average + Gram-Schmidt
  (Lengyel) method, used only when a mesh ships no tangents; it is adequate but
  does not match what DCC tools (Blender, the glTF reference) bake normal maps
  against, so normal-mapped surfaces authored elsewhere can show subtle seams.
  The `mikktspace` crate implements the standard; the real cost is that it
  operates on per-face corners and may split vertices, so it rewrites the
  vertex+index buffers rather than filling a per-vertex tangent in place. Do it
  only if a real asset shows a tangent-basis artefact - the current method is
  not broken, just not the reference.
- [ ] **Web drop-zone / model picker** (S) — native drag-and-drop shipped;
  browser File API pending.
- [x] **Touch controls** (done 2026-07-20) — one finger orbits, two pinch to
  zoom. Ratio-based so the gesture is DPI-independent and reversible; the
  pinch baseline resets on any finger-count change so adding or lifting a
  finger cannot lurch the camera. 7 tests, verified to bite.
- [x] **GPU instancing** (done 2026-07-20) — per-instance transform buffer,
  one identity instance by default so there is a single code path; the
  transform reaches normals and the shadow pass too. `set_instances` /
  `instance_count` on `ForwardRenderer`, 3 tests including one that catches
  copies drawn on top of each other.
- [ ] **Indirect draws** (M) — instancing landed without them. Indirect only
  pays once draw arguments come from the GPU (culling compute, batched
  submission); with CPU-side instance counts it adds a buffer round trip for
  nothing. Revisit alongside GPU occlusion culling below, which is what would
  produce those arguments.
- [ ] **Clustered/tiled lighting** (L) — 4-light cap is fine today; lift it
  when a real scene needs it.
- [x] **GPU occlusion culling** (done 2026-07-21, chosen by the user) — frustum
  culling shipped; this adds occlusion of geometry hidden behind other geometry
  (the Colosseum case). Shipped end-to-end: increment 1 (bbox pipeline +
  QuerySet + resolve + async readback; verified an occluder reports 65536
  samples and a hidden primitive 0), increment 2 (temporal skip in the opaque
  loop; verified an occluded primitive's draw is skipped 1/2 while the visible
  one and the shadow pass are unaffected), a `TimedPass::OcclusionCull` so the
  cull pass cost is in the profile (0.0016 ms measured), and an
  **overlay checkbox** so it is reachable in the app rather than tests-only
  (`occlusion_queries_enabled`, off by default). 153 renderer tests pass.
  Below is the original approach note, kept for the reasoning.

  **Approach: temporal hardware occlusion queries, NOT a Hi-Z depth pyramid.**
  Verified wgpu 27 exposes `RenderPass::begin_occlusion_query`/`end_occlusion_query`
  with a real WebGPU-backend impl (`backend/webgpu.rs`), so it works on the web
  target. A depth pyramid would need max-reduction depth mips and depth
  sampling that WebGPU core does not portably provide — the same portability
  wall that makes single-pass shadows impossible here. Queries sidestep it.

  Data flow: after the forward pass populates depth, run a lightweight pass
  that, per primitive, draws its world AABB as a unit cube (depth-test ON,
  depth-write OFF, no colour) wrapped in `begin/end_occlusion_query(i)`;
  `resolve_query_set` into a buffer, map async, read sample counts one frame
  later; next frame skip primitives whose last-frame count was 0. One-frame
  latency is the accepted cost (standard for this technique; brief pop on fast
  camera cuts).

  Increments: (1) bbox pipeline + QuerySet + resolve + readback of per-primitive
  visibility, tested by reading back that a hidden primitive reports 0 samples
  and a visible one reports >0. (2) temporal skip in the draw loop, tested like
  the shadow-caster cull: an occluded primitive's draw is skipped (stats) while
  the visible one and the shadow pass are unaffected. Off by default
  (`occlusion_culling_enabled`), like `lod_enabled`.
- [ ] **WebXR** (XL) — parked.
- [ ] **Colosseum demo scene** (blocked on you) —
  pick a licensed photogrammetry scan, keep the asset out of git.

  This entry used to claim "LOD + KTX2 machinery is ready" while the LOD
  subsystem was library-and-tests-only, called by no render pass at all. That
  is now genuinely true for LOD (see the render-path item above) — set
  `lod_enabled` before `upload_scene`. **KTX2 is still not ready**: Basis
  ETC1S/UASTC transcoding is unimplemented (`asset/ktx2_loader.rs` rejects any
  supercompression), so a scan shipping Basis-compressed textures will not
  load.

## Cross-renderer

- [x] **Side-by-side timing comparison, first increment** (2026-07-20) —
  `Scripts/Compare-RendererTimings.ps1` runs both renderers headlessly and
  prints per-pass GPU milliseconds in one table: the C++ engine via
  `KATAGLYPHIS_GPU_TIMING_JSON` over the golden harness, the Rust renderer via
  the `dump_gpu_timings` example. Same JSON schema on both sides, one parser.
  **Now same-scene, same-resolution** (second increment, same day): the script
  converts the Dinosaurs OBJ to glTF via the new `obj2gltf` example —
  data-exact, 166563 positions / 894174 indices on both sides — and times the
  Rust renderer on it at the C++ harness's 1200x768.

  | Pass | C++/Vulkan ms | Rust/WebGPU ms |
  |---|---|---|
  | ShadowCascades | 0.067 | **0.119** |
  | Main / Forward | 0.041 | 0.100 |
  | Sky | 0.025 | — |
  | Post / (Ssao+Bloom+Tonemap+…) | 0.041 | 0.072 |

  **First finding from the harness, and its correction the same day:** the
  Rust shadow pass costs 1.8x the C++ one on identical geometry. I attributed
  that to missing per-cascade caster culling, implemented the culling (same
  near-plane-exempt design as #66, tested to engage AND to preserve the
  visible shadow) — and the harness showed ShadowCascades **unchanged at
  0.119 ms**, refuting the attribution: the converted scene is one primitive
  intersecting every cascade, so there was nothing to cull. Both shadow maps
  are 2048², so resolution is ruled out too. The structural difference that
  remains: the Rust renderer runs **three separate shadow render passes**,
  where the C++ engine renders all cascades in one geometry-shader pass.

  **That single-pass approach is NOT portably reproducible, and is not a target**
  (verified 2026-07-20): wgpu's `multiview` needs the native-only MULTIVIEW
  feature, absent from WebGPU core, and WGSL cannot write the render-target
  array-layer index from a vertex shader without it. This renderer targets web
  (`wasm_demo.rs`), and WebGPU has no geometry shaders, so there is no portable
  one-pass equivalent — the three-pass structure is the correct design and the
  ~2× cost is inherent to portable WebGPU, not a deficiency. The per-cascade
  uniform rewrites I also flagged are already gone: the cascade-matrix fix
  replaced them with static per-cascade index buffers. So this line of
  optimisation is closed.

  The culling stays — `considered` scales with primitive count, so it pays on
  multi-object scenes like the Colosseum. Honest gap that remains: camera
  framing differs between the two renderers in the comparison (the dino scene
  has no textures, so that is not a gap — its `.mtl` carries no `map_Kd`).

  **A real correctness bug surfaced while measuring the shadow pass** (fixed
  same day): the cascade index `vs_shadow` projects with was written into every
  primitive's *shared* uniform buffer once per cascade inside a single encoder,
  and `Queue::write_buffer` applies all writes before the command buffer runs -
  so all three shadow layers rendered with cascade 2's matrix while the
  fragment stage sampled them as 0/1/2. Near shadows were mis-projected;
  "a shadow exists" tests could not see it. Fixed with three static per-cascade
  index buffers bound at group(1), correct by construction. Shadowed-pixel
  count on cube_on_plane went 5015 -> 11792.

- [x] **Shader export wired into the build** (done 2026-07-20) — opt-in
  `-ExportWgslShaders` on both `Build-Windows.ps1` and
  `Build-Windows-Container.ps1`, non-critical so a missing cargo toolchain
  warns rather than failing a C++ build. Output is gitignored.
- [ ] **Consume the generated SPIR-V in `VulkanRenderer`** (M) — the export
  pipeline is wired and guarded but nothing reads its output yet, so a WGSL
  change still does not reach the Vulkan engine. The blocker is real and
  documented in `docs/shader-sharing.md`: WebGPU bind groups are not Vulkan
  descriptor sets, so the generated modules' binding decorations have to be
  reconciled with this engine's layout before they can be loaded.
- [ ] **Side-by-side comparison harness** (M) — same scene, same camera,
  Vulkan vs WebGPU screenshot diff; with shared BRDF math this becomes a
  regression net for both renderers (needs C++ glTF + offscreen path above).
- [x] **OBJ→glTF conversion** (done 2026-07-20) —
  `asset::obj_to_gltf::convert_file`, 7 tests round-tripping through the real
  `gltf` loader, including one that converts a real engine asset. Supports
  positions/normals/UVs and fan-triangulated convex faces; rejects relative
  indices, malformed indices and unknown directives rather than dropping them
  silently. Materials carry across as base colour + alpha, one glTF primitive
  per `usemtl` run, sharing one vertex buffer.

  `map_Kd` becomes a glTF image/texture/sampler, deduplicated across
  materials, with the file copied next to the output so the document is
  self-contained.

  Known lossy edge, deliberately: `Ks`/`Ns` are dropped - a Phong-era format
  has no faithful PBR equivalent, and a guessed one would differ from the
  source in a way nobody can audit. Normal, roughness and occlusion maps
  (`map_Bump`, `map_Ns`, `map_d`) are likewise not carried; add them only if a
  comparison actually needs them.

## Dependencies / housekeeping

- [ ] **Upgrade software versions across the whole tree** (unsized, recurring).
  A deliberate sweep to pull dependencies forward, done as one reviewable batch
  rather than piecemeal, so a bump that breaks a build is easy to bisect. Cover:
  - **C++ `ExternalLib` git submodules** — GLFW, imgui, glm, spdlog,
    nlohmann_json, KTX, VMA, tinyobjloader, tomlplusplus, STB, GSL, FUZZTEST,
    etc. Bump each to its latest release tag (not a stray upstream `main`),
    update the `.gitmodules`/pin, rebuild. NOTE the standing drift problem: a
    host tool already nudges GLFW/NLOHMANN_JSON forward off-tag
    (see the FUZZTEST-watcher item below and [[submodule-pin-drift]]) — a
    version sweep should *land those on real tags*, not leave them mid-drift.
  - **CMake `FetchContent` deps** — googletest, abseil, re2 (pulled by
    FUZZTEST). These are version-pinned in the FUZZTEST tree / our CMake; bump
    together since abseil↔re2↔googletest have coupled version expectations and
    abseil's LTS already bit us once (the `fuzzing_bit_gen.h` force-include).
  - **Rust crates** — `cargo update` in both `Cargo.lock`s (main bridge +
    RustProjectTemplate workspace), plus considered major bumps of the pinned
    ones (wgpu 27 → newer, winit, the glTF crate). Re-run `cargo deny`/`audit`
    after — a bump may clear the quick-xml advisory ignored above.
  - **GitHub Actions** — pin-bump `actions/checkout`, `actions/upload-artifact`,
    `actions/cache`, `softprops/action-gh-release`, etc. across all workflows in
    both repos and ContainerHub; prefer SHA pins over floating major tags.
  - **Toolchain/base images** — Vulkan SDK (currently 1.4.341.1), the gcc/clang
    in ContainerHub (see the 22.1.2-vs-22.1.8 split above), Ubuntu base, CMake.
  Do it against the local Rancher container so a break is caught before a
  ~40-min CI round-trip, and land it only once the Linux lanes are green so a
  version regression is distinguishable from the pre-existing outage.

- [x] **cargo-deny advisories** (resolved 2026-07-21, but revisit the call) —
  `quick-xml 0.39.4` RUSTSEC-2026-0194/0195 and unmaintained `ttf-parser`.
  These were **deliberately left unignored to stay visible** — which was free
  while CI was red and never reached the security step. Now that the
  CARGO_HOME fix makes `cargo audit`/`cargo deny` run as a HARD gate, an
  unignored advisory fails the whole lane, and cargo-deny/audit offer only
  full-ignore, not per-advisory warn. So keeping them visible and having a
  green lane are mutually exclusive. Ignored both quick-xml IDs in `deny.toml`
  + `.cargo/audit.toml` with a justification: quick-xml is a BUILD-TIME
  dependency of `wayland-scanner` (winit → Wayland), parsing the trusted
  protocol spec, never attacker-controlled runtime data, so the DoS advisories
  are unreachable here. `ttf-parser` was already a non-failing warning.
  **If you'd rather the lane fail-visibly on advisories than pass with a
  documented ignore, revert the two ignore entries** — that is a
  green-vs-visible preference, not a correctness question.
- [x] **FUZZTEST checkout watcher — no watcher found** (investigated
  2026-07-20) — the submodule's reflog holds 14 entries, all between
  2026-07-15 and 2026-07-18, clustered into three working sessions, with
  nothing in the two days since. No hook, no CMake `FetchContent`, no script
  and no `.gitmodules` branch setting references those date tags, and
  `submodule.<name>.branch` is unset, so `git submodule update --remote`
  cannot be the cause either. VS Code does have the submodule registered as a
  repository (`branch.main.vscode-merge-base` is set in its local config), so
  its Git UI is the most plausible route — but that is a human action, not a
  daemon. Best reading: hand or agent experimentation during those sessions,
  misremembered as something recurring.

  Rather than keep hunting, `Scripts/Windows/tests/Submodule.Pins.Tests.ps1`
  now detects the symptom whatever the cause: any submodule checked out away
  from its recorded commit (the easily-missed `+` in `git submodule status`),
  plus a check that the FUZZTEST pin is reachable from its remote so local-only
  drift cannot produce a build that works on one machine. Verified by
  deliberately drifting the submodule and watching it fail.

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
- **GPU-side numbers already exist**: per-pass timestamps land in
  `GUIRendererSharedVars::gpuTimings` (GUI "GPU timings" header). A headless
  mode that renders N frames and dumps the per-pass averages as JSON would
  turn them into a comparable artifact instead of a number a human squints
  at. Nothing asserts a budget for `GpuTimedPass::ShadowCascades` today.
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
  asset-loading item above — it was previously argued from first
  principles only.
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
  works on Linux (`linux-debug-tsan-clang`), which CI runs. Decide whether to
  rename the Windows preset to something that does not promise TSan, or drop
  it; leaving it named `tsan` invites exactly the false assurance above.
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
- Headless offscreen assertions in the C++ engine — **re-scoped, not done.**
  "What is thin is the set of assertions" was written when
  `goldenRenderSuite.cpp` had one or two tests; it now carries six, including
  `DeferredMatchesForwardRoughly`, `FrustumCullingDropsOffscreenMeshesOnly` and
  `SecondModelLoadsAndRenders`. What is still genuinely missing is coverage of
  the *shadow* path beyond the single darkened-pixel ratio, and of the
  post-processing chain.

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

## CI and release gaps

- [x] **TSan build failed on an ASan/TSan flag conflict** (fixed 2026-07-21,
  verifying). The `Configure/build with ThreadSanitizer` step (added 2026-07-19)
  runs right after the fuzzer and *before* the profiling and all gcc lanes, so
  its failure silently skipped everything after it - including the ccache-fixed
  gcc benchmarks. Only visible once the fuzzer went green and the run reached
  this step. Root cause: `Src/GraphicsEngineVulkan/CMakeLists.txt` applied
  `-fsanitize=address` to `VulkanEngineCore` for *every* Debug+Linux build
  (added for the fuzz-ODR project-wide-ASan need), but the `linux-debug-tsan-clang`
  preset also reaches the engine with `-fsanitize=thread` via `myproject_options`
  - and clang rejects the pair (`invalid argument '-fsanitize=address' not
  allowed with '-fsanitize=thread'`), so the engine would not compile at all
  under TSan. Fix: gate that hardcoded ASan on
  `NOT myproject_ENABLE_SANITIZER_THREAD`, leaving the ASan and plain-debug
  lanes byte-for-byte unchanged and letting TSan stand alone. Reproduced and
  fixed in the container. (The TSan *run* step is next; a non-instrumented Rust
  static lib may still surface false races there - a separate concern from the
  build.)

- [x] **All gcc CI lanes broken by a bad ccache env** (fixed + verified
  end-to-end 2026-07-21). `benchmarks (gcc)` had been red since 2026-04-19 and
  the gcc unit/integration lane was latently broken the same way. Root cause:
  the `:latest-cross` image (`ContainerHub linux/Dockerfile.package`) sets
  `CCACHE_SECONDARY_STORAGE=true`, but that variable is ccache's `remote_storage`
  and must be a URL — ccache parses `true` as one and aborts EVERY compile with
  `URL scheme must not be empty: true`. Only the gcc presets use ccache; the
  clang presets use sccache, which ignores `CCACHE_*`, so clang lanes were
  unaffected and the breakage looked gcc-specific. Reproduced in the container
  (`ccache gcc -c` fails as shipped, succeeds with the var unset). Two-part fix:
  removed the env at source in `Dockerfile.package` (needs an image rebuild to
  land), and added a guard in `Scripts/Linux/cmake-configure-build.sh` that
  unsets any `CCACHE_SECONDARY_STORAGE` that is not a URL, so the currently
  deployed image works without waiting for a rebuild. Verified end-to-end in the
  container: the `linux-profile-GNU` build compiles (gcc handles the C++20
  modules), links, and `perfTestSuite` runs all 9 benchmarks to completion
  (exit 0). This was the last known-masked CI layer after the fuzzer, which is
  itself now confirmed green in CI (`Run fuzzer tests => success` on the fuzz-fix
  run). (Two benign image gaps surfaced alongside: `libprofiler not found`
  falls back to gprof, and `cppcheck requested but executable not found` skips
  that analyzer - neither fails the build.)

- [x] **Linux amd64 CI runs clang 22.1.2, not the pinned 22.1.8** (fixed by the
  user elsewhere, 2026-07-21 — do not re-raise). Kept below for the root-cause
  record. ContainerHub `LLVM_RELEASE=22.1.8` drives a
  from-source `llvmorg-22.1.8` build, and arm64/riscv64 get it — but
  `linux/Dockerfile.sdk` (~L69) copies the distro apt `clang-22`
  (`1:22.1.2-1ubuntu1`, at `/usr/lib/llvm-22`) as `/opt/llvm-target` for amd64
  "because it is native", so `:latest-cross` amd64 ships 22.1.2. Confirmed in
  the container: `/usr/local/llvm-target/bin/clang` reports
  `Ubuntu clang version 22.1.2 (1ubuntu1)`, `dpkg` shows `clang-22
  1:22.1.2-1ubuntu1`, and no `llvmorg-22.1.8` binary exists in the image. Net:
  a version split — amd64 on 22.1.2, cross arches on 22.1.8. Options: (1) build
  22.1.8 from source for amd64 too (slower image, all arches match); (2) drop
  the pin to `LLVM_RELEASE=22.1.2` to match reality (cross arches then also
  22.1.2); (3) accept and document the split. Needs a multi-hour rebuild to
  verify whichever is chosen. (Corrects the memory note that claimed the image
  matched the host's 22.1.8 - that is true for the Windows :winamd64 image, not
  the Linux cross image.)

- [x] **Linux fuzzer step: `scene_config_fuzz_test` SEGV'd at startup** (fixed
  2026-07-21, verifying in-container). With the mtime-exclude fix the
  unit/integration step went green for the first time since the outage, which
  finally let the `Run fuzzer tests` step run — and it died on the third binary
  with a bare `SEGV on unknown address 0x000000000000` (null read) AFTER
  "Sanitizer coverage enabled" but BEFORE the first `[ RUN ]`. Same *shape* as
  the abseil-ODR SEGV but a different *cause*: `scene_config_fuzz_test` was the
  first fuzz binary to `target_link_libraries(... VulkanEngineCore)`, so it was
  the first to run the whole renderer's global constructors — Vulkan dynamic
  dispatch, ImGui/GLFW statics — inside a windowless fuzztest `main` with no
  Vulkan instance, and one of them dereferenced null during static init.
  `first_fuzz`/`obj_parsing` never link the engine, so they passed. Fix: the
  scene_config module is self-contained (glm + std::filesystem, no other engine
  import), so `Test/fuzz/CMakeLists.txt` now compiles the two scene_config TUs
  straight into the fuzz executable instead of linking VulkanEngineCore — the
  path fuzzer gets its unit under test and nothing else, mirroring obj_parsing's
  minimalism. (A path-string fuzzer has no business constructing the renderer.)

- **Latent: a `VulkanEngineCore` global constructor faults in a headless
  process** (found 2026-07-21, unsized). Surfaced by the fuzz SEGV above: some
  engine global ctor null-derefs when it runs without the app's `main()` having
  initialised GLFW/Vulkan first. The shipping app is fine (its init order holds),
  and the fuzz fix above stops *linking* it into the fuzzer, so this is not
  blocking — but a global that assumes app init is a real fragility (it would
  bite any future headless/tool use of the engine). Worth symbolizing once (build
  `scene_config_fuzz_test` the old way in the ASan container and run under
  `llvm-symbolizer`) to name the exact ctor, then either make it lazy or guard it.

- [x] **ContainerHub shell scripts checked out CRLF on Windows** (fixed
  2026-07-21) — the 199 `*.sh` scripts are stored LF but ContainerHub's
  `.gitattributes` had no `*.sh` rule, so `core.autocrlf=true` (Windows default)
  flipped them to CRLF on checkout. Invisible until the repo is bind-mounted
  into a Linux container (the documented Rancher Desktop workflow): bash chokes
  on the trailing `\r` sourcing `logging.sh` (`$'\r': command not found`), the
  `info/warn/err` helpers never define, and every script dies with
  `info: command not found`. Added `*.sh text eol=lf` (+ `*.bash`); blob content
  unchanged (index was already LF). ContainerHub `14f1c38`, pin bumped here.

- [x] **Stay on the 8.7 GB `:latest-cross` image** (decided by the user 2026-07-20)
  (researched 2026-07-20). Repeatedly observed today: `Pull container image`
  stalling 40+ minutes, dwarfing the build. The lane pulls the full
  `:latest-cross` (gstreamer, opencv, ffmpeg, onnx, torch, android SDK) to run
  a clang/cmake/vulkan/rust build + fuzz tests. Measured alternatives on ghcr:
  `:toolchain` is **3.2 GB** (16 layers) and `:compiler` 3.3 GB, both vs 8.7 GB
  / 49 layers — a ~63% pull reduction.

  **Not a safe drop-in, and the blocker is identified.** The C++ engine itself
  has zero media/ML deps (grepped), but the build runs with `RUST_FEATURES=ON`,
  which integrates the Rust project via corrosion. The media crate's gstreamer
  support is feature-gated OFF by default (`crates/media/Cargo.toml`), so the
  question is precisely which features `RUST_FEATURES=ON` activates and whether
  any pull system libs (gstreamer/opencv) or heavy crates (onnx/torch) absent
  from `:toolchain`. Resolving that, then switching `CONTAINER_IMAGE`, is worth
  a dedicated validation cycle — deferred rather than stacked on the in-flight
  fuzz-fix run. If the toolchain image suffices, every future Linux run gets
  ~5 GB lighter and materially more reliable.

  **Decision: keep `:latest-cross`.** The user chose the full image over a
  slim-image switch, so both Linux lanes stay on it and the pull cost is
  accepted. The research above is kept for the record, not as an open action.

- [x] **The Rust template's Ubuntu lane was also silently red** (fixed
  2026-07-20) — every visible run failed with `cargo_debug.sh: No such file or
  directory`: ContainerHub reorganised its scripts into numbered directories
  and the workflow kept the old `linux/scripts/rust/` paths (the packaging
  step had been migrated, so it was a partial migration). Eight paths updated,
  and the lane moved from the stale `:latest` image to `:latest-cross` like
  the main repo. Found by pointing `gh` at that repo's pipeline for the first
  time — same lesson as here: a lane nobody reads is a lane that stays red.
  ContainerHub's own pipeline checked the same way: green.

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
- **Docs builds are unverified.** Sphinx/Doxygen output is deployed by
  `Linux.yml` but nothing checks for broken links or missing pages first.
- **Golden-image CI** for the Rust renderer: the headless tests already
  render; storing reference images per GPU vendor would catch shader
  regressions that structural assertions miss (they were designed to be
  driver-independent, which is also their blind spot).

## Startup and build-time costs

- [x] **GLSL is NOT recompiled at every startup** (stale entry, corrected
  2026-07-20). This item asked for exactly the behaviour `ShaderHelper` already
  has: it consumes the prebuilt `Resources/Shaders/**/spv/*.spv` and falls back
  to runtime compilation only when the source is newer than the SPIR-V. That
  landed with the "never run stale shaders" fix, which replaced an
  existence-only check - under which every edit after the first was silently
  ignored.

  Verified rather than assumed: a startup logs **18 "SPV up to date, skipping
  runtime compile" and 0 recompiles**. Nothing to do here.
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
- **A source file deleted on the host keeps building inside the reusable
  container.** Reproduced 2026-07-19: added a probe test, built (it ran),
  deleted the file, rebuilt — the test still ran, and the `.cpp` was still
  present at `C:\ws\...` inside the container. The inbound `tar` extracts
  over the existing tree and never prunes, so tests can keep passing against
  code that no longer exists, and a file whose deletion breaks the build
  looks fine locally and fails in CI.

  Workaround today: `-FreshContainer`, or delete the file inside the
  container. Proper fix (**not yet implemented — do this deliberately, not
  in a hurry**): prune the source tree inside the container before streaming,
  keeping `build-*` and `logs`. Sources re-stream in seconds; only the build
  tree is expensive, and that is what must survive. The risk is that a
  wrong pattern deletes the build tree on every build, so it needs a careful
  exclusion test before it goes in.

## Architecture debt not yet sized

- **`VulkanRenderer` is still the hub.** PipelineBuilder (-416 lines) and
  DescriptorSetGroup (-617) shrank it a lot, but it still owns the
  swapchain, sync objects, UBOs, five stages and four foreign pointers
  (`Window*`, `Scene*`, `GUI*`, `Camera*`). Candidate extractions:
  `FrameSync` (fences/semaphores/frame index), `SwapchainTarget`
  (swapchain + framebuffers + recreation), a stage registry so adding a
  pass does not mean editing the renderer.
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
- **Wasm size budget**: the demo payload is ~3.7 MB uncompressed and
  nothing tracks it; `wasm-opt -Oz` plus a CI size gate would keep the
  Sphinx-hosted demo honest.

## Housekeeping candidates

- The `x64-Clang-Windows-Release` preset survives only because
  `windows-clang-release-wix` packages from it; if WiX packaging moves to
  ClangCL, that preset can go too.
- `imgui.ini` is tracked and changes whenever a window is dragged — decide
  whether it is source (layout you want shipped) or user state (gitignore).

---

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
