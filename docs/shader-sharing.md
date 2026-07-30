# Sharing Shader Code Between the C++ Vulkan Engine and the Rust WebGPU Renderer

**Yes — via [Slang](https://shader-slang.com/), with Slang as the single
source of truth.** One `.slang` source compiles to **SPIR-V** (Vulkan/C++)
and **WGSL** (WebGPU/Rust), unifying the shader codebase between both
renderers, including shared entry points where the two renderers' passes
coincide.

## The pipeline (current)

```pwsh
pwsh -ExecutionPolicy Bypass -File .\Scripts\Windows\compile-slang-shaders.ps1
```

Wired into the C++ build unconditionally (no opt-in flag needed): compiling
either `clangcl-*` or `linux-*` configuration compiles the Slang manifest
first, so a `.slang` edit reaches the Vulkan engine on the next build.

`slangc` is resolved from `VULKAN_SDK\Bin` then `PATH` (the Vulkan SDK ships
it; verified against 1.4.350.0). Output lands in
`Resources/ShadersSlang/build/{spv,wgsl}/` (gitignored, derived, regenerated
on every Slang edit — staleness is timestamp-based, conservative: any
`.slang` file changing invalidates every output, since imports are not
tracked individually).

**Rust side:** the same script additionally compiles each shared shader
*without* `-entry`/`-stage` (Slang then emits every entry point of that file
into one combined WGSL file) and copies the result straight into the Rust
crate's shader directories (`crates/webgpu_renderer/src/shaders/`,
`crates/gui/src/shaders/`), replacing the hand-written WGSL there. The
crate's `include_str!` calls are unchanged — they just now read
Slang-emitted text. `histogram.wgsl` is the one deliberate holdout (see
below).

## Why Slang (not GLSL, not naga/WGSL-as-source)

WGSL used to be the shared-math source of truth here, translated with
`naga` into GLSL the C++ engine `#include`d (see **Historical note**
below for why that route was retired). That approach could share
*functions* but not *entry points*: the generated SPIR-V/GLSL carried
WebGPU binding decorations that do not match the Vulkan engine's descriptor
layout.

Slang removes that limit. Its parameter-binding system
(`[vk::binding(i,s)]` → `@binding(i) @group(s)` in WGSL) is the lever that
aligns the two binding models, so entry points can be shared too, not just
math. It also targets **SPIR-V directly**, including the ray tracing
pipeline and ray query — neither of which WebGPU/WGSL has, so those shaders
are Slang-authored for language unity on the C++ side even though they only
ever emit one target.

### Target status (verified against slangc in VulkanSDK 1.4.350.0)

| Target | Status | Notes |
| --- | --- | --- |
| SPIR-V (Vulkan) | **stable** | Rasterization, compute, **ray tracing pipeline**, **ray query**, descriptor sets, push constants |
| WGSL (WebGPU) | **experimental** | Functional (official `wgpu-html5` example uses it). **No** ray tracing, ray query, mesh/tessellation/geometry shaders, wave intrinsics, `f64`, `i8`/`u8` |

**WGSL fallback policy:** if Slang's experimental WGSL emitter can't handle
a particular shared shader, that shader's WGSL stays hand-written and the
Slang source (if one exists) is kept for documentation/future SPIR-V use
only. `histogram.wgsl` is the one case today: Slang's `InterlockedAdd` on
`RWStructuredBuffer` is not supported for the WGSL target (WGSL requires
`array<atomic<u32>>` storage).

## Architecture: Slang-native, not `#include`

Slang is module- and entry-point-centric. A no-entry-point math module
cannot be emitted to GLSL/WGSL text directly, and imported functions are
name-mangled in the emit, so sharing works through Slang's own module
system rather than textual `#include`:

- **Shared math** lives in Slang modules under `Resources/ShadersSlang/common/`
  (`aces.slang`, `brdf.slang`, `noise.slang`, `fullscreen.slang`, plus
  `material_fetch.slang` and `cascaded_shadow.slang`).
- **Entry points** are Slang shaders that `import` the math modules and
  compile to whichever target(s) their renderer needs. Mangling is a
  non-issue because Slang links modules internally.
- Each renderer keeps its own entry points where the passes genuinely
  differ (they composite differently); both `import` the same math.

