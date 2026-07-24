# ROADMAP

Single source of truth for open, unblocked, actionable work.
Each task lists exact files, line numbers, steps, and verification.
Run verification commands FROM THE REPO ROOT.

## Status key
- `[ ]` open  `[x]` done  `BLOCKED` needs external thing first  `SKIP` parked/owner decision

---

## S — half-day or less

### [ ] Wasm size budget CI gate

**Repo:** `ExternalLib/Kataglyphis-RustProjectTemplate`

1. Add `cargo build --target wasm32-unknown-unknown --release -p kataglyphis_webgpu_renderer` to a CI step
2. Measure resulting `.wasm` size
3. Fail CI if > threshold (start at 4.0 MB uncompressed)
4. Run `wasm-opt -Oz` before measuring

**Files:** `.github/workflows/*.yml` in RPT repo

**Verify:** `cargo build --target wasm32-unknown-unknown --release -p kataglyphis_webgpu_renderer` succeeds, wasm file exists

---

### [ ] Headless GPU timing JSON dump benchmark

**Repo:** root (C++ Vulkan engine)

**What:** `KATAGLYPHIS_GPU_TIMING_JSON=<path>` already writes per-pass GPU ms to JSON. Add a gtest that:
1. Sets the env var
2. Renders N frames (golden harness)
3. Parses the JSON
4. Asserts the file contains expected pass names + each timing is finite and non-negative

**File:** `Test/commit/VulkanEngine/goldenRenderSuite.cpp`
**Ref:** `VulkanRenderer.cpp:1283` (getenv path), `VulkanRenderer.cpp:1270-1310` (JSON write)

**Verify:** `docker exec bb-build-persistent cmd /c "set KATAGLYPHIS_GPU_TIMING_JSON=C:\ws\gpu_timing.json && C:\ws\build-clangcl-debug\Test\commit\VulkanEngine\commitTestSuite.exe --gtest_filter=*GpuTiming*"`

---

### [ ] Headless GPU timing comparison in CI

**Repo:** root

**What:** `Scripts/Compare-RendererTimings.ps1` runs both renderers headlessly and prints one table. Add a CI step that:
1. Runs the script (both renderers must produce JSON)
2. Asserts the script exited 0
3. Asserts timings contain expected passes (ShadowCascades, Forward, Tonemap)

**File:** `Scripts/Compare-RendererTimings.ps1`

**Verify:** Run the script locally, check exit code

---

### [ ] Test all four clang-cl configurations in one session

**Repo:** root

**What:** One PowerShell script that runs:
```
Scripts/Windows/Build-Windows-Container.ps1 -Configurations "clangcl-debug,clangcl-profile,clangcl-release" -SkipPerfTests
```
Plus (when Rancher Desktop is running):
```
Scripts/Linux/cmake-configure-build.sh --preset linux-debug-tsan-clang
```

**File:** New file `Scripts/test-all-configs.ps1`

**Verify:** Script runs without errors, each build exits 0

---

## M — roughly a day

### [ ] Consume generated SPIR-V in VulkanRenderer

**Repo:** root (C++ Vulkan engine)

**What:** The Rust renderer exports WGSL->SPIR-V (Build-Windows.ps1 `-ExportWgslShaders`). The C++ engine does NOT load these. Wire the generated SPIR-V files into the Vulkan pipeline so a WGSL change reaches both renderers.

**Blockers documented in** `docs/shader-sharing.md`: WebGPU bind groups are not Vulkan descriptor sets. Binding decorations must be reconciled with this engine's layout before loading.

**Files:**
- `Resources/Shaders/generated/` (WGSL->SPIR-V output, gitignored)
- `Src/GraphicsEngineVulkan/renderer/VulkanRenderer.cpp` (pipeline creation)

**Verify:** Container build succeeds. GoldenRender suite passes with generated SPIR-V loaded.

---

### [ ] Side-by-side comparison harness

**Repo:** root (cross-renderer)

