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
| `d120610c` | **PipelineBuilder** | The copy-pasted ~70–100-line pipeline-construction block across Rasterizer/PostStage/DeferredRasterizer/SkyBox/CascadedShadowMap became `kataglyphis.vulkan.pipeline_builder`; stages state only what differs. −416/+51 at call sites; configurations preserved bit-for-bit |

## In progress

(nothing — remaining queue: stage/renderer-level RAII, sync-validated barrier removal, GPU timestamps)

## Queued (design notes)

1. **Pipeline cache + prebuilt SPIR-V** — no `VkPipelineCache` anywhere; GLSL
   recompiles from source on every pipeline build (startup + hot-reload
   stalls). Persist a cache under `build-*/pipeline.cache`; optionally consume
   `Resources/Shaders/**/spv` instead of runtime GLSL compilation.
3. **`vk::raii` teardown migration** — ~30 manual `cleanUp()` methods with
   defaulted destructors; hand-ordered 48-line teardown; device-lost
   special-casing in App.cpp. Migrate leaf types first (`VulkanBuffer`,
   `VulkanImage`, samplers), then stages, then the renderer.
4. **Deferred-path shadows** — `deferred/lighting.frag` still ignores the CSM
   (forward is fixed); same binding is available in the shared set.
5. **Redundant same-layout swapchain barrier** (`VulkanRenderer.cpp` post-sky) —
   remove only after a sync-validation (`VK_LAYER_KHRONOS_validation` with
   `VALIDATION_CHECK_ENABLE_SYNCHRONIZATION_VALIDATION`) run confirms the
   post render pass's external dependency covers it.
6. **Per-pass GPU timestamps + debug labels** — nothing is measured on-device
   today; prerequisite for honest perf claims beyond the structural fixes.

## Verification pattern

Container build (`Scripts/Windows/Build-Windows-Container.ps1
-Configurations 'clangcl-debug'`, tar-pipe fallback on Dev Drive) →
`build-clangcl-debug/commitTestSuite.exe` directly (host ctest cannot read
the container's CMake tree) → 8–10 s engine run with stderr captured,
grepping validation output. GPU integration tests (`Integration.*`) run on
the host RX 9070 XT.
