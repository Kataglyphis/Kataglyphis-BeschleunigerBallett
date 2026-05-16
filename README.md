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

Kataglyphis-BeschleunigerBallett is a renderer and graphics-engine playground used to explore modern graphics APIs and the surrounding engineering workflow. The repository combines Vulkan and OpenGL rendering work with build automation, packaging, testing, documentation, and optional Rust integration.

## Highlights

- Vulkan renderer with rasterization, ray tracing, path tracing, PBR, OBJ loading, and mip mapping
- OpenGL renderer with dynamic lights, multiple shadow techniques, clouds, compute shaders, skyboxes, and PBR
- Tooling around CMake presets, CI, code coverage, benchmarking, fuzzing, packaging, Sphinx, Doxygen, and Graphviz
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

Use the build orchestration script when you want one entry point for formatting, configuration, build, and tests:

```powershell
powershell -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows.ps1 -Configurations clang-debug
```

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

Containerized and reproducible environment details live in [Kataglyphis-ContainerHub](https://github.com/Kataglyphis/Kataglyphis-ContainerHub).

## Roadmap

- Keep the renderer and tooling foundations healthy on Linux and Windows
- Expand test, fuzz, and performance coverage
- Continue documenting build, packaging, and API workflows
- Keep generated reference material easy to reproduce locally

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

Otherwise install the validation layers through the Vulkan SDK used on your system.

