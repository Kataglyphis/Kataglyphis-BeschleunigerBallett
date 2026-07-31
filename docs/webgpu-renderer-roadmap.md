# WebGPU Renderer — Feature Roadmap

Continuation of [webgpu-gltf-rust-plan.md](webgpu-gltf-rust-plan.md) (milestones
1–5 shipped 2026-07-18: context + surface lifecycle, glTF meshes, textures +
ACES tonemapping, directional shadows, wasm32 browser demo). This roadmap
covers everything after. Crate:
`ExternalLib/Kataglyphis-RustProjectTemplate/crates/webgpu_renderer`.

Effort: **S** ≤ half a day · **M** 1–3 days · **L** a week+ · **XL** multi-week.

## Guiding principles

1. **Every feature lands with a golden test.** Headless render + structural
   pixel assertions (the `cube_on_plane.gltf` pattern). If it can't be
   asserted, it ships with a debug view that can be.
2. **Web parity is checked per phase, not at the end.** Chrome's WGSL
   validator is stricter than native naga (see: `textureSampleCompare`
   uniformity) — a wasm32 build + browser smoke belongs in the definition of
   done for anything touching shaders.
3. **The C++ Vulkan engine is the feature mirror.** Where it already solved
   something (CSM, skybox, hot reload, ImGui overlay), port the design, not
   just the idea — and port the *lessons* (resize path, per-image semaphores).

---

## Phase A — Material & rendering correctness

Small, high-value items that make arbitrary glTF files from the wild look right.

| Feature | Effort | Notes |
| --- | --- | --- |
| ✅ glTF sampler filters + wrap modes | S | Done 2026-07-18: nearest/linear + repeat/mirror/clamp honored per material texture |
| ✅ Mipmap generation | M | Done 2026-07-18: CPU box-filter chains at upload, sRGB-aware averaging |
| ✅ Normal matrix | S | Done 2026-07-18: inverse-transpose per primitive |
| ✅ Normal mapping | M | Done 2026-07-18: glTF tangents or Lengyel-style generation (MikkTSpace parity later if baked assets demand it) |
| ✅ Full metallic-roughness BRDF | M | Done 2026-07-18: GGX + Smith + Fresnel-Schlick; metallic/roughness texture sampling |
| ✅ Emissive + occlusion maps | S | Done 2026-07-18; `KHR_materials_emissive_strength` done 2026-07-21 (folded into `emissive_factor` at load for HDR emitters, GPU-free loader test) |
| ✅ Alpha modes | M | Done 2026-07-18: MASK cutoff discard + sorted BLEND pass with transparency-aware shadow casting. Per-pixel alpha-tested shadows for textured MASK materials done 2026-07-22 (`d2aafae`): MASK casters with a real base-color texture route through an alpha-testing shadow pipeline, so a cut-out foliage card casts the shadow of its silhouette, not the solid quad |
| ✅ Vertex colours (`COLOR_0`) | S | Done 2026-07-22 (`abb46c1`): read per-vertex `COLOR_0` (vec3/vec4, integer or float), multiplied into albedo per spec; white (no-op) when absent. `Vertex.color` at location 6 |
| ✅ Second UV set (`TEXCOORD_1`) | S | Done 2026-07-22 (`0f715e1`): per-material-slot UV-set selection via a `uv_set_mask` bitmask (baked AO commonly lives on UV1); `Vertex.uv1` at location 7, falls back to UV0 when absent |
| ✅ `KHR_texture_transform` | S | Done 2026-07-18: base color slot (other slots as needed) |
| ✅ Double-sided materials | S | Done 2026-07-18: per-primitive pipeline variant |
| ✅ sRGB/linear audit | S | Done 2026-07-18: full table in `docs/webgpu-srgb-audit.md`; one known deviation (web swapchain non-sRGB) |
| ✅ `KHR_materials_unlit` | S | Done 2026-07-22: flag rides a new `material_flags` vec4; the shader returns base color BEFORE any lighting, per spec. Fixes every Sketchfab/mobile/AR flat-color export, which previously got a full GGX response with IBL and shadows |
| ✅ Anisotropic filtering | S | Done 2026-07-22: 16x, but only when min/mag/mipmap are ALL linear - wgpu validates that, and nearest-filtered assets are the pixel-art ones whose look is deliberate. Grazing-angle floors were being over-blurred by several mip levels |
| ✅ Loader robustness | S | Done 2026-07-22: 16-bit images down-convert instead of aborting the WHOLE file (one 16-bit PNG used to mean the model would not open); triangle strips/fans are triangulated instead of silently vanishing (with the odd-triangle winding swap, or half the faces come out back-facing) |
| ✅ Degenerate-input hardening | S | Done 2026-07-22: a cyclic node parent was a stack-overflow ABORT; one non-finite vertex NaN'd all three cascade matrices (breaking shadows scene-wide, and `test_planes` treats NaN as visible); a zero-scale node NaN'd its normal matrix. All three are pure unit tests |