**What:** Same glTF scene, same camera, render with Vulkan (C++) and WebGPU (Rust), diff screenshots pixel-by-pixel. Structural assertions that survive driver/machine differences.

**Prerequisites:** C++ glTF loading (done), Rust render-to-pixels (done), shared assets (done).

**File:** New script `Scripts/Compare-RendererPixels.ps1` + golden test

**Verify:** Script exits 0, diff within tolerance

---

### [ ] MSAA / anti-aliasing (Rust WebGPU)

**Repo:** `ExternalLib/Kataglyphis-RustProjectTemplate`

**What:** `MultisampleState::default()` on all four forward pipelines. Color MSAA needs matching MSAA depth. Blocked by: SSAO and occlusion-cull read depth as single-sample. Two approaches:
(a) Manual depth-resolve pass: MSAA depth → single-sample depth via fullscreen min/max blit
(b) Make SSAO/occlusion MSAA-depth-aware (per-sample loads)

**Files:**
- `crates/webgpu_renderer/src/render/forward.rs` (pipelines, render pass)
- `crates/webgpu_renderer/src/render/ssao.rs` (depth read)
- `crates/webgpu_renderer/src/render/occlusion.rs` (depth read)
- `crates/webgpu_renderer/src/shaders/ssao.wgsl` (depth read)
- `crates/webgpu_renderer/src/shaders/occlusion_bbox.wgsl` (if option b)

**Verify:** Headless test: diagonal edge produces intermediate-colour "partial" pixels under MSAA, only hard fg/bg pixels without.

---

### [ ] Indirect draws (Rust WebGPU)

**Repo:** `ExternalLib/Kataglyphis-RustProjectTemplate`

**What:** Draw arguments from GPU buffer (culling compute, batched submission). Indirect only pays once arguments come from GPU.

**Files:**
- `crates/webgpu_renderer/src/render/forward.rs` (draw loop)
- `crates/webgpu_renderer/src/render/occlusion.rs` (would produce draw counts)

**Prerequisite:** GPU occlusion culling producing per-primitive draw counts

**Verify:** Scene with N primitives, N occlusion-queried → draw count buffer has correct values

---

## L — multi-day

### [ ] Clustered/tiled lighting (Rust WebGPU)

**Repo:** `ExternalLib/Kataglyphis-RustProjectTemplate`

**What:** 4-light cap is fine today. Lift it when a real scene needs it. Cluster/tile the frustum, bin lights per cluster, sample the cluster's light list in forward shader.

**Files:**
- `crates/webgpu_renderer/src/shaders/forward.wgsl` (~line 386 `punctual_lighting`)
- `crates/webgpu_renderer/src/render/forward.rs` (light upload)

**Verify:** Headless test with 100+ point lights

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

---

## Completed (2026-07-24 batch, BUILD-VERIFIED)

- [x] C++: Delete point light dead code (10 files, SceneUBO restructured)
- [x] C++: Extract `reprovisionPerImageResources()` helper from `VulkanRenderer::recreateSwapChain`
- [x] C++: `Scene::update_user_input` takes `const GUISceneSharedVars &` instead of `GUI*`
- [x] Rust: NaN guards in `compute_world_transforms` + `update_cascades` (3 new CPU tests)
- [x] Rust: Uniform block optimization — `FrameUniforms` at @group(2), save ~576B/primitive
- [x] Rust: Shadow cascade render bundles — record once, execute 3× (0.047→0.029 ms)
- [x] Rust: All 15 deep-dive items audited — #1-#7, #9-#15 DONE; only #8 (MSAA) genuinely open
- [x] Container: Source tree pruning before tar stream in `Build-Windows-Container.ps1`
- [x] imgui.ini → `.gitignore` (`git rm --cached`)

### Build verification
| target | result |
|---|---|
| Windows container `clangcl-debug` | 3/3, 0 errors, 121 tests |
| Rust `cargo check --workspace` | clean, 2 benign warnings |
| Rust unit tests | 105/105 |
| Rust headless GPU tests | 205/205 (all suites) |
