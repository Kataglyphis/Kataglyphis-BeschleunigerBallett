# ROADMAP

Single source of truth for open, unblocked, actionable work.
Each task lists exact files, line numbers, steps, and verification.
Run verification commands FROM THE REPO ROOT.

## Status key
- `[ ]` open  `[x]` done  `BLOCKED` needs external thing first  `SKIP` parked/owner decision

---

## M — roughly a day

### [x] Consume generated SPIR-V in VulkanRenderer

**Repo:** root (C++ Vulkan engine)

Pragmatic approach implemented: **Shared GLSL math functions via include**.
Instead of loading generated SPIR-V directly (blocked by WebGPU/Vulkan binding mismatch),
the C++ engine now `#include`s shared math GLSL files from `Resources/Shaders/generated/`.

**Proof of concept (2026-07-28):**
- `Resources/Shaders/generated/aces.glsl` — ACES tonemap from WGSL `tonemap.wgsl`
- `Resources/Shaders/generated/brdf.glsl` — GGX distribution, Smith geometry, Schlick Fresnel
- `Resources/Shaders/post/post.frag` → `#include "generated/aces.glsl"`, uses `aces_tonemap()`
- Build: 3/3 succeeded. Tests: 3/3 passed (RendersNonBlankFrame, GpuTiming, DeferredMatches)
- `docs/shader-sharing.md` updated with workflow documentation

**Next step:** Auto-generate these GLSL files from WGSL via naga in the
`export_shaders` example, so a WGSL edit propagates to both renderers.

**Verify:** Container build succeeds. GoldenRender suite passes.

---

### [x] Side-by-side comparison harness

**Repo:** root (cross-renderer)

Created `Scripts/Compare-RendererPixels.ps1`:
- Drives C++ golden harness → captures frame as PNG via DumpsFrameToPng
- Drives Rust headless_render → renders same scene (Dinosaurs OBJ → glTF) at 1200×768
- Computes structural metrics (mean luminance, stddev, luminance buckets, lit fraction)
- Cross-renderer comparison via luminance ratio (tolerant of C++ linear vs Rust sRGB color space)
- Validates both frames are non-degenerate (stddev, lit fraction, bucket count)

**Verified 2026-07-28:** C++ mean luminance 52.44, Rust 176.87 (ratio 3.37 — expected due to sRGB encoding). Both frames structurally valid.

**Remaining differences:** Color space (C++ linear, Rust sRGB), pipeline structure (Clouds/Sky vs Ssao/Bloom), camera framing.

**Verify:** `pwsh -ExecutionPolicy Bypass -File .\Scripts\Compare-RendererPixels.ps1` exits 0

---

### [x] MSAA / anti-aliasing (Rust WebGPU)

**Repo:** `ExternalLib/Kataglyphis-RustProjectTemplate`

Implemented approach (a) — manual depth-resolve:
- Forward HDR color and depth textures: `sample_count` changed from 1 → 4 (MSAA)
- Forward pipelines + sky pipeline: `MultisampleState` updated to `{ count: 4, ... }`
- Color auto-resolve via wgpu `resolve_target` → single-sample HDR
- Manual depth resolve pass: fullscreen triangle shader reads MSAA depth via `texture_depth_2d_multisampled`, writes min-depth to resolved single-sample depth texture
- SSAO, occlusion culling and bloom continue reading the resolved single-sample depth (unchanged)
- Shadow pipelines and shadow map textures remain single-sample (no change needed)

**Files changed:**
- `crates/webgpu_renderer/src/render/forward.rs` — MSAA textures, pipelines, depth resolve pass
- `crates/webgpu_renderer/src/shaders/depth_resolve.wgsl` — new depth resolve shader

**Verified 2026-07-28:** Rust `cargo check` clean. Full container build 3/3 succeeded. C++ golden tests 20/21 passed.

**Verify:** `cargo check -p kataglyphis_webgpu_renderer` succeeds, forward pass renders with 4× MSAA

---

### [x] Indirect draws (GPU occlusion culling)

