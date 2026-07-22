# Path Tracing

State of the path-tracing mode after the 2026-07-22 overhaul (commits
`4181e0da` → `110b1503`). Everything here describes **shipped** behaviour;
open work is listed at the end with its BACKLOG anchors.

## Pipeline shape

One compute kernel, `Resources/Shaders/path_tracing/path_tracing.comp`,
dispatched by `PathTracing::recordCommands` between two image barriers that
hand the rasterizer's offscreen target (`rgba16f` since the HDR unit,
`OUT_IMAGE_BINDING`) from the graphics to the compute queue family and back. The post pass then samples
that target like any other mode - path tracing replaces the *lighting*, not
the presentation path.

Ray traversal uses `GL_EXT_ray_query` against the same TLAS the RT mode
uses; geometry/material data arrive via buffer-device-address
(`ObjectDescription` → vertex/index/material-id/material arrays). The record
path dispatches only when a TLAS exists - during the async model load there
is nothing to trace against, and dispatching anyway used to consume
never-written descriptor sets.

## The estimator

Per pixel and frame, `samples_per_pixel` independent paths (GUI slider,
default 8), each up to `max_bounces` segments (GUI slider, default 8);
changing either slider resets the accumulation (a mean over two different
estimators is biased):

- **Primary ray** from the precomputed `inv_view`/`inv_projection` in
  `GlobalUBO` (the kernel used to invert both matrices *per sample* - 24
  4x4 inversions per pixel per frame).
- **Hit**: throughput *= albedo (texture, or `material.diffuse` when
  `textureID` is -1). Normals are transformed as directions
  (inverse-transpose via row-multiplying `worldToObject`); the hit normal is
  face-forwarded against the ray.
- **Next-event estimation**: one shadow ray per bounce toward the GUI
  directional light; unoccluded hits contribute
  `throughput * NdotL * (1/pi) * dirLight.color.rgb * dirLight.color.w`
  (the throughput carries the albedo; NEE carries the Lambertian 1/pi -
  measured, the missing factor left lit surfaces at 208.5 crop luminance vs
  188.2 correct, with forward at 158.1 and the gap being PT's indirect
  sky). This is the only connection to the GUI light - before it existed,
  the mode's sole light source was an accidental radiance-1 "white furnace"
  on miss. The BOUNCE path needs no 1/pi: cosine-weighted sampling of a
  Lambertian cancels the pi and the cosine exactly.
- **Bounce**: RTIOW-style cosine-ish sampling (normal + random unit vector),
  with the degenerate-scatter guard (near-zero sum falls back to the
  normal). From the fourth segment on, **Russian roulette** terminates
  low-throughput paths and reweights survivors (unbiased).
- **Miss**: a soft RTIOW gradient sky at half intensity ends the path.

Self-intersection is guarded by a 1e-4 normal offset **and** ray-query
`t_min = 0.001` (matches the RT path).

## Temporal accumulation

The kernel folds the frame index into the RNG seed (without it, every frame
drew bit-identical samples) and maintains a running mean in a dedicated
`rgba32f` history image (`ACCUMULATION_IMAGE_BINDING`) - full float because
averaging in the `rgba8` output would quantize and stall after a few frames.
There is exactly **one** history image, deliberately not per swapchain
image; a compute→compute barrier orders each frame's read-modify-write
against the previous frame's dispatch across command buffers.

The history resets whenever its premises die:

| Trigger | Where |
| --- | --- |
| Camera moved (view-matrix compare) | PT branch of `record_commands` |
| Quality sliders changed (different estimator = biased mean) | same |
| Swapchain resize (extent-sized image is recreated) | `recreateSwapChain` |
| AS rebuilt - model load/reload OR GUI transform moved the traced world | `updateRaytracingDescriptorSets` |

The AS-rebuild trigger was found the hard way: without it, the mean kept
blending frames of the *half-loaded* scene until the camera happened to
move, and that healing motion mimicked convergence well enough to make a
naive golden pass with per-frame sampling disabled.

## Verification

Four PT-facing goldens in `Test/commit/VulkanEngine/goldenRenderSuite.cpp`,
each red/green-proven against the pre-fix kernel (numbers are post-HDR;
lifting the UNORM ceiling widened several of them dramatically):

- `PathTracingAccumulatesAndConverges` - consecutive-frame changed-pixel
  fraction in a GUI-free crop: 7.6e-3 early, 3.1e-5 late; the
  frame-invariant seed measures exactly 0 early and fails.
- `PathTracingRespondsToTheDirectionalLight` - radiance 10 vs 0 on the
  shadow rig: swung fraction 0.751 (0.027 pre-HDR - the ceiling hid three
  quarters of the response) vs exactly 0 for the pre-NEE kernel.
- `PathTracingHonorsTheQualityControls` - bounce cap 8 vs 1 swings 0.255
  (all indirect sky light vanishes); hardcoded-bounds kernel: 7.3e-5.
- `RaytracedWorldFollowsTheModelTransform` - a GUI move must change the
  traced image: 0.397 vs exactly 0 on a stale TLAS.

Instrument lessons that cost real hours, preserved in the test comments: the
ImGui panel covers the left ~70% of the capture; whole-frame or centre-crop
means measure FPS-counter digits, not the scene; before the HDR unit, lit
surfaces clamped at the UNORM ceiling (186 after tonemap) and hid most of
every light response. Measure changed-pixel fractions in the panel-free
right edge, and dump amplified diff-map PNGs before trusting any new pixel
metric.

Editing the kernel no longer needs a manual compile step: glslc resolves at
runtime (baked path -> `VULKAN_SDK` -> PATH, fixed 2026-07-22) and the
engine recompiles an edited source at startup, loudly reporting any compiler
failure. `Scripts/Windows/compile-shaders.ps1` remains the bulk/CI path.

## Open work (BACKLOG, "PT survey" section)

- Furnace golden: wants a uniform-environment toggle (the sky is a
  gradient), folded into the PT-goldens remainder of item 10.
- RNG decorrelation (item 11, partially done via the frame fold): linear
  seeds still correlate neighbours within a frame.
- Runtime glslc resolution (new, 2026-07-22): resolve at runtime instead of
  the baked container path, and check the `system()` return.
