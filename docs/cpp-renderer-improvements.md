# C++ Renderer Improvement Campaign (2026-07)

Working log of the "fix all" pass over `Src/GraphicsEngineVulkan`, driven by
a three-way analysis (architecture/complexity, testing, performance). Each
unit ships only after: container debug build green, 14/14 tests, and a
validation-layer-clean runtime check where rendering changed.

## Shipped

| Commit | Unit | Notes |
| --- | --- | --- |
| `65cf9229` | **Consume the cascaded shadow map** | The CSM pass rendered 3×2048² depth layers per frame that no compiled shader sampled. Forward lighting now selects a cascade by distance + PCF (GUI slider) + `cascaded_shadow_intensity`. Same commit: `GLM_FORCE_DEPTH_ZERO_TO_ONE` moved from App.cpp (no projection math) to the engine target — camera/CSM projections were using [-1,1] depth in a [0,1] API; GUI cascade default synced to `MAX_CASCADES` |
| `20107238` | **Real fuzzing + CI + ASan presets** | `ObjLoader::loadVertices` bounds-hardened (negative/out-of-range indices → OOB reads; `exit()` on parse error killed the app on GUI model reload). `obj_parsing_fuzz_test` mirrors the walk; 60 s coverage-guided ASan fuzzing, zero findings. Stale `RendererTest.BasicSetup` ctest filter removed; fuzz target added to Linux CI; `x64-ClangCL-Windows-Debug-ASan` presets added (matrix had TSan only) |
| `9e900dbd` | **Fail-fast `ASSERT_VULKAN`** | Was log-and-continue → null-handle UB later. Exceptions are disabled project-wide (`/EHs-`, `VULKAN_HPP_NO_EXCEPTIONS`), so the macro now logs critical + `abort()`. All 41 call sites audited: creation/allocation only |
| `522d0c9f` | **Draw-loop perf** | Invariant descriptor set bound once instead of per mesh; per-draw `std::vector<vk::Buffer>` heap churn removed (rasterizer + all CSM cascade loops) |
| `be60a235` | **Deferred-path shadows** | Shared `common/cascaded_shadow.glsl`; both lighting paths use the same cascades |
| `ad77cbdd` | **Pipeline cache** | Persisted in `VulkanDevice`, all pipeline sites consume it; two-run verified (saves on graceful exit only) |
| `b06aaf27` (folded in) | **RAII leaf types** | `VulkanBuffer`/`VulkanImage` became move-only with destructor release during the VMA rewrite; verified via full teardown tests + clean exit |
| `326a6eae` | **Staging ring + fence-synced uploads** | Per-upload `queue.waitIdle()` → per-submit fence; one persistent mapped staging buffer with geometric growth |
| `b06aaf27` | **VMA adoption** | Allocator moved into `VulkanDevice`; `VulkanBuffer`/`VulkanImage` on `vmaCreateBuffer/Image`; persistently mapped UBOs via `MAPPED_BIT`; RT device addresses intact. Verified incl. RT/path-trace GPU tests |
| (pending) | **First-frame delta_time clamp + first input-handling tests** | `last_time` started at 0, so the first frame's delta was the ENTIRE startup wall clock (window + scene + renderer construction, seconds) - a key held during load teleported the camera. Pure `clamp_frame_delta` (10 FPS cap, negatives to zero) wired into `update_frame_timing`. Plus the first coverage for the input path that routes ALL keyboard/mouse into the camera: 8 tests over key press/release/REPEAT, out-of-range keys (ASan-enforced), first-mouse-move zero-delta, consume-and-reset axis deltas, and the ImGui capture gate tested in BOTH directions with a real context. ESC-to-close and cursor-callback swapping deliberately untested (need a live GLFWwindow); both suites registered in the Windows CI CPU filter, without which they would silently never run there. Verified: 80/80, validation-clean run |
| `961a9a7a` | **Deferred mode was unreachable end-to-end; parity golden was vacuous** | Two real defects, found while chasing three phantom ones: (1) the GUI mode radios lived in function-local statics and wrote themselves back into the shared vars every frame, stomping any programmatic mode change; (2) the post pass's input descriptor was written once at init, so even a surviving mode switch presented the STALE forward image - proven by forcing the deferred lighting shader to pure red and observing an unchanged capture. Both fixed (radios derive from shared vars; descriptors rebind on mode change). The three "deferred shader defects" from survey 2 turned out to live in DEAD legacy files (`rasterizer/g_buffer_*`, unreferenced by any code) - the live `deferred/{geometry,lighting}` pair is healthy. Golden upgraded: absolute per-channel means (a probe shader is visible there; a relative diff of two equally-affected captures is not) + per-pixel diff with a measured threshold - clean parity 0.203/channel, a single deliberate extra tonemap in the LIVE shader pushes it to 2.105, threshold 1.0. Verified: red/green on the live shader, 72/72, validation-clean 9s run |
| `7a1a4ade` | **Shadow pancaking via depth clamp** | Casters between the light and a cascade's near plane were CLIPPED (near hugs the camera frustum + 10-unit pad; `depthClampEnable` was false) while `isVisibleAsShadowCaster` deliberately keeps them - a ceiling/overhang cast nothing. Fix: `PipelineBuilder::setDepthClamp` + guarded `depthClamp` device feature + enabled on the CSM pipeline. First attempt (extending the near plane to scene bounds) was REJECTED by `GoldenRender.ShadowsDarkenSomePixels` (2.4% darkened vs 4% required): the shader bias is constant in normalized depth, so widening the range scales it in world units and eats contact shadows - the 60x-scaled rig amplified it. Clamp keeps the range tight. Verified: 72/72 incl. the golden, validation-clean 9s run. Open: a raised-slab golden proving the rescue itself |
| `d120610c` | **PipelineBuilder** | The copy-pasted ~70–100-line pipeline-construction block across Rasterizer/PostStage/DeferredRasterizer/SkyBox/CascadedShadowMap became `kataglyphis.vulkan.pipeline_builder`; stages state only what differs. −416/+51 at call sites; configurations preserved bit-for-bit |

