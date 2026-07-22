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

- [x] **glTF loading** (MVP done 2026-07-21 on `feature/gltf-loading`; textures
  deferred) — the C++ engine now loads `.gltf`/`.glb`. Shipped increments a/b/c/
  e/f: cgltf v1.15 submodule; `GltfLoader::parseCpu` (positions/normals/UVs,
  baked node transforms, indices) → the same `Vertex`/index/`ObjMaterial`/
  materialIndex arrays `ObjLoader` produces; per-glTF-material mapping
  (`baseColorFactor`→diffuse, roughness→lossy specular, emissive) with per-face
  materialIndex; `GltfLoader::loadModel` + `Scene::loadModelByExtension` dispatch
  (`.gltf`/`.glb`→GltfLoader, else ObjLoader, case-insensitive, OBJ path
  untouched). Verified in-container: 3 `GltfParseUnit` tests pass on the Rust
  renderer's `cube.glb` (copied to `Resources/Models/GltfTest/`, so both
  renderers load the SAME asset - the comparison harness's shared-input half is
  now unblocked). **Merged to develop** (2026-07-21) — the MVP+textures arm in
  `56688a09` and the async arm in `efce2090`; `feature/gltf-loading` is fully
  contained in develop (`develop..feature/gltf-loading` is empty).

  Two follow-ups, deliberately out of the MVP:
  - **(d) textures DONE** (2026-07-21) — glTF base-colour images now load.
    - Part 1 `Texture::createFromMemory` (`stbi_load_from_memory` → the shared
      `uploadRgba` extracted from `createFromFile`, OBJ path behaviour-preserved).
    - Part 2 `GltfLoader`: `parseCpu` pulls each material's `baseColorTexture`
      bytes (glb `image->buffer_view`; data-URI base64 via
      `cgltf_load_buffer_base64`) into `textureImages` + sets `ObjMaterial`
      `textureID`; `loadModel` decodes+uploads via `createFromMemory` in order
      (default when none), matching the OBJ path.
    - Verified: the `ExtractsAnEmbeddedBaseColorTexture` test decodes
      `cube_textured.gltf`'s data-URI PNG to real bytes (PNG signature) with a
      valid textureID. The CPU extraction is proven headless; the GPU upload is
      compile-verified. Only remaining, if ever wanted: eyeball the textured
      result on a GPU host (not headless-checkable) - the mapping is standard so
      this is low risk.
  - **async glTF DONE** (2026-07-21 on `feature/gltf-loading`; verified) — glTF
    now loads off the render thread exactly like OBJ.
    - **Foundation** (verified compiling): `GltfLoader` split into
      `parseCpu` + `uploadParsed` + `adoptParsed`, mirroring `ObjLoader`, so a
      device-free worker can parse and a device-owning loader upload.
    - **Increment 2, done non-breakingly**: rather than CHANGE
      `AsyncModelParse::takeResult() -> unique_ptr<ObjLoader>` (which would have
      forced a rewrite of every `AsyncModelParseUnit` OBJ test), the OBJ arm is
      kept byte-identical and a PARALLEL glTF arm added: `AsyncModelParse` now
      dispatches by extension (`isGltfPath`), holds an optional
      `unique_ptr<GltfLoader>` beside the OBJ one, and exposes `parsedGltf()` +
      `takeGltfResult()`. `Scene::pollModelLoad` branches on `parsedGltf()`; the
      two upload arms are symmetric via `adoptParsed`/`uploadParsed`. The five
      existing OBJ async tests are untouched and still pass.
    - **Verified** (container, `linux-debug-clang`, 2026-07-21): all 6
      `AsyncModelParseUnit` tests pass, including the new
      `RoutesGltfToTheGltfLoaderOffThread` (a `.glb` routes to GltfLoader on the
      worker and matches a device-free reference parse). TSan already confirmed
      the OBJ worker race-clean; the glTF arm uses the identical
      release/acquire + join-not-detach structure.
    - **Merged to develop** (2026-07-21, in `efce2090`) — landed with the rest of
      `feature/gltf-loading`.

  Original scoping note kept below for the reasoning:

  **Integration point.** `ObjLoader::parseCpu` produces exactly
  `vector<Vertex> + vector<index> + vector<ObjMaterial> + materialIndex +
  textures`, and `Model`/`Mesh` consume only those (`Mesh.ixx` ctor takes
  vertices/indices/materialIndex/materials). So a `GltfLoader::parseCpu` that
  emits the same five vectors plugs into the existing device-side Model/Mesh
  build and the `AsyncModelParse` worker unchanged - no renderer changes needed.
  The engine `Vertex`/`ObjMaterial` are the interchange format.

  **Library.** Add `tinygltf` as an ExternalLib submodule (header-only C++,
  the natural parallel to the existing header-only `tinyobjloader`; pin the
  latest release tag - bleeding-edge). cgltf is the lighter-C alternative if
  tinygltf's stb/json bundling clashes with the engine's own stb/nlohmann.

  **Increments:** (a) submodule + `ExternalLib/CMakeLists.txt` wiring, guarded
  so it does not double-define stb/json. (b) `GltfLoader::parseCpu`:
  positions/normals/UVs/tangents → engine `Vertex`, indices flattened across
  primitives (mirror the Rust `gltf_loader`, including tangent generation when
  absent). (c) materials: pbrMetallicRoughness base colour + textures →
  `ObjMaterial` + `textures` + per-primitive `materialIndex`. (d) node
  hierarchy: bake glTF node transforms into vertex positions (the OBJ path has
  no nodes, so flatten). (e) dispatch in `Scene::loadModel` by extension
  (`.obj`→ObjLoader, `.gltf`/`.glb`→GltfLoader), reusing the async path. (f)
  test: load the Rust renderer's `cube.gltf`/`cube.glb` and assert vertex +
  material + primitive counts - the shared-asset goal, and the first half of
  the comparison harness. Best started fresh, not mid-CI-verification: each step
  needs a ~20-min container build and a main-repo push.
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