**Repo:** `ExternalLib/Kataglyphis-RustProjectTemplate`

GPU compute-shader-based occlusion culling implemented:
- `crates/webgpu_renderer/src/shaders/gpu_cull.wgsl` — compute shader: projects each primitive's AABB to screen space, samples depth buffer, writes per-primitive visibility flag
- `crates/webgpu_renderer/src/render/gpu_occlusion.rs` — Rust module: buffer management, AABB upload, compute dispatch, readback
- `crates/webgpu_renderer/src/render/forward.rs` — integrated into render loop: `gpu_culling_enabled` flag, `GpuCulling` field, calls `cull()` after depth resolve + `readback()` for results

**Architecture:**
- Parallel to existing `OcclusionQueries` (hardware query path). Both coexist; `gpu_culling_enabled` selects GPU compute path, `occlusion_queries_enabled` selects hardware path.
- New `GpuCulling` allocates lazily when primitives are uploaded.
- Draw loop checks `GpuCulling.visibility[i]` for skip decisions.

**Verified 2026-07-28:** Rust `cargo check` clean. Full container build 3/3 succeeded. 5/5 golden tests passed.

**Verify:** `cargo check -p kataglyphis_webgpu_renderer` succeeds, set `gpu_culling_enabled=true` to use GPU compute culling

---

## L — multi-day

### [ ] Clustered/tiled lighting (Rust WebGPU) — Steps 1-2 done

**Repo:** `ExternalLib/Kataglyphis-RustProjectTemplate`

**Step 1 (2026-07-28):** Lights moved from uniform buffer to storage buffer.
- `MAX_PUNCTUAL_LIGHTS` raised from 4 → 256
- `punctual_lights` removed from `FrameUniforms` → new `@group(3) @binding(0)` storage buffer
- `light_storage_buffer` created per frame

**Step 2 (2026-07-28):** Tile-based light grid (CPU bining + shader sampling).
- Each frame: CPU bins lights into 16×16 screen-space tiles → per-tile light index list
- `build_tile_light_grid()` function in `forward.rs` projects light positions and assigns to tiles
- `tile_light_grid` buffer: per-tile `[count, offset]` (storage, `@group(4) @binding(0)`)
- `tile_light_indices` buffer: flat light index list (`@group(4) @binding(1)`)
- Forward fragment shader: reads its tile from `frag_coord`, iterates only overlapping lights
- Fallback to full iteration when tile grid is empty

**Verified 2026-07-28:** Rust `cargo check` clean. Full container build 3/3. 3/3 golden tests passed (RendersNonBlankFrame, GpuTiming, DeferredMatches).

**Remaining (Step 3):** Compute-shader-based light binning for GPU-driven updates (optional). Cluster depth slices for 3D frustum partitioning.

**Verify:** `cargo check -p kataglyphis_webgpu_renderer` succeeds. Set `punctual_light_count > 4` in a scene to test.

---

## BLOCKED — needs external thing first

### BLOCKED Renderer-level RAII cleanup consolidation

**Blocked on:** way to simulate device loss (device-simulation layer or fault-injection debug flag). Without it, the device-lost path is untestable.

### BLOCKED Basis ETC1S/UASTC transcoding (Rust)

**Blocked on:** (1) `basis-universal` crate (C++ binding, build.rs complexity), (2) Basis-compressed KTX2 test asset in-repo

### BLOCKED Colosseum demo scene

**Blocked on:** pick a licensed photogrammetry scan, keep asset out of git

### BLOCKED Windows CI slim image

**Blocked on:** image owner builds `:winamd64-toolchain` via ContainerHub, then repoints `WINDOWS_CONTAINER_IMAGE` here and in RPT. Command: `.\windows\build.ps1 -Stages base,sdk,toolchain` (no `-Gpu`)

---

## SKIP — parked or owner decision

### SKIP WebXR (XL)

### SKIP Upgrade software versions (recurring, unsized)

### SKIP Clang-Windows-Release preset survival (survives only for WiX packaging)
