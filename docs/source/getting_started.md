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

For Windows, use the orchestration script if you want configuration, build, formatting, and tests from one entry point. The available configurations are `msvc-debug`, `msvc-release`, `clangcl-debug` (Debug with ASAN/UBSan), `clangcl-tsan`, `clangcl-profile` (RelWithDebInfo with benchmarks), and `clangcl-release`:

```powershell
# single configuration
powershell -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows.ps1 -Configurations clangcl-debug

# full sanitizer/profile/release sweep
powershell -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows.ps1 `
  -Configurations "clangcl-debug,clangcl-tsan,clangcl-profile,clangcl-release"
```

Sanitizers apply to Debug builds only. `clangcl-debug` enables AddressSanitizer and UBSan by default. `clangcl-tsan` requests ThreadSanitizer, but clang-cl does not support TSan on Windows targets, so it builds a plain Debug binary without sanitizers; use `linux-debug-tsan-clang` or `linux-debug-tsan-GNU` for real TSan runs.

After building, these run helpers are available:

```powershell
& ./Scripts/Windows/run_clangcl_debug.ps1 2>&1 | Tee-Object -FilePath logs/debug/run.log
& ./Scripts/Windows/run_clangcl_release.ps1 2>&1 | Tee-Object -FilePath logs/release/run.log
```

If build dependencies are missing on the host, prefer the containerized workflow below — the toolchain image ships everything (clang-cl, CMake, Ninja, Vulkan SDK, Rust, sccache).

## Windows Container Workflow (Stevedore)

The Windows builds also run fully containerized in the ContainerHub developer image `ghcr.io/kataglyphis/kataglyphis_beschleuniger:winamd64`, exactly like CI (`.github/workflows/Windows.yml`). Install [Stevedore](https://github.com/slonopotamus/stevedore) with `winget install stevedore` and reboot, then:

```powershell
# defaults to clangcl-debug,clangcl-tsan,clangcl-profile,clangcl-release
powershell -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows-Container.ps1
```

Details worth knowing:

- The script always uses Stevedore's `docker.exe`; `nerdctl` is not usable for builds or runs on Windows.
- Process isolation is the default so the container sees all host CPUs.
- The repo is bind-mounted to a fresh container path. If the repo lives on a Dev Drive that has not allow-listed the container filesystem filters, bind mounts fail; the script then falls back to streaming the sources into the container via tar and streaming the resulting build trees and logs back into the working tree. To enable the faster bind-mount path on a Dev Drive, run once from an elevated prompt `fsutil devdrv setfiltersallowed bindFlt, wcifs` and remount the volume.
- Builds are supported against the recorded submodule pins; restore them with `git submodule update --checkout --recursive`. In particular the Windows scripts import PowerShell modules from the `ExternalLib/Kataglyphis-ContainerHub` submodule (ContainerHub `main` removed that module layout), and a drifted `ExternalLib/FUZZTEST` breaks CMake configure (see `AGENTS.md`).

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

On Windows this shows up as Debug builds aborting at startup with exit code `-1073740791` (`0xC0000409`) right after logging `Validation layers requested, but not available!`. Install the Vulkan SDK (`winget install VulkanSDK`), or set `VK_LAYER_PATH` to a directory containing `VkLayer_khronos_validation.dll`/`.json`. Profile and Release builds run without validation layers. When running the AddressSanitizer Debug build manually, keep the `ASAN_OPTIONS` `log_path` relative (an absolute `C:\...` path breaks ASAN option parsing at the drive-letter colon) — the `run_clangcl_debug.ps1` helper already handles this.