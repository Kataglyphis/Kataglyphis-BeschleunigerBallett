# Getting Started

## Prerequisites

Make sure these tools are available before you build the project:

- a C++23 capable compiler
- a C17 capable compiler
- CMake 4.1 or newer
- a Vulkan SDK installation for Vulkan-enabled builds
- an OpenGL 4.6 capable driver/runtime for the OpenGL renderer
- Python plus the packages from `requirements.txt` for docs and formatting tasks
- optionally Rust if you want to enable the experimental Rust path

## Clone the Repository

```bash
git clone --branch develop --recurse-submodules git@github.com:Kataglyphis/Kataglyphis-BeschleunigerBallett.git
cd Kataglyphis-BeschleunigerBallett
```

## Configure with CMake Presets

The repository already ships a `CMakePresets.json`. Start there instead of creating ad-hoc build commands.

```bash
cmake --list-presets
cmake --preset <preset-name>
cmake --build build --config Debug
ctest --test-dir build --output-on-failure
```

For Visual Studio style generators on Windows, add `-C Debug` or `-C Release` to `ctest`.

## Linux Workflow

For Linux, the helper script under `Scripts/Linux/` wraps the common configure-and-build path:

```bash
bash ./Scripts/Linux/cmake-configure-build.sh \
  --preset linux-debug-clang \
  --build-dir build \
  --build-config Debug
```

Useful adjacent scripts:

- `Scripts/Linux/run-ctest.sh`
- `Scripts/Linux/build-coverage-gcovr.sh`
- `Scripts/Linux/build-coverage-llvm.sh`
- `Scripts/Linux/run-perf-suite.sh`

## Windows Workflow

For Windows, use the orchestration script if you want configuration, build, formatting, and tests from one entry point:

```powershell
powershell -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows.ps1 -Configurations clang-debug
```

After building, these run helpers are available:

```powershell
& ./Scripts/Windows/run_clangcl_debug.ps1 2>&1 | Tee-Object -FilePath logs/debug/run.log
& ./Scripts/Windows/run_clangcl_release.ps1 2>&1 | Tee-Object -FilePath logs/release/run.log
```

If dependencies are missing on Windows, start with `Scripts/Windows/setup-dependencies.ps1`.

## Packaging

### Linux release packages

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
```

For AppImage packaging, enable `CPACK_ENABLE_APPIMAGE=ON` on a separate release build tree and ensure `appimagetool` is on `PATH`.

### Windows MSIX

The Windows release workflow can produce an MSIX package. If signing is enabled, provide the certificate password through `MSIX_PFX_PASSWORD` or `MSIX_CERT_PASSWORD`.

## Shader Include Workflow

If you introduce new shader include files, keep these paths aligned:

- `Src/GraphicsEngineVulkan/vulkan_base/ShaderIncludes.hpp`
- `Src/GraphicsEngineVulkan/cmake/CompileShadersToSPV.cmake`

## Troubleshooting

If Vulkan validation layers are missing, install the validation packages from your operating system or Vulkan SDK before retrying the build or run workflow.