## Phase B — Scene, animation, input

| Feature | Effort | Notes |
| --- | --- | --- |
| ✅ Interactive camera controls | M | Done 2026-07-18: drag-orbit + wheel-zoom, auto-orbit until first interaction, native + web (fly/WASD later) |
| ✅ glTF node animations | M | Done 2026-07-18: TRS channels, looping, node hierarchy with animated AABBs. **CUBICSPLINE + STEP interpolation proper as of 2026-07-21** (was collapsing cubic→linear and mis-indexing the 3×-length cubic output — a latent bug; now `scene::Interpolation` + Hermite eval) |
| ✅ Skinning | L | Done 2026-07-18: JOINTS_0/WEIGHTS_0, per-primitive joint storage buffer, skinned forward + shadow passes |
| ✅ Morph targets | M | Done 2026-07-21: loader parses POSITION/NORMAL deltas into `CpuPrimitive.morph_targets` + mesh default weights; `scene::blend_morph_targets` weighted-accumulates + renormalizes; the WEIGHTS animation channel (`ChannelValues::MorphWeights`, Step/Linear/CubicSpline via `sample_morph_weights`) drives per-target weights; `forward::apply_morph_targets` re-blends + re-uploads dirty primitives each frame (COPY_DST vertex buffer, dirty-flag gated, neutral pose kept only for morphed prims). Simplified LODs drop morphing (v1). 7 tests |
| ✅ Runtime scene graph | M | Done 2026-07-18: node table with parents + local TRS, world recompute per frame (dirty-flag optimization later) |
| ✅ GLB verification | S | Done 2026-07-18: generated cube.glb + load test |
| ✅ Drag-and-drop model loading | M | Done 2026-07-18 for native; web File API drop zone done 2026-07-22 (`62e215c`): browser drag-and-drop via the DOM File API (winit-web never delivers `DroppedFile`), first dropped `.glb` read async and uploaded with native-viewer semantics |
| ✅ Multiple cameras from glTF | S | Done 2026-07-18: parsed into `CpuScene::cameras` (yfov/znear/zfar + node pose); viewer still uses its orbit camera by default |
| `EXT_meshopt_compression` / Draco | L | Decompression on load; meshopt first (pure Rust decoder exists) |

## Phase C — Lighting & atmosphere (C++ engine parity)

| Feature | Effort | Notes |
| --- | --- | --- |
| ✅ Skybox pass | M | Done 2026-07-18: procedural gradient + analytic sun following the light sliders (HDR equirect/cubemap upgrade later with IBL) |
| ✅ Image-based lighting | L | Analytic v1 done 2026-07-18 (hemisphere irradiance + roughness-blended sky reflection + Karis split-sum approx from the analytic sky). HDR-cubemap IBL for arbitrary env maps done (audited 2026-07-21): real split-sum in `render/ibl.rs` (irradiance-convolved cubemap + roughness-prefiltered specular cubemap + BRDF LUT), fed by `asset/hdr.rs` decoding Radiance `.hdr`/RGBE; the analytic path is now the fallback when no env is bound |
| ✅ `KHR_lights_punctual` | M | Done 2026-07-18: point/spot/directional, KHR range window + spot cones, up to 4 lights (shadowless; punctual shadows are the next row) |
| Point/spot shadows | L | Shadow atlas or cube shadows; after punctual lights |
| ✅ Cascaded shadow maps | L | Done 2026-07-18: 3 cascades in a depth array, view-distance selection, per-cascade fitting |
| ✅ Bloom | M | Done 2026-07-18: half-res brightpass + 9-tap separable Gaussian, strength slider in the overlay |
| ✅ SSAO | M | Done 2026-07-18: depth-only reconstruction, half-res + 3x3 blur, tonemap composite, overlay slider |
| ✅ Exposure control | S/M | Manual EV done 2026-07-18 (exp2(EV) before ACES + overlay slider). Histogram auto-exposure done 2026-07-20: histogram compute pass → GPU reduction to an adapted EV → tonemap reads it from a buffer, no per-frame readback on the frame path; off by default (`ForwardRenderer::auto_exposure`), manual EV survives as an override through the same buffer |
| Clustered / Forward+ lighting | XL | Only when light counts demand it |