## In progress

(nothing — remaining queue: stage/renderer-level RAII, sync-validated barrier removal, GPU timestamps)

## Queued (design notes)

1. **Prebuilt SPIR-V consumption** — the `VkPipelineCache` half shipped
   (`ad77cbdd`, persisted in `VulkanDevice`), but pipelines still recompile
   GLSL from source on every build (startup + hot-reload stalls). Remaining:
   consume `Resources/Shaders/**/spv` instead of runtime GLSL compilation.
3. **`vk::raii` teardown migration** — ~30 manual `cleanUp()` methods with
   defaulted destructors; hand-ordered 48-line teardown; device-lost
   special-casing in App.cpp. Migrate leaf types first (`VulkanBuffer`,
   `VulkanImage`, samplers), then stages, then the renderer.
5. **Redundant same-layout swapchain barrier** (`VulkanRenderer.cpp` post-sky) —
   remove only after a sync-validation (`VK_LAYER_KHRONOS_validation` with
   `VALIDATION_CHECK_ENABLE_SYNCHRONIZATION_VALIDATION`) run confirms the
   post render pass's external dependency covers it.
6. **Per-pass GPU timestamps + debug labels** — nothing is measured on-device
   today; prerequisite for honest perf claims beyond the structural fixes.

## Verification pattern

This is the canonical description; `AGENTS.md` links here rather than
restating it.

Container build (`Scripts/Windows/Build-Windows-Container.ps1
-Configurations 'clangcl-debug'`, tar-pipe fallback on Dev Drive) →
`build-clangcl-debug/commitTestSuite.exe` directly (host ctest cannot read
the container's CMake tree) → 8–10 s engine run with stderr captured,
grepping validation output. GPU integration tests (`Integration.*`) run on
the host RX 9070 XT.
