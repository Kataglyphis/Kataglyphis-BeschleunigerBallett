# Plan: WebGPU + glTF Renderer in Rust

Status: **milestones 1–5 implemented** (2026-07-18) as
`ExternalLib/Kataglyphis-RustProjectTemplate/crates/webgpu_renderer`
(`kataglyphis_webgpu_renderer`): wgpu 27 context with headless + windowed
paths, resize/Outdated/Lost-safe surface lifecycle (attachment sizes derive
from the *acquired* frame texture, never the window), glTF loader
(positions/normals/UVs/indices, node transforms, base-color materials, flat
normal synthesis), forward pass with a directional light, orbit camera, a
`viewer` example (`cargo run -p kataglyphis_webgpu_renderer --example viewer
[model.gltf]`), and headless golden tests against a bundled generated
`tests/assets/cube.gltf` (structural pixel assertions in sRGB space).
Milestone 3 (2026-07-18): sRGB base-color textures with a white fallback
(glTF images decoded to RGBA8; a checkered `cube_textured.gltf` golden test
proves sampling), HDR Rgba16Float offscreen target, and an ACES tonemap
fullscreen pass (`render/tonemap.rs`) used by both the viewer and headless
readback. Follow-up noted: honor glTF sampler filters/wrap modes (currently
always linear/repeat), mipmaps.
Milestone 4 (2026-07-18): directional shadow mapping — depth-only pass from
the light's POV into a 2048² Depth32Float map (uniforms-only bind group: the
full group samples the map the pass writes), orthographic light frustum
fitted to the scene AABB, 3x3 PCF with slope-scaled bias in the forward
pass; golden test `shadow_darkens_plane_under_cube` over a generated
`cube_on_plane.gltf`.
Milestone 5 (2026-07-18): wasm32/WebGPU browser demo — cdylib +
wasm-bindgen entry (`src/wasm_demo.rs`) rendering the embedded shadow scene
in Chrome; verified locally (build wasm32 + `wasm-bindgen --target web`,
serve `crates/webgpu_renderer/web/`). Hard-won lessons: winit does not size
the canvas backing store (explicitly set it or the surface renders at ~1x1),
and Chrome's WGSL validator rejects `textureSampleCompare` in non-uniform
control flow — use `textureSampleCompareLevel` (an invalid module silently
voids the whole submit: pure black canvas, no page-visible error). CI runs
the crate's tests via Invoke-DebugTests.ps1. Public hosting (e.g. GitHub
Pages) still open. Milestone 6 (parity extras: egui overlay, skybox,
animations) remains open.

## Why wgpu

`wgpu` is the de-facto Rust WebGPU implementation (used by Firefox and Bevy). One
codebase runs native (Vulkan on Windows/Linux — including the RX 9070 XT — plus
Metal/DX12) and in the browser via WASM against the WebGPU API. This complements
the C++ Vulkan engine: same scenes, portable renderer, reachable from the web.

## Crate/module layout

```
crates/webgpu_renderer/
├── src/
│   ├── lib.rs              # public API: Renderer::new(surface), load_gltf(), render()
│   ├── context.rs          # instance/adapter/device/queue + surface configuration
│   ├── asset/
│   │   ├── gltf_loader.rs  # gltf crate -> internal scene representation
│   │   └── image.rs        # texture decode (image crate; KTX2/basis later)
│   ├── scene/
│   │   ├── mod.rs          # Scene, Node hierarchy, Mesh, Primitive, Material
│   │   └── camera.rs       # same yaw/pitch/WASD semantics as the C++ Camera
│   ├── render/
│   │   ├── forward.rs      # forward PBR pass (first milestone)
│   │   ├── shadow.rs       # depth-only pass (milestone 4)
│   │   └── tonemap.rs      # post pass: HDR -> swapchain (milestone 3)
│   └── shaders/            # WGSL, checked in, hot-reloadable in debug
├── examples/
│   └── viewer.rs           # winit window, orbit camera, drag&drop a .gltf
└── tests/                  # headless golden-image tests (no window needed)
```

