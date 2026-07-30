# GPU golden testing

The `GoldenRender.*` and `Integration.*` suites in
`Test/commit/VulkanEngine/` render real frames on a real GPU and assert on the
captured pixels. They are the end-to-end safety net for render- and
device-path changes that the CPU unit suites cannot cover.

## They skip without a GPU

Each of these tests begins with `SKIP_WITHOUT_GPU()` (or an equivalent
GLFW/Vulkan-runtime guard). In the headless container build
(`Scripts/Windows/Build-Windows-Container.ps1 -RunTests`) there is no GPU, so
they report **skipped**, not passed. A green container build therefore proves
the code *compiles and links* and that the CPU suites pass — it does **not**
prove a render/device refactor is behaviour-preserving.

## Running them on the host GPU

The container build delivers the built test executable back into the working
tree at `build-clangcl-debug/commitTestSuite.exe`. On a host with a GPU it runs
the golden tests for real. Run it **from the repository root**:

```
cd <repo root>
./build-clangcl-debug/commitTestSuite.exe --gtest_filter='GoldenRender.*:Integration.*'
```

**The working directory matters.** The Slang-emitted SPIR-V shaders are loaded
via *cwd-relative* paths (e.g. `Resources/ShadersSlang/build/spirv/...`). Running
from `build-clangcl-debug/` fails with an access violation /
empty-`codeSize` `vkCreateShaderModule` — that is a wrong cwd, **not** a code
bug. Run from the repo root, where `Resources/` resolves.

The executable carries AddressSanitizer/UBSan (the debug config's flags); it
runs fine on the host.

## The verification loop for render/device changes

For a behaviour-preserving refactor of the record path, a pipeline, a device
feature, an image transition, or the loader upload path:

1. Build in the container (`-FreshContainer` whenever a `.ixx` module
   interface changed — otherwise stale BMIs can ASan-fault at member init).
2. Run the golden + integration suites on the host GPU as above.
3. All tests passing = the recorded frames are unchanged = the refactor is
   render-equivalent. As of 2026-07-23 the baseline is 19 `GoldenRender` +
   2 `Integration` tests, ~44 s.

This turns changes the container can only compile-check (device creation,
image barriers, the deferred/forward command streams, path/ray tracing) into
verifiable ones.

## Writing a new golden test — cautions learned the hard way

The suite's own comments document two expensive mistakes; heed them:

- **Captures are tonemapped**, and the **ImGui overlay is composited into
  them**. A pixel classifier written against raw scene colours (e.g.
  `r < 60 && b < 60`) can silently measure only the overlay. Crop away from the
  overlay (the existing tests crop to the right/lower scene region) and prefer
  luminance-delta oracles over absolute-colour thresholds.
- **A count says how much changed; only the shape says whether what changed is
  the effect.** Always capture an unconditional control (the effect disabled)
  and a same-state noise reference in the same run, and compare against them.
  `GoldenRender.DISABLED_DumpsFrameToPng` dumps the frame, the control, and an
  amplified difference to PNG — use it (`--gtest_also_run_disabled_tests
  --gtest_filter=*DumpsFrameToPng*`, `KATAGLYPHIS_FRAME_DUMP=out`) to *look* at
  what a metric is really measuring before trusting it.

Effects with no runtime toggle (the tonemap is always on) are hard to isolate
this way: "compressed vs clipped" is ambiguous without a control, so a robust
oracle must key on a property the effect uniquely produces (e.g. retained
variation), not just "brighter" or "darker".
