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
| ✅ Emissive + occlusion maps | S | Done 2026-07-18 (`KHR_materials_emissive_strength` still open) |
| Alpha modes | M | OPAQUE / MASK (alpha-cutoff in shader) / BLEND (sorted back-to-front pass) |
| `KHR_texture_transform` | S | Common in atlas-packed assets |
| ✅ Double-sided materials | S | Done 2026-07-18: per-primitive pipeline variant |
| sRGB/linear audit | S | Partially covered (per-slot srgb flags); document the full table |

## Phase B — Scene, animation, input

| Feature | Effort | Notes |
| --- | --- | --- |
| Interactive camera controls | M | Orbit (drag) + fly (WASD) matching the C++ camera semantics; pointer events on web, mouse on native |
| glTF node animations | M | TRS channels, step/linear/cubic interpolation, looping |
| Skinning | L | Joints/weights vertex attributes, joint-matrix uniform/storage buffer, GPU skinning in the vertex stage |
| Morph targets | M | After skinning; weights animated |
| Runtime scene graph | M | Mutable transforms + dirty propagation instead of baked world matrices; prerequisite for animation & editor-ish tooling |
| GLB verification | S | `import_slice` should already handle binary glTF — add a golden test with a .glb |
| Drag-and-drop model loading | M | Native: file dialog/drop; web: File API + drop zone; replaces the current CLI-arg-only flow |
| Multiple cameras from glTF | S | Use the file's cameras when present |
| `EXT_meshopt_compression` / Draco | L | Decompression on load; meshopt first (pure Rust decoder exists) |

## Phase C — Lighting & atmosphere (C++ engine parity)

| Feature | Effort | Notes |
| --- | --- | --- |
| Skybox pass | M | HDR equirect → cubemap; mirrors the C++ SkyBox stage |
| Image-based lighting | L | Irradiance (SH or convolved cubemap) + prefiltered specular + BRDF LUT; the single biggest visual jump |
| `KHR_lights_punctual` | M | Point/spot/directional lights from the glTF |
| Point/spot shadows | L | Shadow atlas or cube shadows; after punctual lights |
| Cascaded shadow maps | L | Port the C++ `CascadedShadowMap` design; needed once scenes get large |
| Bloom | M | Threshold + separable blur chain on the HDR target, composited before tonemap |
| SSAO | M | Depth+normal reconstruction; half-res + blur |
| Exposure control / auto-exposure | S/M | Manual EV first; histogram-based auto later |
| Clustered / Forward+ lighting | XL | Only when light counts demand it |

## Phase D — Performance & scale (Colosseum-ready)

| Feature | Effort | Notes |
| --- | --- | --- |
| Frustum culling | S | Per-primitive AABBs already computable from the loader |
| GPU instancing | M | Instance buffer path for repeated meshes |
| KTX2/BasisU compressed textures | L | Transcode to BCn (native) / supported formats (web); huge VRAM + load-time win for photogrammetry |
| LOD pipeline | L | meshoptimizer-style simplification offline + runtime selection |
| Async asset loading | M | Background thread native / fetch + progress on web; loading UI |
| Indirect draws | M | `draw_indexed_indirect` batching once culling is GPU-side |
| GPU frustum/occlusion culling | XL | Compute-based; far future |
| wasm size budget | S | `twiggy`/`wasm-opt` in the web build; track regression in CI |
| Timestamp-query profiling | M | wgpu timestamp queries + on-screen frame breakdown (pairs with the egui overlay) |

## Phase E — Web platform & demo polish

| Feature | Effort | Notes |
| --- | --- | --- |
| ✅ Web deploy (via Sphinx docs site) | S | Done 2026-07-18: demo ships inside the Sphinx site (`docs/source/_webgpu_demo` + `html_extra_path`, page `webgpu_demo.md`), deployed by the existing docs FTP pipeline. Rebuild + recopy when the demo changes |
| ✅ Responsive canvas | S | Done 2026-07-18: CSS-driven layout, backing store follows clientSize × devicePixelRatio per frame |
| Touch controls | M | Pinch-zoom orbit for mobile WebGPU (Chrome Android) |
| Model picker UI | S | Query param + dropdown of bundled scenes |
| WebGPU-unsupported fallback page | S | Clear message + link; optionally a pre-rendered video/gif |
| `webgl` backend feature flag | M | wgpu's GL backend for older browsers, feature-gated with reduced effects |
| Demo scene: Colosseum | M | License-checked photogrammetry scan (CC-BY: attribute in `LICENSES-ASSETS.md`), LFS or download step — never committed raw; needs Phase D compression to be pleasant |

## Phase F — Engine architecture & tooling

| Feature | Effort | Notes |
| --- | --- | --- |
| egui overlay (plan milestone 6) | M | FPS/frametime, light + tonemap sliders, mode toggles — parity with the C++ ImGui overlay; egui-wgpu is already in the workspace (gui crate) |
| Hot shader reload | M | Native file-watch on `src/shaders/`, pipeline rebuild; mirrors the C++ "Hot shader reload" button |
| Render graph | XL | Declarative passes with tracked resources; do it *before* pass count explodes (bloom+SSAO+shadows is the trigger point) |
| Screenshot/turntable capture | S | Native readback → PNG; also useful for docs and regression baselines |
| Golden-image CI on Linux | M | lavapipe/llvmpipe software Vulkan on the Ubuntu runner so the GPU tests stop skipping in CI |
| Error telemetry | S | Route `log` + panic reports through `kataglyphis_telemetry` |
| cargo-deny + licenses | S | The workspace already runs deny — verify the new deps stay clean each phase |
| API docs + examples | M | `cargo doc` polish, a `headless_render` example, README with the browser screenshot |

## Phase G — Ecosystem integration (BeschleunigerBallett ↔ template)

| Feature | Effort | Notes |
| --- | --- | --- |
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
