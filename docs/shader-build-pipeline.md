# Shader Build Pipeline (SPIR-V)

How GLSL becomes SPIR-V here, and a failure mode that cost hours in July 2026.
`AGENTS.md` links here rather than restating it.

## How it works

Shaders live in `Resources/Shaders/**` and compile to
`Resources/Shaders/<dir>/spv/<name>.spv`. Two paths produce them:

1. **Build time (preferred).** `Scripts/Windows/compile-shaders.ps1`, invoked
   by `Build-Windows.ps1` through `Invoke-ShaderPrecompile` for **every**
   configuration. Uses `glslc` from `VULKAN_SDK`, defines `KAT_VULKAN`, and
   passes every shader subdirectory as an include path.
2. **Runtime fallback.** `ShaderHelper::compileShader` compiles on demand in
   Debug builds (disabled in Release unless
   `KAT_ENABLE_RUNTIME_SHADER_COMPILATION` is set). Since 2026-07-22
   (`c246ded3`) it works on the HOST for container-built binaries too: glslc
   resolves at call time (baked build path → `VULKAN_SDK` → PATH) and a
   nonzero compiler exit is a loud spdlog error naming the stale spv that
   will be served. Before that, the baked container path made every host
   runtime compile a silent no-op. Practical consequence: a shader probe
   cycle on the host is edit → run one golden - no manual compile step.
   Pipelines also compile-then-read since `91a73cd1` (they used to read the
   spv BEFORE regenerating it, so an edit reached the GPU one process-start
   late).

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

```powershell
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
