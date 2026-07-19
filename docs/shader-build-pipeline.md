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
   `KAT_ENABLE_RUNTIME_SHADER_COMPILATION` is set).

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

## Known gaps

- Ten legacy OpenGL-era shaders (`clouds/CloudsRectangle.*`,
  `rasterizer/g_buffer_*`, `rasterizer/shadows/omni_shadow_map.*`) fail to
  compile with the Vulkan include convention. They are not part of any Vulkan
  pipeline; leave them or port them deliberately.
- `.spv` files are committed to the repo. That makes stale binaries possible
  in a fresh clone; the timestamp checks now handle it, but treating them as
  build artifacts would be cleaner.
