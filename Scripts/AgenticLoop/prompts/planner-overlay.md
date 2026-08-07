# Project: Kataglyphis-BeschleunigerBallett

The role, the critical rules, the refactor focus areas and the task-entry
template come from ContainerHub's shared Planner prompt, which the loop prepends
to this file automatically
(`ExternalLib/Kataglyphis-ContainerHub/shared/agentic-loop/system-prompts/planner.md`).
Everything below is what is specific to **this** repo.

This is a Vulkan graphics engine: C++23/C17, CMake presets, optional Rust WebGPU
sibling renderer.

## BACKLOG sections

Place tasks under the appropriate existing heading — `## C++ Vulkan engine`,
`## Rust WebGPU renderer`, and the others already in the file.

## Which build to specify

- `clangcl-debug` — fast iteration (ASAN + UBSan). The default for most tasks.
- `clangcl-profile` — benchmarks (`perfTestSuite.exe`).
- `clangcl-release` — packaging.

The command to put in a task's **Build:** field:

```
pwsh -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows-Container.ps1 -Configurations clangcl-debug
```

## Conventions a task must never violate

Read `AGENTS.md`. In particular, do not propose enabling exceptions
(the project compiles with `-fno-exceptions` / `/EHs-` and
`VULKAN_HPP_NO_EXCEPTIONS`), hand-rolling Vulkan pipelines instead of
`PipelineBuilder`, or bypassing VMA for buffer/image memory.

## Docs to reference rather than restate

`docs/cpp-renderer-improvements.md` for engine change history,
`docs/shader-build-pipeline.md` for anything shader-related,
`docs/gpu-golden-testing.md` for render-output verification.
