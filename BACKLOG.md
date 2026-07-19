# BACKLOG

Ideas and recurring chores that are **not** committed roadmap items. Things
here are candidates: they graduate to [`ROADMAP.md`](ROADMAP.md) when they
get a size and a decision, or get dropped. Roadmap = what we agreed to do;
backlog = what we might.

## Performance testing

The perf suite currently benchmarks `std::string` create/copy — it measures
nothing about this engine, and it is registered outside CTest, so nothing
gates on it.

- **Benchmarks that mean something.** Candidates, in rough value order:
  - `record_commands` wall time per frame for each render mode (forward,
    deferred, RT, path tracing) at a fixed scene + camera — the closest
    proxy to "did a refactor make the frame path slower".
  - Upload path: `createBufferAndUploadVectorOnDevice` for a few payload
    sizes, now that the staging buffer is reused (guards against a
    regression back to per-upload create/destroy).
  - `ObjLoader::loadModel` on a bundled model — parsing plus mesh build.
  - Cascade math: `CascadedShadowMap::updateCascades` (pure CPU, no GPU
    needed, so it can run in CI).
  - Pure-CPU units are the ones worth gating in CI; anything touching the
    GPU is machine-dependent and belongs in the "run it locally" bucket.
- **GPU-side numbers already exist**: per-pass timestamps land in
  `GUIRendererSharedVars::gpuTimings` (GUI "GPU timings" header). A headless
  mode that renders N frames and dumps the per-pass averages as JSON would
  turn them into a comparable artifact instead of a number a human squints
  at.
- **Regression tracking**: Google Benchmark can emit JSON
  (`--benchmark_out=... --benchmark_out_format=json`); storing one baseline
  per machine and diffing beats eyeballing console output.

## Recurring validation runs

Debug-only builds are the default working loop (fast, sanitized). Things
that are *not* exercised that way and should be run periodically:

- **`clangcl-profile` (RelWithDebInfo) once in a while** — optimized code
  paths differ from debug: different inlining, different UB exposure,
  and it is the only configuration where the benchmarks are meaningful
  (debug timings are noise). Run it after any perf-relevant change and
  before a release; it also builds `perfTestSuite.exe`.
- **`clangcl-tsan`** — data races only show up here; nothing runs it today.
- **Synchronization validation** — `khronos_validation.validate_sync = true`
  in `vk_layer_settings.txt` next to the executable. This found 10 real
  WRITE-AFTER-WRITE hazards in July 2026; it is not part of any automated
  run, so it needs a deliberate pass after touching render passes,
  barriers, or frames-in-flight.
- **Release build** — the only configuration with logging compiled out and
  validation layers absent; behavioral surprises hide there.

## Test coverage ideas

- Headless offscreen rendering in the C++ engine, mirroring the Rust
  renderer's structural pixel assertions (colour dominance, coverage
  ratios, energy deltas rather than exact images). This is the missing
  piece that would have caught the "shadow map rendered but never sampled"
  bug — tracked as a sized item in `ROADMAP.md`.
- Fuzz the remaining untrusted inputs: `SceneConfig` parsing, the shader
  file reader, KTX2/texture loading paths.
- A GUI-state round-trip test (`GUISceneSharedVars` → renderer → back)
  so option plumbing cannot silently break, as the cascade-count default
  did.
- Property tests for `CascadedShadowMap::updateCascades`: splits strictly
  increasing, cascade frustums covering the camera frustum, no NaNs at
  degenerate FOV/aspect.

## Housekeeping candidates

- The `x64-Clang-Windows-Release` preset survives only because
  `windows-clang-release-wix` packages from it; if WiX packaging moves to
  ClangCL, that preset can go too.
- `imgui.ini` is tracked and changes whenever a window is dragged — decide
  whether it is source (layout you want shipped) or user state (gitignore).
