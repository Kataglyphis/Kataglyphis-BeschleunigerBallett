# Shader Build Pipeline (Slang → SPIR-V/WGSL)

How Slang becomes SPIR-V and WGSL here. `AGENTS.md` links here rather than
restating it.

## How it works

Shaders are written in [Slang](https://shader-slang.com/) under
`Resources/ShadersSlang/`. The build scripts
(`Scripts/Windows/compile-slang-shaders.ps1`,
`Scripts/Linux/compile-slang-shaders.sh`) compile each `.slang` file to:

- **SPIR-V** (`.spv`) for the C++ Vulkan renderer → `Resources/ShadersSlang/build/spirv/`
- **WGSL** (`.wgsl`) for the Rust WebGPU renderer → `Resources/ShadersSlang/build/wgsl/`
  (combined WGSL files are also copied to the Rust crate's `src/shaders/` directory)

The C++ renderer loads pre-compiled SPIR-V via `File` I/O — there is no
runtime shader compilation. Slang emits `"main"` as the SPIR-V entry point
name (not the Slang function name), so all `pName` values in pipeline
creation use `"main"`.

`histogram.wgsl` in the Rust crate remains hand-written: Slang does not
support `InterlockedAdd` on `RWStructuredBuffer` for the WGSL target.

## Staleness rules

An output is reused only when it is newer than its source **and** every
`.slang` file under the Slang tree (conservative — an import edit rebuilds
every dependent). The compile scripts walk the manifest and recompile only
stale entries.

## Fast shader iteration

Edit a `.slang` file, then recompile locally:

```pwsh
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Windows\compile-slang-shaders.ps1
```

Or on Linux:

```bash
bash Scripts/Linux/compile-slang-shaders.sh
```

The C++ engine loads the `.spv` files at startup via `File::readCharSequence`,
so no rebuild of the C++ binary is needed for a shader-only change — just
recompile the shaders and run.

## Historical note: the retired GLSL/glslc pipeline (until the Slang migration)

Before the Slang migration, shaders were hand-written GLSL under a
`Resources/Shaders/` tree, compiled per-file by `compile-shaders.ps1` via
`glslc`, with staleness checked independently by `ShaderHelper::compileShader`
at runtime. That tree and script no longer exist — `Resources/` now holds only
`ShadersSlang/`, `Models/`, and `Textures/`, and the two build scripts named
under "How it works" above are the only compile path. The staleness and
failure-visibility lessons from that era are still worth keeping, because the
Slang scripts were written to avoid repeating them:

- **Silent reuse of a stale output is the worst failure mode.** The GLSL
  pipeline once reused a `.spv` whenever the file merely existed, without
  comparing timestamps — a shader edit could go unexecuted indefinitely while
  the GPU kept running the old binary, which cost real debugging time on a
  shadow-rendering issue that turned out to be stale SPIR-V, not a logic bug.
  The Slang staleness rule above (reuse only when newer than source **and**
  every `.slang` file) exists because of that incident.
- **A compiler warning nobody acts on is indistinguishable from success.** A
  `glslc` include-path bug once silently failed to compile the main rasterizer
  fragment shader on every build (only a `Write-Warning`, previous `.spv` left
  on disk, build reported success) and was misdiagnosed as an intentional
  legacy limitation. `BuildIntegrity.EveryShaderSourceHasCompiledBinary`
  exists so a silently-skipped shader source fails a test instead of aging
  invisibly.

## Known gaps

- `Resources/ShadersSlang/build/` (compiled `.spv`/`.wgsl` output) is
  gitignored, not committed — a fresh clone must run the compile script
  before the engine has anything to load. The staleness checks under
  "Staleness rules" above only protect an existing checkout with stale
  outputs; they do not substitute for the initial compile.