- [ ] **Basis ETC1S/UASTC transcoding** (M, **blocked on a transcoder + a test
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

- [x] **Per-pixel alpha-tested shadows** — **DONE (2026-07-22, RPT d2aafae,
  push held for the Windows-lane verdict)**: MASK casters with a real
  base-color texture route through an alpha-testing shadow pipeline (same
  dedup-cached view/sampler the forward pass binds; cull off for single-sided
  cards; hot-reload covered). The card test the reverted attempt prescribed
  discriminates: green 11016 -> 6608 shadowed pixels, red bit-identical.
  Its oracle survived three measured failures (card body matched the colour
  signature; SSAO tracked the already-alpha-tested forward depth and halved
  the red state; lit card top is mid-grey vs near-black true shadow) - all
  documented in the test. Historical note below. — textured MASK materials cast by base-alpha only, so a foliage
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
- [x] **MikkTSpace tangents** (done 2026-07-21 on `feature/mikktspace-tangents`,
  opt-in) — `generate_tangents_mikktspace` (gltf_loader.rs) is the glTF-reference
  basis DCC tools (Blender, the glTF exporter) bake normal maps against, alongside
  the default Lengyel `compute_tangents`. It runs per face-corner and SPLITS
  vertices where a shared vertex's corners disagree (hard UV seam / mirrored
  island), so it returns fresh vertex+index buffers; corners MikkTSpace treats as
  shared weld back. Opt-in via `KATAGLYPHIS_MIKKTSPACE_TANGENTS` (default off -
  Lengyel stays the default), and only for meshes whose tangents we generated (a
  file shipping its own tangents keeps them).

  Uses **`bevy_mikktspace 1.0`** - the pure-Rust, ZERO-dependency port - NOT the
  `mikktspace` crate, which pulls the stale `nalgebra 0.26` (future-incompat on
  current rustc). 3 unit tests pass (+X unit tangent with +1 handedness on aligned
  UVs, -1 on a mirrored chart, degenerate input rejected); `cargo check -D warnings`
  clean. **Push `feature/mikktspace-tangents` + PR when convenient** (held so it
  does not stack on the in-flight CI-recovery runs). The GPU-visual benefit
  (matching a DCC-baked normal map) still wants eyes on a real normal-mapped asset,
  which is why it is opt-in rather than the default - but the tangent maths is
  unit-verified.