## Phase D — Performance & scale (Colosseum-ready)

| Feature | Effort | Notes |
| --- | --- | --- |
| ✅ Frustum culling | S | Done 2026-07-18: world AABBs + Gribb-Hartmann planes, camera passes only |
| ✅ GPU instancing | M | Done 2026-07-20 (normals corrected 2026-07-22: instanced normals now use the COFACTOR of the instance matrix, not the matrix itself - the raw matrix is only right for uniform scale and shears normals under the non-uniform/mirrored scale instancing exists for; bounds and scene bounds also follow instances now): per-instance transform buffer (one identity instance by default), reaches normals + shadow pass; `set_instances`/`instance_count` |
| 🟡 KTX2 compressed textures | L | Done 2026-07-18: KTX2 container + BC1/3/5/7 passthrough with graceful fallback where BC is unavailable. Basis ETC1S/UASTC transcoding (and the web path) still open |
| ✅ LOD pipeline | L | v1 done 2026-07-18: vertex-clustering simplifier + distance-based selection (`scene::lod`). Quadric-error (meshoptimizer-grade) decimation done 2026-07-20 (`scene::qem`, selectable via `Simplifier::Quadric`) — preserves silhouettes/creases that clustering rounds off. On the render path since 2026-07-20 (per-primitive per-frame selection on camera distance; off by default; shadow casters stay full-detail) |
| Async asset loading | M | Background thread native / fetch + progress on web; loading UI |
| Indirect draws | M | `draw_indexed_indirect` batching once culling is GPU-side |
| ✅ GPU occlusion culling | XL | Done 2026-07-21: temporal hardware occlusion queries (NOT a Hi-Z pyramid — WebGPU core lacks portable depth-mip sampling). Per-primitive world-AABB query pass → `resolve_query_set` → async readback → next-frame skip of zero-sample primitives; one-frame latency accepted. `render/occlusion.rs`, `TimedPass::OcclusionCull`, overlay checkbox, off by default. GPU *frustum* culling (compute-based) still open |
| ✅ wasm size budget | S | Done 2026-07-31: `Scripts/Linux/wasm-size-budget.sh` builds wasm32-unknown-unknown release, runs `wasm-opt -Oz`, fails above a 12 MiB budget; wired into `Linux.yml`'s "Enforce wasm demo size budget" step ahead of the docs deploy. Measured post-opt size at the time: ~8.3 MiB — the previously-quoted ~3.7 MB figure was stale/never enforced |
| ✅ Timestamp-query profiling | M | Done 2026-07-20: per-pass wgpu timestamp queries, averaged ms via `gpu_timings_ms()` (`render/gpu_timing.rs`), + `dump_gpu_timings` example feeding the cross-renderer timing table |

## Phase E — Web platform & demo polish

| Feature | Effort | Notes |
| --- | --- | --- |
| ✅ Web deploy (via Sphinx docs site) | S | Done 2026-07-18: demo ships inside the Sphinx site (`docs/source/_webgpu_demo` + `html_extra_path`, page `webgpu_demo.md`), deployed by the existing docs FTP pipeline. **CI auto-rebuild done 2026-07-23** (`4088fe0a`): `Scripts/Linux/docs-build-web.sh` recompiles the crate to wasm32 + wasm-bindgen and refreshes `_webgpu_demo` before Sphinx on every deploy (pinned wasm-bindgen, best-effort with the committed snapshot as fallback), so the live demo always tracks the current renderer instead of a hand-built snapshot that goes stale |
| ✅ Responsive canvas | S | Done 2026-07-18: CSS-driven layout, backing store follows clientSize × devicePixelRatio per frame |
| ✅ Touch controls | M | Done 2026-07-20: one finger orbits, two-finger pinch-zoom; ratio-based (DPI-independent), pinch baseline resets on finger-count change |
| Model picker UI | S | Query param + dropdown of bundled scenes |
| ✅ WebGPU-unsupported fallback page | S | Done 2026-07-18: `navigator.gpu` check with requirements + native command |
| `webgl` backend feature flag | M | wgpu's GL backend for older browsers, feature-gated with reduced effects |
| Demo scene: Colosseum | M | License-checked photogrammetry scan (CC-BY: attribute in `LICENSES-ASSETS.md`), LFS or download step — never committed raw; needs Phase D compression to be pleasant |