## What is wired today

**Shared math modules** (imported by entry points, never emitted directly):

- `common/aces.slang` — ACES filmic tonemap (Narkowicz 2015) + exact IEC
  61966-2-1 sRGB encode.
- `common/brdf.slang` — the Epic Games / Unreal 4 microfacet PBR model
  (`distribution_ggx`, `geometry_smith`, `fresnel_schlick`, `brdf_direct`),
  shared by both renderers' forward/RT shading.
- `common/noise.slang` — 3D simplex noise + fractal Brownian motion, used
  by both renderers' cloud/procedural passes.
- `common/fullscreen.slang` — the shared fullscreen-triangle vertex trick
  (`vid/2*4-1`), used by every fullscreen pass on both sides.

**Entry points compiled to both targets** (Rust/WebGPU + C++/Vulkan share
the pass): forward PBR (`forward/forward.slang`), sky
(`sky/sky.slang`), bloom (`bloom/bloom.slang`), SSAO (`ssao/ssao.slang`),
IBL precompute (`ibl/ibl.slang`), GPU occlusion culling
(`gpu_cull/gpu_cull.slang`), tonemap (`tonemap/tonemap.slang`, WGSL only —
the C++ side's fullscreen post pass is `post/post.slang` instead, SPIR-V
only, since it also handles cloud compositing via push constants), depth
resolve, occlusion bbox, and the GUI tex quad.

**Vulkan-only** (SPIR-V emit — WebGPU has no RT pipeline, and these still
`import` the shared math): `raytracing/*.slang` (rgen/rchit/rmiss, shadow
miss), `path_tracing/path_tracing.slang` (ray query), plus the
raster-only-on-Vulkan set: `rasterizer/rasterizer.slang`,
`deferred/deferred.slang`, `rasterizer/shadows/shadow_map.slang`,
`skybox/skybox.slang`, `compute/noise.slang`, `compute/clouds.slang`.

**CI guards:** `tests/brdf_test.slang` and `tests/noise_test.slang` each
`import` a shared math module and dual-emit to SPIR-V + WGSL, so a change
that breaks either target's compile fails the manifest run in
`compile-slang-shaders.ps1` before it reaches either renderer.

**Status:** all C++ shaders are on Slang SPIR-V (all 8 loading sites), and
all shared Rust shaders are on Slang-emitted WGSL. The migration described
in earlier revisions of this document as "in progress" is complete.

## Beyond shaders

The bigger cross-renderer wins on the roadmap
(`webgpu-renderer-roadmap.md`, Phase G) are:

- **Shared assets** — OBJ→glTF conversion so both renderers eat the same
  scenes (the C++ engine's `Resources/Models` becomes directly usable).
- **Side-by-side comparison harness** — same scene, same camera, diff the
  Vulkan and WebGPU screenshots. With shared BRDF math the images should
  match closely, which turns the diff into a regression net for *both*
  renderers.

## Historical note: the retired naga/WGSL-export route (2026-07-20 to 2026-07-30)

Before Slang, the plan was to keep WGSL (not Slang) as the source of truth:
`cargo run -p kataglyphis_webgpu_renderer --example export_shaders` (naga)
translated the Rust renderer's hand-written WGSL into SPIR-V/GLSL for the
C++ side, opt-in via a `-ExportWgslShaders` flag on `Build-Windows.ps1` /
`Build-Windows-Container.ps1`, output to the gitignored
`Resources/Shaders/generated/`. It could share *functions* (a
`Resources/Shaders/generated/aces.glsl`/`brdf.glsl` `#include` POC worked)
but never *entry points*, for the binding-model mismatch reason described
above — that limit is what motivated the Slang migration.

Retired 2026-07-30: nothing in the C++ engine ever consumed the exported
output (`grep -rn "\.spv" Src/` shows every SPIR-V load coming from
`Resources/ShadersSlang/build/spirv/`), so the flag, its build-script
plumbing, and the `Resources/Shaders/generated/` gitignore entry were
removed. The `export_shaders` naga example itself stays in the Rust crate
upstream — it is harmless and still useful for inspecting naga's WGSL→SPIR-V
output — this repo just no longer treats it as the sharing route.