- [x] **Web drop-zone / model picker** (done 2026-07-22, RPT 62e215c) — browser
  drag-and-drop via the DOM File API (winit-web never delivers DroppedFile);
  first dropped .glb read async, uploaded with native-viewer semantics; page
  header carries the hint. Native drag-and-drop was already shipped.
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
    ones. **wgpu 27→29 + egui 0.33→0.35 + naga 26→29 DONE** (2026-07-21, on
    RustProjectTemplate `develop` — now the repo's default+integration branch;
    85 tests pass, `--workspace --all-targets` clean). Still open: winit, the
    glTF crate. Note the `--all-features` clippy step needs the `:latest-cross`
    image rebuilt so its rustc reaches 1.96 (`kstring 2.0.4` MSRV; the ContainerHub
    source already pins RUST_VERSION=1.96.0). Re-run `cargo deny`/`audit`
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
  are unreachable here. **Update 2026-07-21:** `ttf-parser` stopped being a mere
  warning - it got its own hard advisory `RUSTSEC-2026-0192` (unmaintained) that
  failed the Ubuntu security step, so it is now ignored by ID too (it is a
  transitive font-parsing dep of the winit/egui text stack; unmaintained is
  informational, no maintained drop-in). Verified locally with cargo-audit +
  cargo-deny (`advisories ok`).
  **If you'd rather the lane fail-visibly on advisories than pass with a
  documented ignore, revert the ignore entries** — that is a
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
  fixed in the container. TSan build now confirmed passing end-to-end (full
  982-step build, exit 0).

  The TSan *run* step is separate. Reproducing it locally under Rancher/nerdctl
  hits `FATAL: ThreadSanitizer: encountered an incompatible memory layout but
  was unable to disable ASLR (perhaps sandboxing is enabled?)` - TSan re-execs
  with `personality(ADDR_NO_RANDOMIZE)` to lay out shadow memory, and nerdctl's
  seccomp profile blocks that syscall. This is very likely LOCAL-only: GitHub
  Actions' default docker seccomp allows `personality`, so CI's TSan run should
  not hit it (and CI never reached this step before - the build failed first).
  If CI *does* hit it, the fix is `--security-opt seccomp=unconfined` on that
  step's `docker run`. Confirmed locally with that option: **100% tests passed,
  0 failed under TSan** (2.53 s), `AsyncModelParseUnit` included - the real C++
  concurrency the run exercises (`AsyncModelParse` parses OBJ off a worker thread
  and hands back to a frame-loop poller; objParseSuite.cpp) is race-clean, which
  matches the code by inspection (loader/path written before the worker is
  spawned = happens-before; result flags atomic release/acquire; destructor
  joins, never detaches). So the TSan lane is good end to end - build fixed +
  run race-clean; the only wrinkle is the local sandbox quirk CI does not share.

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

- [ ] **Windows CI: the `:winamd64` image is 54 GB and exhausts the runner**
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

  **Fix (owner builds the image):** `build.ps1 -Stages base,toolchain` WITHOUT
  `-Gpu` (the non-`-Gpu` lane makes the CUDA/nvidia stage a no-op `docker tag`,
  and stopping at `toolchain` skips the media stack) → a few-GB `:winamd64-toolchain`;
  repoint `GHCR_IMAGE_WIN`/the `:winamd64` refs in `Windows.yml` at it. Mirrors the
  Linux `:toolchain` split. Relocating Docker data-root to a bigger drive only helps
  if a drive has >54 GB free — the diagnostic will say. Almost certainly the slim
  image is the only reliable fix.

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
  (The build+deploy itself is now green - the `Build web page` uv/venv fix and
  the FTP `Sync files to domain` both passed on 2026-07-21 - so a link/page
  check is the remaining gap.)