## Phase F — Engine architecture & tooling

| Feature | Effort | Notes |
| --- | --- | --- |
| ✅ egui overlay (plan milestone 6) | M | Done 2026-07-18: FPS/frametime + light azimuth/elevation/intensity/ambient sliders, native + web |
| ✅ Hot shader reload | M | Done 2026-07-18: mtime polling + R key, validation-scoped rebuild keeps old pipelines on bad WGSL |
| ✅ Render graph (v1) | XL | Done 2026-07-18: passes declare read/write resources, validated in debug builds; explicit order retained. Auto-scheduling/aliasing still open |
| ✅ Screenshot capture | S | Done 2026-07-18: viewer S key → 1080p PNG via offscreen readback (turntable video later) |
| Golden-image CI on Linux | M | lavapipe/llvmpipe software Vulkan on the Ubuntu runner so the GPU tests stop skipping in CI |
| Error telemetry | S | Route `log` + panic reports through `kataglyphis_telemetry` |
| ✅ cargo-deny verification | S | Run 2026-07-18: **licenses ok**. Advisories flag 3 pre-existing transitive issues (all predate the renderer, from commits `6daaac6`/`4969ab6`): `quick-xml 0.39.4` (2 CVEs, Linux/Wayland only, pinned by `wayland-scanner` ← `smithay-client-toolkit` ← `winit`) and unmaintained `ttf-parser` (via egui fonts). Not fixable locally — they need upstream winit/egui bumps. Left un-ignored deliberately so they stay visible |
| ✅ API docs + examples | M | Done 2026-07-18: crate README, `headless_render` example, warning-free `cargo doc --no-deps` |
| ✅ wgpu 29 / egui 0.35 major-version migration | L | Done 2026-07-21: wgpu 27→29, egui 0.33→0.35, naga 26→29 (`immediate_size`, `multiview_mask`, `bind_group_layouts: &[Option<&_>]`, `Option` depth fields, `MipmapFilterMode`, error-scope guards, egui `begin_pass`/`end_pass`, `CurrentSurfaceTexture` enum). Fully CI-green on `develop` (now the repo's default+integration branch) |

## Phase G — Ecosystem integration (BeschleunigerBallett ↔ template)

| Feature | Effort | Notes |
| --- | --- | --- |
| ✅ Shared shader pipeline | M | Shipped via [Slang](https://shader-slang.com/): one `.slang` source compiles to SPIR-V (Vulkan/C++) and WGSL (WebGPU/Rust); see `docs/shader-sharing.md`. The earlier naga-based `export_shaders` WGSL→SPIR-V/GLSL450 route is retired (see that doc's Historical note) but the example still exists in the crate |
| Shared asset pipeline | M | OBJ→glTF conversion for the C++ engine's `Resources/Models` so both renderers eat the same scenes |
| Side-by-side comparison harness | M | Same scene, same camera: Vulkan C++ vs WebGPU Rust screenshots diffed — a regression net for BOTH renderers |
| Compute playground | L | Particles / GPU skinning via compute passes; the Rust sibling of the Kompute experiments |
| Flutter embedding | XL | The template already ships flutter_rust_bridge — render into a Flutter texture; speculative |
| WebXR | XL | Browser VR/AR once wgpu's WebXR story matures; parking-lot item |

---

## Suggested order of attack

1. **A1–A5** (samplers, mipmaps, normal matrix/mapping, BRDF) — correctness first, everything visual builds on it.
2. **E1–E2** (Pages deploy + responsive canvas) — cheap, makes the demo shareable while the rendering work continues.
3. **F1 egui overlay + B1 camera controls** — turns the demo from a turntable into a tool.
4. **C1–C2 (skybox + IBL)** — the big visual payoff.
5. **D-phase as needed** once the Colosseum asset is picked.
6. Render graph (F3) the moment pass wiring starts hurting.
