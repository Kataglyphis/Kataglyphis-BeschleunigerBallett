# Project Overview

Kataglyphis-BeschleunigerBallett is a renderer and graphics-engine playground for experimenting with Vulkan, OpenGL, modern CMake, testing, packaging, and optional Rust integration. This documentation focuses on the build and maintenance workflow around the engine, not only on the rendering features themselves.

## What is in Scope?

- Vulkan rendering with rasterization, ray tracing, path tracing, PBR, OBJ loading, and mip mapping
- OpenGL rendering with lights, shadow mapping, clouds, compute shaders, skyboxes, and PBR
- Tooling around CMake presets, CI, coverage, benchmarks, fuzzing, packaging, Sphinx, Doxygen, and Graphviz
- Linux and Windows as the primary development platforms

## Repository Map

| Path | Purpose |
| --- | --- |
| `Src/` | Engine and renderer source code |
| `Resources/` | Shaders and runtime assets |
| `Scripts/Linux/` | Linux build, test, and docs automation |
| `Scripts/Windows/` | Windows build, run, and dependency helpers |
| `Test/` | Tests |
| `docs/source/` | Hand-written Sphinx pages |
| `Documents/` | Generated PDF and other reference artifacts |
| `ExternalLib/` | Third-party dependencies and submodules |

## Supported Environments

- C++23 and C17 toolchains
- CMake 4.1 or newer
- Vulkan SDK 1.4 compatible environment for Vulkan builds
- OpenGL 4.6 capable runtime for the OpenGL renderer
- Optional Rust toolchain for the experimental Rust-enabled build path

## Where to Go Next

- Read [Getting Started](getting_started.md) for clone, configure, build, and run instructions
- Read [Documentation Workflow](documentation_workflow.md) for Sphinx, Doxygen, and Graphviz details
- Open [Graphviz Include Graphs](graphviz_files.rst) for the stable entry page for generated diagrams
- Open [API Reference](api/library_root.rst) for the C++ API landing page

If Doxygen XML has already been generated, the API page is automatically replaced with the generated Exhale output.