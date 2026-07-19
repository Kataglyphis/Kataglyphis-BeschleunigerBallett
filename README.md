<div align="center">
  <a href="https://jonasheinle.de">
    <img src="images/logo.png" alt="logo" width="200" />
  </a>

  <h1>Kataglyphis-BeschleunigerBallett</h1>

  <h4>Experimental graphics engine and renderer playground for Vulkan, OpenGL, modern CMake, testing, packaging, and optional Rust integration.</h4>
</div>

<div align="center"> 
  <br> 
  <a href="https://jonasheinle.de"><img src="images/vulkan-logo.png" alt="VulkanEngine" width="200"></a>
  <a href="https://jonasheinle.de"><img src="images/Engine_logo.png" alt="VulkanEngine" width="200"></a>
  <a href="https://jonasheinle.de"><img src="images/glm_logo.png" alt="VulkanEngine" width="200"></a>
  <a href="https://jonasheinle.de"><img src="images/Opengl-logo.png" alt="OpenGLEngine" width="200"></a>
</div>

see also [**__Official homepage__**](https://beschleunigerballette.jonasheinle.de/). 

[![Linux build + test + coverage on Ubuntu 24.04 ARM](https://github.com/Kataglyphis/Kataglyphis-Renderer/actions/workflows/Linux_arm.yml/badge.svg?branch=main)](https://github.com/Kataglyphis/Kataglyphis-Renderer/actions/workflows/Linux_arm.yml)
[![Linux build + test + coverage on Ubuntu 24.04 x86](https://github.com/Kataglyphis/Kataglyphis-Renderer/actions/workflows/Linux_x86.yml/badge.svg)](https://github.com/Kataglyphis/Kataglyphis-Renderer/actions/workflows/Linux_x86.yml)
[![Windows Server 2025 build x86 MSVC and Clang](https://github.com/Kataglyphis/Kataglyphis-Renderer/actions/workflows/Windows.yml/badge.svg)](https://github.com/Kataglyphis/Kataglyphis-Renderer/actions/workflows/Windows.yml)  
[![CodeQL](https://github.com/Kataglyphis/Kataglyphis-BeschleunigerBallett/actions/workflows/github-code-scanning/codeql/badge.svg)](https://github.com/Kataglyphis/Kataglyphis-BeschleunigerBallett/actions/workflows/github-code-scanning/codeql)
[![Automatic Dependency Submission](https://github.com/Kataglyphis/Kataglyphis-BeschleunigerBallett/actions/workflows/dependency-graph/auto-submission/badge.svg)](https://github.com/Kataglyphis/Kataglyphis-BeschleunigerBallett/actions/workflows/dependency-graph/auto-submission)
[![TopLang](https://img.shields.io/github/languages/top/Kataglyphis/Kataglyphis-Renderer)]()  
[![Donate](https://img.shields.io/badge/Donate-PayPal-green.svg)](https://www.paypal.com/donate/?hosted_button_id=BX9AVVES2P9LN)
[![Twitter](https://img.shields.io/twitter/follow/Cataglyphis_?style=social)](https://twitter.com/Cataglyphis_)
[![YouTube](https://img.shields.io/youtube/channel/subscribers/UC3LZiH4sZzzaVBCUV8knYeg?style=social)](https://www.youtube.com/channel/UC3LZiH4sZzzaVBCUV8knYeg)

## Overview

Kataglyphis-BeschleunigerBallett is a renderer and graphics-engine playground used to explore modern graphics APIs and the surrounding engineering workflow. The repository combines a C++23-modules Vulkan engine, a companion Rust WebGPU renderer (native + browser), build automation, packaging, testing, documentation, and Rust integration.

## Highlights

- Vulkan renderer (C++23 modules) with forward + deferred rasterization, ray tracing, path tracing, PBR, cascaded shadow maps (consumed by both lighting paths), skybox, volumetric clouds, OBJ loading, and mip mapping
- VMA-backed memory, fence-synced uploads with a persistent staging buffer, a persisted `VkPipelineCache`, and fail-fast Vulkan error handling (exceptions are disabled project-wide)
- Companion Rust WebGPU renderer (`ExternalLib/Kataglyphis-RustProjectTemplate/crates/webgpu_renderer`): glTF 2.0/GLB, PBR + IBL, CSM, SSAO, bloom, skinning, animations, LOD — runs natively and in the browser; shaders exportable to SPIR-V/GLSL for this engine (see `docs/shader-sharing.md`)
- Tooling around CMake presets, CI, code coverage, benchmarking, fuzzing (including a real OBJ-parsing fuzz target), packaging, Sphinx, Doxygen, and Graphviz
- Linux and Windows as the primary development targets

## Repository Layout

| Path | Purpose |
| --- | --- |
| `Src/` | Engine and renderer source code |
| `Resources/` | Shaders and runtime assets |
| `Scripts/Linux/` | Linux build, test, coverage, analysis, and docs helpers |
| `Scripts/Windows/` | Windows build, run, and dependency setup helpers |
| `Test/` | Tests |
| `docs/source/` | Hand-written Sphinx pages |
| `Documents/` | Generated PDF and reference artifacts |
| `ExternalLib/` | Third-party dependencies and submodules |

## Requirements

- C++23
- C17
- CMake 4.1 or newer
- Vulkan SDK 1.4 compatible environment for Vulkan builds
- OpenGL 4.6 capable driver/runtime for OpenGL builds
- Python plus `requirements.txt` for docs and formatting tools
- Optional Rust toolchain for experimental Rust-enabled builds

## Quick Start

### Clone

```bash
git clone --branch develop --recurse-submodules git@github.com:Kataglyphis/Kataglyphis-BeschleunigerBallett.git
cd Kataglyphis-BeschleunigerBallett
```

### Configure and build with CMake presets

```bash
cmake --list-presets
cmake --preset <preset-name>
cmake --build build --config Debug
ctest --test-dir build --output-on-failure
```

For Visual Studio style generators on Windows, add `-C Debug` or `-C Release` to `ctest` as needed.

### Linux helper script

```bash
bash ./Scripts/Linux/cmake-configure-build.sh \
  --preset linux-debug-clang \
  --build-dir build \
  --build-config Debug
```

### Windows helper scripts

Use the build orchestration script when you want one entry point for formatting, configuration, build, and tests. Available configurations: `msvc-debug`, `msvc-release`, `clangcl-debug` (Debug with ASAN/UBSan), `clangcl-tsan`, `clangcl-profile` (RelWithDebInfo with benchmarks), `clangcl-release`.

```powershell
# single configuration
powershell -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows.ps1 -Configurations clangcl-debug

# full sanitizer/profile/release sweep
powershell -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows.ps1 `
  -Configurations "clangcl-debug,clangcl-tsan,clangcl-profile,clangcl-release"
```

Note: sanitizers are Debug-only. `clangcl-debug` enables AddressSanitizer and UBSan by default; `clangcl-tsan` requests ThreadSanitizer, but clang-cl does not support TSan on Windows, so that preset builds a plain Debug binary without sanitizers — use the `linux-debug-tsan-*` presets for real TSan runs.

### Windows container build (Stevedore)

The same builds run fully containerized in the ContainerHub developer image `ghcr.io/kataglyphis/kataglyphis_beschleuniger:winamd64` — this is what CI does. Install [Stevedore](https://github.com/slonopotamus/stevedore) (`winget install stevedore`, then reboot) and run:

```powershell
# defaults to clangcl-debug,clangcl-tsan,clangcl-profile,clangcl-release
powershell -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows-Container.ps1
```

The script uses Stevedore's `docker.exe` (never `nerdctl` — broken DNS/CNI on Windows), prefers process isolation for full CPU count, and bind-mounts the repo to a fresh path. If the repo lives on a Dev Drive whose filters are not allow-listed for containers, it automatically falls back to streaming the sources into the container via tar and streaming the build trees back out; to enable the faster bind mount instead, run once (elevated) `fsutil devdrv setfiltersallowed bindFlt, wcifs` and remount the volume.

Builds are supported against the recorded submodule pins (`git submodule update --checkout --recursive` restores them). The Windows scripts resolve PowerShell modules from the `ExternalLib/Kataglyphis-ContainerHub` submodule when available, with vendored fallbacks in `Scripts/Windows/modules` for modules removed upstream (see `Scripts/Windows/Resolve-BuildModule.ps1` and `AGENTS.md`). When bumping `ExternalLib/FUZZTEST`, keep `ABSL_TAG` in `ExternalLib/CMakeLists.txt` at least as new as the Abseil pin in FuzzTest's `cmake/BuildDependencies.cmake`.

Run helpers after building:

```powershell
& ./Scripts/Windows/run_clangcl_debug.ps1 2>&1 | Tee-Object -FilePath logs/debug/run.log
& ./Scripts/Windows/run_clangcl_release.ps1 2>&1 | Tee-Object -FilePath logs/release/run.log
```

## Documentation

The repository ships two documentation entry points:

- this README for repository-level orientation
- the Sphinx site under `docs/` for getting started, workflow notes, Graphviz output, and optional API reference material

Build the Sphinx HTML docs locally:

```powershell
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\.venv\Scripts\python.exe -m sphinx -M html docs/source docs/build -E
```

If Doxygen XML is available, the Sphinx build automatically includes the generated C++ API reference. If no XML is present, the hand-written docs still build cleanly and the API section stays hidden.

## Tests and Analysis

- `ctest` runs the configured test suite from the active build directory
- `Scripts/Linux/run-ctest.sh` wraps Linux test execution
- `Scripts/Linux/build-coverage-gcovr.sh` and `Scripts/Linux/build-coverage-llvm.sh` generate coverage reports
- `Scripts/Linux/run-perf-suite.sh` runs performance-oriented checks
- `Scripts/Windows/Build-Windows.ps1` can orchestrate formatting, tidy, builds, tests, and packaging

## Packaging

### Linux

Binary packages are generated with CPack. A typical release workflow is:

```bash
bash ./Scripts/Linux/cmake-configure-build.sh \
  --vulkan-setup-script /opt/vulkan/1.4.341.1/setup-env.sh \
  --preset linux-release-clang \
  --build-dir build-release \
  --build-config Release

bash ./Scripts/Linux/cmake-configure-build.sh \
  --vulkan-setup-script /opt/vulkan/1.4.341.1/setup-env.sh \
  --build-dir build-release \
  --skip-configure true \
  --build-target package

cmake -S . -B build-release-appimage \
  --preset linux-release-clang \
  -DCPACK_ENABLE_APPIMAGE=ON
cmake --build build-release-appimage --config Release --target package
```

Artifacts land in the selected build directory. For AppImage builds, `appimagetool` must be available in `PATH`.

### Windows

The Windows release build can produce an MSIX package. For signing, place the PFX certificate at the repository root and provide the password via `MSIX_PFX_PASSWORD` or `MSIX_CERT_PASSWORD`.

## Shaders

Shader include handling is wired through these files:

- `Src/GraphicsEngineVulkan/vulkan_base/ShaderIncludes.hpp`
- `Src/GraphicsEngineVulkan/cmake/CompileShadersToSPV.cmake`

Update both when you add new include-driven shader files.

## Docker and Build Environments

Containerized and reproducible environment details live in [Kataglyphis-ContainerHub](https://github.com/Kataglyphis/Kataglyphis-ContainerHub). On Windows the container runtime is [Stevedore](https://github.com/slonopotamus/stevedore); `Scripts/Windows/Build-Windows-Container.ps1` builds this project inside the prebuilt toolchain image (see the Windows container build section above), and `.github/workflows/Windows.yml` runs the same flow in CI.

## Roadmap

- Keep the renderer and tooling foundations healthy on Linux and Windows
- Expand test, fuzz, and performance coverage
- Continue documenting build, packaging, and API workflows
- Keep generated reference material easy to reproduce locally

All open work is consolidated in [`ROADMAP.md`](ROADMAP.md); unsized ideas
and recurring chores (perf tests, periodic profile/TSan/sync-validation runs)
live in [`BACKLOG.md`](BACKLOG.md).
Renderer-specific plans and status live in `docs/`:
`webgpu-renderer-roadmap.md` (Rust WebGPU renderer),
`cpp-renderer-improvements.md` (C++ engine improvement campaign),
`shader-sharing.md` (sharing shader code between both renderers), and
`webgpu-srgb-audit.md` (color-space audit).

## Contributing

Contributions are welcome. A good change keeps code, docs, and build scripts aligned.

1. Fork the project
2. Create a feature branch
3. Implement and test the change
4. Update documentation when behavior or workflows change
5. Open a pull request

## License

Distributed under the MIT License. See `LICENSE` for more information.

## Third-party Licenses

See the full third-party license overview in [docs/LICENSES-README.md](docs/LICENSES-README.md).

## Contact

Jonas Heinle - [@Cataglyphis_](https://twitter.com/Cataglyphis_) - renderdude@jotrockenmitlocken.de

Project link: [https://github.com/Kataglyphis/Kataglyphis-BeschleunigerBallett](https://github.com/Kataglyphis/Kataglyphis-BeschleunigerBallett)

## Further Reading

- [Official homepage](https://beschleunigerballette.jonasheinle.de/)
- [Kataglyphis-ContainerHub](https://github.com/Kataglyphis/Kataglyphis-ContainerHub)
- [Doxygen PDF reference](Documents/refman.pdf)
- [Sphinx docs source](docs/source)

## Common Issue: Missing Vulkan validation layers

If validation layers are not available, startup can fail with errors like:

```bash
[error] Validation layers requested, but not available!
[error] Failed to create a Vulkan instance!
ERROR: vkGetInstanceProcAddr: Invalid instance
```

On Linux, install the runtime packages first:

```bash
sudo apt install libvulkan1 vulkan-tools vulkan-validationlayers
```

On Windows, Debug builds abort at startup with exit code `-1073740791` (`0xC0000409`) when the validation layers are missing. Install the Vulkan SDK (`winget install VulkanSDK`), or point `VK_LAYER_PATH` at a directory containing `VkLayer_khronos_validation.dll`/`.json` (extractable from the ContainerHub toolchain image). Profile and Release builds run without validation layers.


## Literature 

Some very helpful literature, tutorials, etc. 

* [View Frustum Culling](http://www.lighthouse3d.com/tutorials/view-frustum-culling/geometric-approach-extracting-the-planes/)

OpenGL 
* [learnopengl.com](https://learnopengl.com/)
* [ogldev.org](https://ogldev.org/)
* [Cascaded Shadow Maps](https://ahbejarano.gitbook.io/lwjglgamedev/chapter26)
* [Compute Shader in OpenGL](https://antongerdelan.net/opengl/compute.html)

Clouds
* [pbr-book](https://www.pbr-book.org/)
* [Inigo Quilez](https://iquilezles.org)
* [Shadertoy Horizon Zero Dawn](https://www.shadertoy.com/view/WddSDr)
* [Sebastian Lague](https://m.youtube.com/watch?v=4QOcCGI6xOU&t=97s)
* [Horizon Zero Dawn](http://advances.realtimerendering.com/s2015/The%20Real-time%20Volumetric%20Cloudscapes%20of%20Horizon%20-%20Zero%20Dawn%20-%20ARTR.pdf)
* [Clouds and noise](https://thebookofshaders.com/12/)
* [Shadertoy Clouds using 3D Perlin noise](https://www.shadertoy.com/view/XlKyRw)

Noise
* [Worley noise online demo](https://github.com/Erkaman/glsl-worley)

Vulkan
* [Udemy course by Ben Cook](https://www.udemy.com/share/102M903@JMHgpMsdMW336k2s5Ftz9FMx769wYAEQ7p6GMAPBsFuVUbWRgq7k2uY6qBCG6UWNPQ==/)
* [Vulkan Tutorial](https://vulkan-tutorial.com/)
* [Vulkan Raytracing Tutorial](https://developer.nvidia.com/rtx/raytracing/vkray)
* [Vulkan Tutorial; especially chapter about integrating imgui](https://frguthmann.github.io/posts/vulkan_imgui/)
* [NVidia Raytracing tutorial with Vulkan](https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR/)
* [Blog from Sascha Willems](https://www.saschawillems.de/)

Physically Based Shading
* [Advanced Global Illumination by Dutre, Bala, Bekaert](https://www.oreilly.com/library/view/advanced-global-illumination/9781439864951/)
* [The Bible: PBR book](https://pbr-book.org/3ed-2018/Reflection_Models/Microfacet_Models)
* [Real shading in Unreal engine 4](https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf)
* [Physically Based Shading at Disney](https://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf)
* [RealTimeRendering](https://www.realtimerendering.com/)
* [Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs](https://hal.inria.fr/hal-01024289/)
* [Sampling the GGX Distribution of Visible Normals](https://pdfs.semanticscholar.org/63bc/928467d760605cdbf77a25bb7c3ad957e40e.pdf)

Path tracing
* [NVIDIA Path tracing Tutorial](https://github.com/nvpro-samples/vk_mini_path_tracer/blob/main/vk_mini_path_tracer/main.cpp)

Docker
* [Vulkan Minimal Docker setup](https://github.com/j3soon/docker-vulkan-runtime)
* [scoop](https://scoop.sh/#/apps)
* [Docker container windows GPU](https://learn.microsoft.com/de-de/virtualization/windowscontainers/deploy-containers/gpu-acceleration)
* [Docker windows](https://hub.docker.com/r/microsoft/windows)

## Common issues

### WSL2 — Vulkan (NVIDIA)

Wenn in WSL2 nach dem Start der Anwendung nur "llvmpipe" (Mesa Software‑Renderer) angezeigt wird, siehe die Troubleshooting‑Seite: [docs/source/wsl2_vulkan.rst](docs/source/wsl2_vulkan.rst) für Schritt‑für‑Schritt‑Anweisungen zur Installation des NVIDIA‑ICD und zum Neustart von WSL (`wsl --shutdown`).


  * Problem: 
    If **__Validation Layers__** could not be found:
    ```bash
    A value given directly by extern c function 322
    [XXXX-XX-XX 10:30:40.877] [error] Validation layers requested, but not available!
    [XXXX-XX-XX 10:30:40.879] [error] Failed to create a Vulkan instance!
    [XXXX-XX-XX 10:30:40.880] [error] Validation layers requested, but not available!
    [XXXX-XX-XX 10:30:40.882] [error] Failed to create a Vulkan instance!
    ERROR:             vkGetInstanceProcAddr: Invalid instance [VUID-vkGetInstanceProcAddr-instance-parameter]
    ```
    Solution for linux:
    ```bash
    sudo apt install libvulkan1 vulkan-tools vulkan-validationlayers
    ```
    Otherwise you would have to install them via sdk.