- [x] **Brand the WebGPU demo page** (done 2026-07-21) - the standalone
  `crates/webgpu_renderer/web/index.html` (embedded as an iframe in
  `docs/source/webgpu_demo.md`) now mirrors the site's Kataglyphis brand from
  `docs/source/_static/css/custom.css`: the mint gradient header
  (#6af0ad->#2ad488), accent palette, green radial background and a pill status
  badge. Behaviour unchanged. Pushed to the RustProjectTemplate repo.
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


1. **Shadow casters in front of a cascade's near plane are clipped away** (S/M) —
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
9. **Cascades refit per frame with no texel snapping — shadow edges crawl** (M) —
   `computeCascadeData` derives the box from exact frustum corners each frame
   (`CascadedShadowMap.cpp:201-216`) so it translates AND resizes; the
   stabilisation ingredient (`radius`, `:187-190`) is already computed but only
   used for eye placement. Fix: size from `radius`, snap origin to whole texels.
   Test: two camera positions a fraction of a texel apart — origins must differ by
   an exact texel multiple and box width must be identical. Both fail today.
10. **A `Model` can hold exactly one `Mesh`** (L) — `getMeshCount()` returns literal
    `1` and `getMesh()` ignores its index (`Model.ixx:38-39`); `add_new_mesh`
    overwrites (`Model.cpp:47`). Culling is all-or-nothing on one scene-sized AABB
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
11. **glTF loader gaps** (M) — skinned-node transforms are applied though the spec
    says ignore them (`GltfLoader.cpp:231`); missing `NORMAL` becomes a constant
    `(0,1,0)` instead of computed flat normals (`:265`); non-triangle primitives are
    silently skipped (`:237`); `alphaMode`/`doubleSided`/`KHR_texture_transform`/
    texcoord index all ignored, so transparent glTF renders opaque.
12. **Point lights are wired on the GPU but never fed; `OmniDirShadowMap` renders
    nothing** (M) — `lighting.frag` loops `numPointLights`, which
    `updateUniforms` never writes; the cube depth target allocated at init is
    never recorded into and never sampled. Same "dead pass" class the CSM work
    already caught once. Either finish it or delete it.

    **DECISION BRIEF (2026-07-22, prepared for the call - this is a user
    decision):**
    - *Measured state:* `numPointLights` is written NOWHERE (the deferred loop
      is dead code at runtime); the FORWARD shader has no point-light code at
      all, so enabling even one light today instantly breaks
      `DeferredMatchesForwardRoughly` (threshold 1.0, current parity 0.20);
      no GUI controls exist; `OmniDirShadowMap` (132 lines + 3 shaders) burns
      a 1024x1024 cube depth allocation per run for zero output.
    - *Option A - DELETE (S, ~1 session):* remove OmniDirShadowMap + the
      `omni_shadow_map.*` shader trio (already flagged in the dead-shader
      audit), `pointLights`/`numPointLights` from SceneUBO + the deferred
      loop. Frees the allocation, shrinks SceneUBO, kills the parity trap.
      Re-adding later costs the same M as finishing now - nothing rots.
    - *Option B - FINISH MINIMAL (M, no shadows):* GUI list (add/remove,
      position/color/radiance), upload count+array, ADD THE FORWARD PATH
      (parity!), extend the parity golden to a point-lit scene. Omni shadow
      map stays deleted (Option A for it) until someone wants point SHADOWS.
    - *Option C - FINISH FULL (L):* B + render/sample the cube shadow map;
      6 faces x N lights of depth passes wants its own perf budget.
    - *Recommendation:* A or B; the half-alive state is the only wrong
      option - it costs VRAM, misleads readers, and arms the parity trap.
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
   - **PT dispatches before the TLAS exists** (S): with PT enabled during the
     async model load, the kernel dispatches against never-written descriptor
     sets (20 validation errors in the pre-load window of the golden run).
     Guard the PT/RT record branch on a built TLAS.
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

**DONE 2026-07-22 - dead shader set deleted** (20 files: g_buffer_* pair+spv, CloudsRectangle, noise_texture_{32,128}_res, loading_screen/ entire). `omni_shadow_map.*` deliberately RETAINED - equally dead, but entangled with item #12 (finish-or-delete the point-light system); decide once, for class and shaders together. `generated/` is NOT dead - those are the Rust renderer WGSL->GLSL exports. Original item text: `rasterizer/g_buffer_*`,
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
   (S) - `g_buffer_lighting_pass.frag:215-216` ends with Reinhard + gamma, then
   `post.frag:32-34` re-applies both. Forward writes raw color and is correct,
   so deferred renders crushed/washed vs forward. Fix: delete the two lines.
   Test: tighten `GoldenRender.DeferredMatchesForwardRoughly` to mean-luminance
   tolerance; fails today.
2. **G-buffer material-id is UNORM, every index collapses to 0/1** (S) - the
   geometry pass writes `g_material_id = vec3(mat_ID)` into `eR8G8B8A8Unorm`
   (`DeferredRasterizer.cpp:92,:209`), so `mat_ID >= 1` clamps to 1.0 and
   `SKYBOX_MATERIAL_ID = 35` / `CLOUDS_MATERIAL_ID = 36` can never round-trip -
   sky and cloud pixels get lit as geometry in deferred mode. Fix: `eR8Uint`/
   `usampler2D` (or normalize by MAX_MATERIALS+2).
3. **G-buffer never samples albedo textures** (S) - the texture fetch is
   commented out on the assignment line in `g_buffer_geometry_pass.frag`
   (`g_albedo = materials[mat_ID].diffuse;//texture(...)`), so Sponza renders
   flat per-material color in deferred while forward shows textures.

**CPU-testable robustness/coverage (the now-green Windows CI can gate these):**

4. **First-frame delta_time is unbounded** (S) - `last_time` starts at 0.0
   (`App.cpp:32-33`) so the first `update_frame_timing` returns the whole
   startup wall-clock (seconds); a key held during load lurches the camera.
   Seed or clamp; pure gtest.
5. **Single-time command buffers are never freed** (S/M) -
   `CommandBufferManager.cpp:30-38,:113-115` allocates per upload and
   deliberately never frees ("Avoid explicit free"), and nothing resets the
   pool - every texture/buffer upload and AS build leaks a command buffer for
   the session, plus a fresh fence per submit (`:81-108`). GUI model reloads
   multiply it.
6. **Input handling + frame timing have ZERO tests** (S/M) -
   `WindowInputCallbacks.ixx:24-83` and `FrameInput.ixx:9-21` are pure,
   device-free, and route all input into the camera; no suite in Test/commit
   references them. This is also where #4 gets its regression guard.

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
9. **Acceleration structures are never compacted** (M) - BLAS/TLAS built with
   `ePreferFastTrace`, no `eAllowCompaction`, no size query/copy anywhere
   (`ASManager.cpp:207,:335`). Compaction typically reclaims a large fraction
   of BLAS memory - it is the VRAM headroom for multi-object scenes.
10. **RT output image is `rgba8`** (S) - `raytrace.rgen:26` clamps the traced
    result to 8-bit LDR before post ever sees it; the RT analogue of the
    UNORM-lit-target item. `rgba16f` + `R16G16B16A16Sfloat` target.
11. **GUI render-mode radios live in function-local statics** (S) -
    `GUI.cpp:121,:132` hold the mode in `static int`, decoupled from
    `GUIRendererSharedVars` - the display can desync from renderer state and
    cannot be config-driven. Move into the shared vars; extend the round-trip
    suite.

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

**Status 2026-07-22 — 10 of these are DONE** (RustProjectTemplate develop, each
with its own test unless noted): #2 scene bounds track instances, #3 one
`world_center` metric, #5 instanced normals via cofactor, #9 all three
degenerate-input paths, #11 `KHR_materials_unlit`, #12 anisotropic filtering,
#15 both the 16-bit down-convert and strip/fan triangulation. Two shipped with
an explicit "could not prove it" note rather than a test that cannot fail: #1
(the occlusion guard - from inside a closed back-face-culled mesh nothing
renders, so depth stays empty and the proxy box passes regardless; reaching the
bad path needs interior/double-sided geometry no fixture has) and #14
(bloom/SSAO skip - with the pass skipped its timestamp slots go unwritten, so
the resolved average is a stale-slot delta, and the timing API cannot tell "did
not run" from "ran briefly").

Still open here: #4 texture dedup at upload (the VRAM ceiling for Colosseum),
#6 `COLOR_0` vertex colours, #7 per-slot texcoord sets, #8 MSAA, #10 the
oversized per-primitive uniform block, #13 render bundles for the cascades.


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
6. **`COLOR_0` vertex colours are silently dropped** (M) — the loader never calls
   `read_colors` (`gltf_loader.rs:330-392`) and `Vertex` has no colour field. This
   is the most common way real assets carry colour without a texture
   (photogrammetry, CAD, low-poly packs, baked AO); they render uniformly white.
7. **Every texture slot is forced onto TEXCOORD_0** (M) — `textureInfo.texCoord` is
   never read and only `read_tex_coords(0)` is loaded (`gltf_loader.rs:339-342`);
   `texture_ref` deliberately discards the `Info` carrying the index (`:238-251`).
   Assets with baked AO on UV1 — the standard Blender/Substance export — get it
   sampled with albedo UVs. Rider: `KHR_texture_transform` is plumbed for base
   colour only (`:444-459`).
8. **No anti-aliasing anywhere** (M) — `MultisampleState::default()` on all four
   forward pipelines (`forward.rs:2139`, `:2182`, `:2220`) and `sample_count: 1` on
   the HDR target (`:2814-2831`). The most visible quality defect in the browser
   demo. 4x MSAA resolved before bloom/SSAO/tonemap is portable WebGPU-core; SSAO's
   `textureLoad` on depth is the one design constraint.
9. **Degenerate/NaN input poisons the whole frame** (S/M) — a zero-scale node
   (Blender's standard hide) makes `model.inverse()` NaN → NaN normal matrix
   (`forward.rs:1296`); ONE non-finite POSITION makes `scene_radius` NaN → all three
   cascade matrices NaN, breaking shadows for every object (`:2348-2362`,
   `:2556-2574`), and `Frustum::test_planes` treats NaN as visible (`:2269-2281`);
   a cyclic `parent` chain recurses forever in `compute_world_transforms`
   (`scene/mod.rs:384-405`) — stack overflow, not an error. All three are pure unit
   tests.
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
