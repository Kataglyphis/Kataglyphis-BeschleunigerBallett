# Sharing Shader Code Between the C++ Vulkan Engine and the Rust WebGPU Renderer

**Yes — via naga, with WGSL as the source of truth.** The Rust renderer
already depends on naga (it is wgpu's shader compiler), so the same
translator that runs at wgpu startup can emit SPIR-V and GLSL 450 for the
C++ engine offline.

## The pipeline (working today)

```pwsh
# Wired into the build (2026-07-20). Opt-in, and non-critical: a missing cargo
# toolchain warns and skips rather than failing a C++ build.
pwsh -File .\Scripts\Windows\Build-Windows.ps1 -ExportWgslShaders ...
pwsh -File .\Scripts\Windows\Build-Windows-Container.ps1 -ExportWgslShaders ...

# Export only, no C++ build (the host cmake cannot read this repo's presets):
pwsh -File .\Scripts\Windows\Build-Windows.ps1 `
  -SkipBuild -SkipFormat -SkipTests -SkipPerfTests -SkipMsix -ExportWgslShaders

# Or drive the example directly:
cargo run -p kataglyphis_webgpu_renderer --example export_shaders -- <out_dir>
# default out_dir: target/shader-export
```

Output goes to `Resources/Shaders/generated/`, which is **gitignored** — these
are derived artifacts and would churn on every export.

**The container flag exports inside the container.** `Build-Windows-Container.ps1
-ExportWgslShaders` writes to `C:\ws\Resources\Shaders\generated`, and the
container streams back only build trees and logs, so the artifacts do not
appear on the host. That is the right behaviour for a containerized build (the
C++ build there sees them) but it means the host-side flag above is what a
developer wants when inspecting the output.

**Nothing in the C++ engine consumes these yet.** The pipeline is wired and
guarded; adopting the generated SPIR-V in `VulkanRenderer` is a separate step,
and the binding-decoration mismatch in the table below is why.

Emits, per shader in `crates/webgpu_renderer/src/shaders/`:

- `<shader>.spv` — one SPIR-V module containing every entry point (this is
  what `VulkanRenderer` can load directly; it is the same format
  `Resources/Shaders/**/spv/*.spv` already holds).
- `<shader>.<entry>.glsl` — desktop **GLSL 450** per entry point, for
  reading, debugging, or feeding an existing GLSL toolchain.

`tests/shader_export.rs` guards the path: every shader must parse,
validate, and produce SPIR-V with a correct magic number, so a WGSL change
that would break the C++ side fails in CI.

`compile-shaders.ps1` and `buildIntegritySuite.cpp` both skip anything under a
`generated/` directory, and that exclusion is load-bearing: the exported GLSL
carries WebGPU binding decorations, so glslc would try to compile files that
are not engine shaders, and the fresh timestamps would mark every real shader
stale at once. Both were observed failing before the exclusions went in.

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
shared math.

## Proof of Concept (2026-07-28)

`Resources/Shaders/generated/aces.glsl` and `brdf.glsl` are hand-written GLSL
translations of the WGSL math kernels in `forward.wgsl` and `tonemap.wgsl`.
They live in a `generated/` directory that is gitignored and excluded from
`compile-shaders.ps1` — but the C++ engine CAN `#include` them because glslc
searches `Resources/Shaders/` for includes.

`Resources/Shaders/post/post.frag` now `#include "generated/aces.glsl"` and
uses `aces_tonemap()` instead of the old Reinhard tonemap. The shader builds
and all golden tests pass (RendersNonBlankFrame, deferred/forward parity,
GPU timing dump — all verified 2026-07-28).

### Future: auto-generation from WGSL

The long-term path is:

1. Extract shared math functions from WGSL into standalone WGSL module files
   (e.g. `math/brdf.wgsl`, `math/tonemap.wgsl`, `math/sky.wgsl`).
2. The `export_shaders` example emits GLSL 450 for these modules.
3. The C++ engine `#include`s the generated GLSL, so a WGSL edit propagates
   to both renderers automatically.
4. Entry points and bindings stay renderer-specific. That keeps both binding models native and avoids fighting the
descriptor-set mismatch.

## Slang: a single source of truth (in progress, 2026-07-30)

The naga path above shares *functions* by generating GLSL the C++ engine
`#include`s. It cannot share *entry points* because the generated SPIR-V
carries WebGPU binding decorations that don't match the Vulkan engine's
descriptor layout. **Slang** ([shader-slang.org](https://shader-slang.org/))
removes that limit: one Slang source compiles to **SPIR-V** (Vulkan/C++) and
**WGSL** (Rust/WebGPU), and Slang's parameter-binding system
(`[vk::binding(i,s)]` → `@binding(i) @group(s)` in WGSL) is the lever that
aligns the two binding models so entry points can be shared too.

### Target status (verified against slangc in VulkanSDK 1.4.350.0)

| Target | Status | Notes |
| --- | --- | --- |
| SPIR-V (Vulkan) | **stable** | Rasterization, compute, **ray tracing pipeline**, **ray query**, descriptor sets, push constants |
| WGSL (WebGPU) | **experimental** | Functional (official `wgpu-html5` example uses it). **No** ray tracing, ray query, mesh/tessellation/geometry shaders, wave intrinsics, `f64`, `i8`/`u8`. Switch fall-through restructured. Bare `WGSL` = text (Tint validates downstream); `WGSLSPIRV` = WGSL→Tint→SPIR-V |

Ray tracing / path tracing shaders are **Vulkan-only by nature** (WebGPU has
no RT pipeline) and are migrated to Slang emitting SPIR-V only — for language
unity on the C++ side, not for WebGPU sharing.

### Architecture: Slang-native, not `#include`

Slang is module- and entry-point-centric. A no-entry-point math module
**cannot** be emitted to GLSL/WGSL text directly, and imported functions are
name-mangled in the emit. So the migration does **not** extend the
`#include`-generated-GLSL POC. Instead:

- **Shared math** lives in Slang modules (`Resources/ShadersSlang/common/aces.slang`).
- **Entry points** are Slang shaders that `import` the math modules and
  compile to the target their renderer needs. Mangling is a non-issue because
  Slang links modules internally.
- Each renderer keeps its own entry points where the passes differ (they
  composite differently); both `import` the same math. "Share the math, not
  necessarily the entry points" — but now via Slang `import`, with the option
  to share entry points where the passes coincide.

### What is wired today

**Shared math modules** (imported by entry points, never emitted directly):

- `Resources/ShadersSlang/common/aces.slang` — shared ACES filmic tonemap
  (Narkowicz 2015) + exact IEC 61966-2-1 sRGB encode. Replaces the
  hand-written `generated/aces.glsl` POC. **Fixes a discrepancy**: the C++
  `post.frag` used `pow(x, 1/2.2)` (the approximation this audit flags as
  visibly wrong in the darks); the shared `linear_to_srgb` uses the exact
  piecewise function the Rust renderer already had.
- `Resources/ShadersSlang/common/brdf.slang` — shared PBR BRDF math: the
  Epic Games / Unreal 4 microfacet model (`distribution_ggx`,
  `geometry_smith`, `fresnel_schlick`, `brdf_direct`). This is the same
  model the Rust `forward.wgsl` already uses and the C++ `pbr/brdf/unreal4.glsl`
  implements — unifying them removes the duplication and guarantees both
  renderers shade identically. Epsilon guards (from the Rust side) prevent
  NaN/inf on grazing angles that the C++ GLSL version lacked.
- `Resources/ShadersSlang/common/noise.slang` — shared procedural noise:
  3D simplex noise (`snoise`) + fractal Brownian motion (`fbm`). Based on
  the Ashima Arts / Gustavson implementation (MIT) used by the C++ engine's
  `common/grad_noise.glsl`. Pure math, no bindings — usable by both
  renderers' compute/raster passes.
- `Resources/ShadersSlang/common/fullscreen.slang` — shared fullscreen-
  triangle vertex utility (`fullscreen_vs`). Both renderers duplicate the
  same `vid/2*4-1` vertex-index math in every fullscreen pass; this import
  removes that boilerplate. Used by `tonemap.slang` and `post.slang`.

**Entry points** (compile to the target their renderer needs):

- `Resources/ShadersSlang/tonemap/tonemap.slang` — Rust/WebGPU fullscreen
  tonemap pass mirroring `tonemap.wgsl`, `import aces`. Verified to emit
  correct WGSL (bindings, varyings, `lerp`→`mix`, `SV_Position`→
  `@builtin(position)`, `SV_Target`→`@location(0)`) **and** SPIR-V.
- `Resources/ShadersSlang/post/post.slang` — C++/Vulkan fullscreen post
  pass (tonemap + optional cloud compositing), `import aces`. Uses
  `[[vk::push_constant]]` for feature toggles (matching
  `PushConstantPost.hpp`) and `Sampler2D` (combined image+sampler, matching
  the existing `sampler2D` descriptor layout). Emits SPIR-V only.
- `Resources/ShadersSlang/tests/brdf_test.slang` — CI guard: a compute
  entry point that `import brdf` and exercises `brdf_direct`, forcing emit
  of the shared BRDF math to **both** SPIR-V and WGSL. Guards the math
  module the way `tests/shader_export.rs` guards the naga path.
- `Resources/ShadersSlang/tests/noise_test.slang` — CI guard for the
  simplex noise + fbm math module (dual-emit SPIR-V + WGSL).

**Vulkan-only shaders** (SPIR-V emit — WebGPU has no RT pipeline):

- `Resources/ShadersSlang/raytracing/rt_types.slang` — shared RT types
  and binding constants (HitPayload, PushConstantRaytracing, GlobalUBO,
  SceneUBO). Imported by all RT entry points.
- `Resources/ShadersSlang/raytracing/raytrace.rgen.slang` — ray
  generation: camera ray setup via `TraceRay` + `RayDesc`, image store.
  Uses `DispatchRaysIndex/Dimensions`, `RaytracingAccelerationStructure`,
  `RWTexture2D`.
- `Resources/ShadersSlang/raytracing/raytrace.rchit.slang` — closest
  hit: `BuiltInTriangleIntersectionAttributes`, `inout HitPayload`,
  `WorldRayOrigin/Direction`, `RayTCurrent`, shadow ray `TraceRay`, and
  `import brdf` for shading. Proves the shared BRDF math works across RT
  stages.
- `Resources/ShadersSlang/raytracing/raytrace.rmiss.slang` — miss:
  background color from push constants.
- `Resources/ShadersSlang/raytracing/shadow.rmiss.slang` — shadow miss:
  `shadowed = false`.
- `Resources/ShadersSlang/path_tracing/path_tracing.slang` — path tracing
  compute kernel using `RayQuery<RAY_FLAG_FORCE_OPAQUE>` (ray query, not
  RT pipeline): `TraceRayInline` + `Proceed` + `CommittedStatus`, with
  `import brdf` for direct lighting. Proves the ray query API works in
  Slang.

  **Remaining for full parity** with the GLSL raytracing/path_tracing
  shaders: buffer device addresses (`GL_EXT_buffer_reference2` — Slang
  needs a `Pointer<T>` / buffer-reference mechanism), texture arrays, and
  the other BRDF models (disney, frostbite, pbrBook, phong). The current
  ports demonstrate all the ray tracing/ray query intrinsics working and
  the shared math modules being usable from RT stages.

**Build tooling**:

- `Scripts/Windows/compile-slang-shaders.ps1` — manifest-driven compile:
  emits SPIR-V and/or WGSL per entry point, timestamp staleness (mirrors
  `compile-shaders.ps1`), resolves `slangc` from `VULKAN_SDK` then `PATH`.
  Output: `Resources/ShadersSlang/build/{spv,wgsl}/` (gitignored).

```pwsh
pwsh -ExecutionPolicy Bypass -File .\Scripts\Windows\compile-slang-shaders.ps1
```

### Migration roadmap (phased)

| Phase | Scope | Status |
| --- | --- | --- |
| 0 | Toolchain + spike: prove `slangc` emits SPIR-V + WGSL | ✅ done |
| 1 | Shared math library in Slang (ACES ✅, BRDFs ✅, noise ✅, fullscreen ✅, material_fetch ✅, cascaded_shadow ✅) | ✅ done |
| 2 | Shared/simple entry points → SPIR-V (C++) + WGSL (Rust), Slang bindings resolve layout | ✅ done (tonemap, post, forward, sky, bloom, ssao, ibl, etc.) |
| 3 | Vulkan-only shaders (raytracing, path_tracing, deferred, rasterizer, shadows, skybox, clouds, noise) → Slang → SPIR-V | ✅ done |
| 4 | Retire the naga `export_shaders` path; wire Rust renderer to Slang WGSL | 🟡 in progress |

**50 Slang artifacts** total (SPIR-V + WGSL), all emit cleanly.

**C++ side: ALL shaders ported + ALL 8 loading sites wired to Slang SPIR-V.**

**Rust side: ALL shaders ported to Slang (emit WGSL).** The Rust renderer
still loads hand-written WGSL via `include_str!` — wiring it to the Slang-emitted
WGSL is the remaining step. This requires either:
- Changing the Rust `include_str!` paths (submodule change to
  `Kataglyphis-RustProjectTemplate`), or
- Adding a `build.rs` that compiles Slang and emits WGSL into the crate's
  shader directory, or
- Restructuring the compile script to produce combined WGSL files (Slang emits
  one file per entry point; the Rust renderer expects a single file with
  multiple entry points).

**WGSL fallback:** `histogram.wgsl` stays hand-written — Slang's
`InterlockedAdd` on `RWStructuredBuffer` is not supported for the WGSL target
(WGSL requires `array<atomic<u32>>` storage).

**WGSL fallback policy:** if Slang's experimental WGSL emitter can't handle a
particular shared shader, that shader's WGSL stays hand-written; everything
else flows from Slang. (Decided 2026-07-30.)

### Open items

- **Rust renderer WGSL wiring**: the Rust crate (`Kataglyphis-RustProjectTemplate`)
  still loads hand-written WGSL via `include_str!`. Wiring it to the Slang-emitted
  WGSL requires submodule changes (changing `include_str!` paths, adding a
  `build.rs`, or restructuring the compile script to produce combined WGSL files).
- **Visual parity verification**: no Slang shader has been run on a GPU and
  compared against the original. Compilation ≠ correctness.
- **Container `slangc`**: the Linux ContainerHub image builds `slang` as a
  Vulkan SDK component; the Windows container image's `slangc` availability is
  **unverified**.
- **Old GLSL/WGSL removal**: the old `Resources/Shaders/` GLSL and the Rust
  crate's hand-written WGSL **cannot be removed** until the Rust renderer is
  wired to Slang WGSL and visual parity is verified.
- **CI guard**: `brdf_test.slang` and `noise_test.slang` dual-emit in
  `compile-slang-shaders.ps1` (the script fails the build if any manifest entry
  fails to emit). A dedicated test binary (like the Rust `tests/shader_export.rs`)
  is not yet added.

## Beyond shaders

The bigger cross-renderer wins on the roadmap
(`webgpu-renderer-roadmap.md`, Phase G) are:

- **Shared assets** — OBJ→glTF conversion so both renderers eat the same
  scenes (the C++ engine's `Resources/Models` becomes directly usable).
- **Side-by-side comparison harness** — same scene, same camera, diff the
  Vulkan and WebGPU screenshots. With shared BRDF math the images should
  match closely, which turns the diff into a regression net for *both*
  renderers.
