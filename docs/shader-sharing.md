# Sharing Shader Code Between the C++ Vulkan Engine and the Rust WebGPU Renderer

**Yes — via naga, with WGSL as the source of truth.** The Rust renderer
already depends on naga (it is wgpu's shader compiler), so the same
translator that runs at wgpu startup can emit SPIR-V and GLSL 450 for the
C++ engine offline.

## The pipeline (working today)

```powershell
cargo run -p kataglyphis_webgpu_renderer --example export_shaders -- <out_dir>
# default out_dir: target/shader-export
```

Emits, per shader in `crates/webgpu_renderer/src/shaders/`:

- `<shader>.spv` — one SPIR-V module containing every entry point (this is
  what `VulkanRenderer` can load directly; it is the same format
  `Resources/Shaders/**/spv/*.spv` already holds).
- `<shader>.<entry>.glsl` — desktop **GLSL 450** per entry point, for
  reading, debugging, or feeding an existing GLSL toolchain.

`tests/shader_export.rs` guards the path: every shader must parse,
validate, and produce SPIR-V with a correct magic number, so a WGSL change
that would break the C++ side fails in CI.

## Why WGSL as the source of truth (not GLSL)

WGSL is the **stricter** language: it enforces uniformity analysis, has no
implicit conversions, and rejects constructs Vulkan GLSL tolerates. What
validates as WGSL will compile for Vulkan; the reverse is not true — this
project already hit that asymmetry (`textureSampleCompare` in non-uniform
control flow passed native naga and was rejected by Chrome; see
`webgpu-gltf-rust-plan.md`). Authoring in the strict language and
generating the permissive one removes a whole class of "works native,
breaks on web" bugs.

## What shares well — and what does not

| Shareable | Notes |
| --- | --- |
| ✅ Pure math kernels | GGX/Smith/Fresnel BRDF, ACES tonemap, sky model, PCF filtering, SSAO kernel — these translate cleanly and are the bulk of the interesting code |
| ✅ Struct layouts | Uniform blocks translate; keep field order/padding identical on both sides (the C++ engine's `hostDevice/` includes serve the same purpose) |
| ⚠️ Entry points | Translate, but binding decorations must line up with the consumer's descriptor layout |
| ❌ Binding models | WebGPU bind groups ≠ Vulkan descriptor sets; WebGPU has no push constants and no `gl_` builtins beyond its own set |
| ❌ A few texture ops | e.g. `textureLoad` on depth textures has no GLSL equivalent (SSAO's main pass) — SPIR-V still works, only the GLSL emit is skipped |

**Practical recommendation:** share the *functions*, not the *entry
points*. Keep each renderer's entry points and binding declarations local
(they're ~20 lines each), and let the generated SPIR-V/GLSL supply the
shared math. That keeps both binding models native and avoids fighting the
descriptor-set mismatch.

## Beyond shaders

The bigger cross-renderer wins on the roadmap
(`webgpu-renderer-roadmap.md`, Phase G) are:

- **Shared assets** — OBJ→glTF conversion so both renderers eat the same
  scenes (the C++ engine's `Resources/Models` becomes directly usable).
- **Side-by-side comparison harness** — same scene, same camera, diff the
  Vulkan and WebGPU screenshots. With shared BRDF math the images should
  match closely, which turns the diff into a regression net for *both*
  renderers.
