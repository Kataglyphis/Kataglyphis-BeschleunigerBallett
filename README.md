<div align="center">
  <a href="https://jonasheinle.de">
    <img src="images/logo.png" alt="logo" width="200" />
  </a>

  <h1>Kataglyphis-BeschleunigerBallett</h1>

  <h4>A modern graphics engine built on top of Vulkan+OpenGL. Serves also as playground 
for learning various best practices in Graphic APIs, CMake, Rust, Modern C++ ... 🌋🌋🌋 </h4>
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

<!-- TABLE OF CONTENTS -->
## Table of Contents

- [About The Project](#about-the-project)
  - [Built With](#built-with)
  - [Key Features](#key-features)
  - [Dependencies](#dependencies)
  - [Useful tools](#useful-tools-you-might-also-considering-)
  - [Benchmarking](#benchmarking)
  - [VSCode Extensions](#vscode-extensions)
- [Getting Started](#getting-started)
  - [Specific version requirements](#specific-version-requirements)
  - [Installation](#installation)
- [Shaders](#shaders)
- [Tests](#tests)
- [Docker](#docker)
  - [Linux](#linux)
  - [Windows](#windows)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)
- [Third-party Licenses](#third-party-licenses)
- [Contact](#contact)
- [Acknowledgements](#acknowledgements)
- [Literature](#literature)
- [Common issues](#common-issues)

<!-- ABOUT THE PROJECT -->
## About The Project

<div align="center">
  <br>
  <a href="https://jonasheinle.de"><img src="images/VulkanEngine/Screenshot1.png" alt="VulkanEngine" width="400"></a>
  <a href="https://jonasheinle.de"><img src="images/VulkanEngine/Screenshot2.png" alt="VulkanEngine" width="400"></a>
  <a href="https://jonasheinle.de"><img src="images/VulkanEngine/Screenshot3.png" alt="VulkanEngine" width="700"></a>
</div>

<div align="center">
  <br>
  <a href="https://jonasheinle.de"><img src="images/OpenGLEngine/Screenshot1.png" alt="VulkanEngine" width="600"></a>
  <a href="https://jonasheinle.de"><img src="images/OpenGLEngine/Screenshot2.png" alt="VulkanEngine" width="600"></a>
  <br>
  <a href="https://jonasheinle.de"><img src="images/OpenGLEngine/Screenshot3.png" alt="VulkanEngine" width="200"></a>
  <a href="https://jonasheinle.de"><img src="images/OpenGLEngine/Screenshot4.png" alt="VulkanEngine" width="200"></a>
</div>

This project provides me a solid Vulkan/OpenGL renderer starting point for implementing 
modern established rendering techniques and getting quickly started in own research topics.  
As this project evolved it gained additional functionality:

* collecting/using [CMake best practices](https://github.com/Kataglyphis/Kataglyphis-CMakeTemplate)
* collecting/using C++ best practices and testing new lang features :blush:
* collecting experience in fuzzy/benchmark testing in C++
* collecting experience in integrating :love_letter: Rust :love_letter: code in Cmake projects

I frequently test under Linux and Windows.  
For more information regarding the build environment refer to my 
[Kataglyphis-ContainerHub](https://github.com/Kataglyphis/Kataglyphis-ContainerHub) repository.  

### Key Features

<div align="center">


|            Category           |           Feature                             |  Implement Status  |
|-------------------------------|-----------------------------------------------|:------------------:|
|  **Vulkan Render agnostic**   | Rasterizer                                    |         ✔️         |
|                               | Raytracing                                    |         ✔️         |
|                               | Path tracing                                  |         ✔️         |
|                               | PBR support (UE4, Disney, etc.)               |         ✔️         |
|                               | .obj Model loading                            |         ✔️         |
|                               | Mip Mapping                                   |         ✔️         |
|  **OpenGL Render agnostic**   |                                               |                    |
|                               | Directional Lights                            |         ✔️         |
|                               | Point Lights                                  |         ✔️         |
|                               | Spot Lights                                   |         ✔️         |
|                               | Directional Shadow Mapping                    |         ✔️         |
|                               | Omni-Directional Shadow Mapping               |         ✔️         |
|                               | Cascaded Shadow Mapping                       |         ✔️         |
|                               | Cloud system                                  |         ✔️         |
|                               | 3D-worley noise generation                    |         ✔️         |
|                               | .obj Model loading                            |         ✔️         |
|                               | PBR support (UE4,disney,phong, etc.)          |         ✔️         |
|                               | Support for `#include` directives in shaders. |         ✔️         |
|                               | Sky box                                       |         ✔️         |
|                               | Supporting compute shader                     |         ✔️         |
|                               | On the fly 3D worley/perlin noise creation    |         ✔️         |
|      **C++/CMake agnostic**   | Code coverage for Clang                       |         ✔️         |
|                               | Advanced unit testing                         |         🔶         |
|                               | Advanced performance testing                  |         🔶         |
|                               | Advanced fuzz testing                         |         🔶         |

</div>

**Legend:**
- ✔️ - completed  
- 🔶 - in progress  
- ❌ - not started


### Dependencies

* [Vulkan 1.4](https://www.vulkan.org/)
* [OpenGL 4.6](https://www.opengl.org//)
* [GLAD](https://glad.dav1d.de/)
* [glm](https://github.com/g-truc/glm)
* [glfw](https://www.glfw.org/)
* [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader)
* [stb](https://github.com/nothings/stb)
* [vma](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
* [tinygltf](https://github.com/syoyo/tinygltf)
* [gtest](https://github.com/google/googletest)
* [gbenchmark](https://github.com/google/benchmark)
* [google fuzztest](https://github.com/google/fuzztest)
* [cmake](https://cmake.org/)
* [gsl](https://github.com/Microsoft/GSL)
* [nlohmann_json](https://github.com/nlohmann/json)
* [SPDLOG](https://github.com/gabime/spdlog)

##### Optional
* [Rust](https://www.rust-lang.org/)
* [corrision-rs](https://github.com/corrosion-rs/corrosion)
* [cxx](https://cxx.rs/)
* [uv](https://github.com/astral-sh/uv)

### Useful tools (you might also considering :) )

* [NSIS](https://nsis.sourceforge.io/Main_Page)
* [doxygen](https://www.doxygen.nl/index.html)
* [cppcheck](https://cppcheck.sourceforge.io/)
* [renderdoc](https://renderdoc.org/)
* [nsightgraphics](https://developer.nvidia.com/nsight-graphics)
* [valgrind](https://valgrind.org/)
* [clangtidy](https://github.com/llvm/llvm-project)
* [visualstudio](https://visualstudio.microsoft.com/de/)
* [ClangPowerTools](https://www.clangpowertools.com/)
* [Codecov](https://app.codecov.io/gh)
* [Ccache](https://ccache.dev/)
* [Sccache](https://github.com/mozilla/sccache)

#### Benchmarking
* [gperftools](https://github.com/gperftools/gperftools)

### VSCode Extensions
* [CMake format](https://github.com/cheshirekow/cmake_format)
* [CMake tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)
* [CppTools](https://github.com/microsoft/vscode-cpptools)

<!-- GETTING STARTED -->
## Getting Started

### Specific version requirements

**C++23** or higher required.<br />
**C17** or higher required.<br />
**CMake 4.2.0** or higher required.<br />

### Installation

> **__NOTE:__**
> On Windows use Git Bash


1. Clone the repo
   ```bash
   git clone --branch develop --recurse-submodules git@github.com:Kataglyphis/Kataglyphis-BeschleunigerBallett.git
   ```
2. Use the scripts (in the `Scripts` folder for installing dependencies on your system) 
3. Then build your solution with [CMAKE] (https://cmake.org/) <br />
  You can follow my steps from my [CMake best practices](https://github.com/Kataglyphis/Kataglyphis-CMakeTemplate) repo.  
  For now the features in Rust are experimental. If you want to use them install
  Rust and set `RUST_FEATURES=ON` on your CMake build.

  Alternatively you can use the build scripts I use for my standard configuration: <br/>
  * [`buildEngine.sh`] 
  * [`buildEngine.bat`]
  ```sh
  $ {WORKING_DIR}/GraphicsEngineVulkan/buildEngine[.sh/.bat]
  ```

### Packaging

On Linux, binary packages are generated with CPack (`TGZ` and `DEB`).
Use this repeatable workflow after a clean checkout or after deleting build folders:

1. Configure and build in `Release` mode
2. Generate release packages (`TGZ`, `DEB`)
3. Generate AppImage packages as standard packaging step

```sh
# 1) Release configure + build
bash ./Scripts/Linux/cmake-configure-build.sh \
  --vulkan-setup-script /opt/vulkan/1.4.341.1/setup-env.sh \
  --preset linux-release-clang \
  --build-dir build-release \
  --build-config Release

# 2) CPack package target (TGZ + DEB)
bash ./Scripts/Linux/cmake-configure-build.sh \
  --vulkan-setup-script /opt/vulkan/1.4.341.1/setup-env.sh \
  --build-dir build-release \
  --skip-configure true \
  --build-target package

# 3) Standard AppImage packaging
cmake -S . -B build-release-appimage \
  --preset linux-release-clang \
  -DCPACK_ENABLE_APPIMAGE=ON
cmake --build build-release-appimage --config Release --target package
```

Generated artifacts are written to the selected build folder (for example `*.tar.gz`, `*.deb`, and AppImage artifacts).
For AppImage builds, `appimagetool` must be available in your `PATH`.

Windows MSIX signing
--------------------
When the Windows build produces an MSIX package the build script can sign it using a PFX certificate located at the repository root. Provide the certificate password via environment variable `MSIX_PFX_PASSWORD`. The script also accepts `MSIX_CERT_PASSWORD` as a fallback for CI environments that use that name for the secret.

Set the secret in your CI (recommended) or export it in your environment before running the Windows build script. Example (GitHub Actions):

  - name: Build Windows
    env:
      MSIX_CERT_PASSWORD: ${{ secrets.MSIX_CERT_PASSWORD }}

### Running the program after a release build

To run the program after a release build on Windows and log its output, use the following PowerShell command:

```powershell
& ./Scripts/Windows/run_clangcl_release.ps1 2>&1 | Tee-Object -FilePath logs/release/run.log
```

### Running the program after a debug build

To run the program after a debug build on Windows and log its output, use the following PowerShell command:

```powershell
& ./Scripts/Windows/run_clangcl_debug.ps1 2>&1 | Tee-Object -FilePath logs/debug/run.log
```

# Shaders
I provide two ways for compiling shaders with. Hence if you want to add new
files as `#include` in your shaders you have to modify the files: (should be self-explanatory)<br/>
* [`include/vulkan_base/ShaderIncludes.hpp`] 
* [`cmake/CompileShadersToSPV.cmake`]

appropriately.</br>


# Tests
I follow the test setup as descriped in: [CMake best practices](https://github.com/Kataglyphis/Kataglyphis-CMakeTemplate) 

# Docker

You can find all details in my [Kataglyphis-ContainerHub](https://github.com/Kataglyphis/Kataglyphis-ContainerHub) repository.  

## Linux

If you want to run it on NVIDIA GPUs you will have to  
install the [NVIDIA Container Toolkit](Kataglyphis-BeschleunigerBallett)  
before you proceed with the next steps.

## Windows

> **__NOTE:__** Pls for GPU accelerated Windows Docker
> have a look [here](https://learn.microsoft.com/en-us/virtualization/windowscontainers/deploy-containers/gpu-acceleration)

<!-- ROADMAP -->
# Roadmap
Watch the refman generated by doxygen. <br/>
* [Watch it here](Documents/refman.pdf)

<!-- CONTRIBUTING -->
## Contributing

Contributions are what make the open source community such an amazing place to be learn, inspire, and create. Any contributions you make are **greatly appreciated**.

1. Fork the Project
2. Create your Feature Branch (`git checkout -b feature/AmazingFeature`)
3. Commit your Changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the Branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request


<!-- LICENSE -->
## License

Distributed under the MIT-License. See `LICENSE` for more information.

## Third-party Licenses

See the full third-party license overview in the docs: [docs/LICENSES-README.md](docs/LICENSES-README.md)


<!-- CONTACT -->
## Contact

Jonas Heinle - [@Cataglyphis_](https://twitter.com/Cataglyphis_) - renderdude@jotrockenmitlocken.de

Project Link: [https://github.com/Kataglyphis/GraphicsEngineVulkan](https://github.com/Kataglyphis/GraphicsEngineVulkan)



<!-- ACKNOWLEDGEMENTS -->
## Acknowledgements

You will find important links to information in the code.
But here in general some good sources of information:

Thanks for free 3D Models: 
* [Morgan McGuire, Computer Graphics Archive, July 2017 (https://casual-effects.com/data)](http://casual-effects.com/data/)

* [Viking room](https://sketchfab.com/3d-models/viking-room-a49f1b8e4f5c4ecf9e1fe7d81915ad38)

* [Loading Screen Image](https://www.golem.de/news/raumfahrt-spacex-macht-sicherheitstest-bei-hoechster-belastung-2001-146124.html)


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

