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

## The failure mode (fixed 2026-07-19)

**Both layers reused a `.spv` whenever the file merely EXISTED.** Neither
compared timestamps. Consequences, all observed:

- Every shader edit after the first successful build was silently ignored.
  The GPU kept executing stale SPIR-V; the source on disk and the binary being
  run had nothing to do with each other.
- A `shader.frag` modified at 14:00 was still rendered from a `.spv` produced
  at 18:46 the previous day.
- Debugging was actively misleading: a debug visualisation compiled into the
  fragment shader produced byte-identical statistics run after run, because
  the edits never reached the GPU. Several hours of shadow-bug diagnosis were
  performed against a binary that did not match the source.
- `Invoke-ShaderPrecompile` was additionally only called for **Release**
  builds, so Debug — the normal working configuration — depended entirely on
  the broken runtime path.

**Fixes:**

- `compile-shaders.ps1` recompiles when the `.spv` is missing **or older than
  its source or any shared include** (`*.glsl`, `*.hpp`, `*.h` under
  `Resources/Shaders`). Conservative: editing a shared include rebuilds every
  dependent.
- `ShaderHelper::compileShader` compares `last_write_time` of source vs `.spv`
  and recompiles stale ones (logs `SPV is older than its source,
  recompiling`).
- `Invoke-ShaderPrecompile` now runs for Debug as well as Release.

## Iterating on shaders quickly

Regenerating SPIR-V does **not** require a rebuild — the engine loads `.spv`
at pipeline creation, so recompiling the shader and re-running the binary is
enough. During diagnosis this cut the loop from minutes to seconds:

```pwsh
$GV = 'C:\VulkanSDK\1.4.350.0\Bin\glslangValidator.exe'
$incs = Get-ChildItem Resources/Shaders -Recurse -Directory | ForEach-Object { "-I$($_.FullName)" }
& $GV --target-env vulkan1.3 @incs -ISrc/GraphicsEngineVulkan/renderer `
  -o Resources/Shaders/rasterizer/spv/shader.frag.spv Resources/Shaders/rasterizer/shader.frag
```

Note that `glslangValidator` (Vulkan SDK) and `glslc` differ slightly in
include handling; the build script uses `glslc`.

## The second failure mode: silent glslc failures (fixed 2026-07-19)

An earlier version of this document claimed that eight "legacy OpenGL-era"
shaders (`clouds/CloudsRectangle.*`, `rasterizer/g_buffer_*`) could not compile under the Vulkan
include convention, and that this was acceptable because no pipeline used
them. **Both halves were wrong.**

The real cause was that `compile-shaders.ps1` passed every shader
*subdirectory* to `glslc` as an include path, but never the shader **root**.
Any include written with a directory prefix — `#include
"hostDevice/host_device_shared_vars.hpp"` — therefore could not resolve. All
ten compile once the root is on the path.

This stayed invisible because a `glslc` failure was only a `Write-Warning`.
The previous `.spv` remained on disk and the build reported success, so:

- `rasterizer/shader.frag` — loaded by the **main pipeline every frame**, not
  a legacy file at all — was among the failures. Edits to it silently did
  nothing, which compounds the stale-SPIR-V problem above and may account for
  shadow behaviour that resisted diagnosis.
- The failures were durable enough to be mistaken for a property of the
  shaders and written into this document as a known limitation.

**Fixes:**

- `$shadersRoot` is now passed to `glslc` ahead of the subdirectories.
- A failed `glslc` invocation now **fails the script** (non-zero exit) and
  lists every shader that failed, instead of warning and continuing.
- `BuildIntegrity.EveryShaderSourceHasCompiledBinary` asserts that every
  shader source has a `.spv`, so a silently-skipped shader fails a test.

Lesson worth keeping: a warning that nothing acts on is indistinguishable from
success, and "these files just don't compile" is a claim to verify, not to
document.

## Known gaps

- `.spv` files are committed to the repo. That makes stale binaries possible
  in a fresh clone; the timestamp checks now handle it, but treating them as
  build artifacts would be cleaner.