## Dependencies (all pure Rust, workspace-pinned)

| Crate | Role |
| --- | --- |
| `wgpu` | WebGPU implementation |
| `winit` | window + input (native and WASM) |
| `gltf` | glTF 2.0 parsing (+ `import()` for buffers/images) |
| `glam` | linear algebra (mirrors GLM usage in the C++ engine) |
| `image` | PNG/JPEG texture decoding |
| `bytemuck` | safe casting of vertex/uniform data |
| `pollster` | block-on for native async adapter/device requests |
| `egui` + `egui-wgpu` (later) | overlay UI, mirroring the ImGui overlay |

## Milestones

1. **Triangle + surface lifecycle** — context creation, swapchain config,
   resize/suboptimal handling done correctly from day one (the C++ engine's
   resize path was this project's only real bug — port the lesson, not the bug:
   reconfigure the surface on `SurfaceError::Outdated/Lost`, never mid-frame).
   Headless `wgpu` test asserting a rendered triangle's pixels (golden test).
2. **glTF meshes + camera** — load positions/normals/UVs/indices, node
   transforms, depth buffer, fly camera. Golden test: render a bundled small
   asset (e.g. Khronos `Box.gltf`) headless and compare against a reference.
3. **PBR materials + tonemapping** — base color/metallic-roughness/normal maps,
   sRGB handling, HDR offscreen target + tonemap pass. Start with
   `KHR_materials_` core; skip extensions until needed.
4. **Lights + shadows** — directional light with a single shadow map first
   (cascades later, mirroring `CascadedShadowMap` in C++ only if needed).
5. **WASM target** — `wasm32-unknown-unknown` build of the viewer example,
   deployed as a static page; verifies the "web" half of WebGPU.
6. **Parity extras** (optional): skybox, egui overlay with the same toggles as
   the ImGui GUI (renderer mode switches), animation (`gltf` animations).

Each milestone lands with: headless golden-image test in CI (Linux lavapipe/
llvmpipe software adapter — no GPU runner needed), `cargo clippy` clean,
`cargo deny` clean (license check for new deps).

## Testing strategy

- `wgpu` supports headless rendering: create a device without a surface, render
  to a texture, read back, compare. This is the render-mode-coverage lesson from
  the C++ side applied from the start: every selectable pipeline path gets a
  frame-producing test, not just a bring-up test.
- Keep reference images small (64×64) and compare with a tolerance to absorb
  driver differences.

## Assets: can you use a Colosseum asset?

Short answer: **yes, if the specific asset's license allows it — check per
asset, and attribute.**

- Photogrammetry scans of the Colosseum on Sketchfab/Fab are typically
  **CC-BY 4.0** (usable, requires attribution in README/credits) or
  **CC-BY-NC** (usable only if the project stays non-commercial). A few are
  CC0 (no strings attached). The *building itself* is millennia out of any
  copyright; what is licensed is the particular scan/model you download.
- Do not commit multi-hundred-MB scans into git — put big assets in a
  `Resources/` download step or Git LFS, and keep a `LICENSES-ASSETS.md` entry
  (this repo already has `docs/LICENSES-README.md` for exactly this).
- For CI golden tests use tiny Khronos sample assets
  (github.com/KhronosGroup/glTF-Sample-Assets, mostly CC0/CC-BY) — e.g. `Box`,
  `DamagedHelmet` — and keep the Colosseum scan as a local/manual showcase
  asset. A million-triangle photogrammetry mesh will also want milestone 3+
  (mipmapped textures) and possibly meshoptimizer-style decimation first.

## Integration back into this repo

Not required initially — the crate lives and builds in the Rust workspace. If
desired later: a `RUST_WEBGPU` CMake option could build the viewer via
corrosion like the existing `kataglyphis_rustprojecttemplate` bridge, but the
faster path is `cargo run --example viewer` during development.
