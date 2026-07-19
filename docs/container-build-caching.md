# Container Build Caching & Incremental Builds

How builds in *this repo* stay fast. `AGENTS.md` links here rather than
restating it.

> **The general findings are not repeated here.** The reusable-container
> pattern, its safety rails, why sccache cannot cache a C++23 modules build,
> why a named volume cannot be a CMake build directory, the Windows path-limit
> trap that silently truncates tar transfers, the Dev Drive bind-mount
> restriction and the `docker exec` entrypoint bypass all live in
> [`ContainerHub / windows-container-build-performance.md`](../ExternalLib/Kataglyphis-ContainerHub/docs/windows-container-build-performance.md).
> They apply to any project built in that image. This page covers only what is
> specific to this repository.

## Measured here (2026-07-19, `clangcl-debug`)

| Scenario | Build step | Wall clock |
| --- | --- | --- |
| Fresh container (`-FreshContainer`, host tree seeded) | 327.9 s | 364 s |
| Incremental, a few files changed | ~14 s | ~50 s |
| **No source changes** | **9.6 s** | **44 s** |
| Previously: fresh container every build | — | 352–484 s |

The wall clock exceeds the build step because sources stream in and artifacts
stream back out on every run; that transport cost is now the dominant term,
not compilation.

Verified against these numbers: 21/21 commit tests and 18/18 Pester tests pass
on the incrementally built binaries.

## What is actually cached

Not compiler output — **build state**. One container (`bb-build-persistent`)
is reused, so ninja's dependency graph, the C++23 module BMIs and the object
files stay where they are. sccache is wired to a persistent volume but
contributes nothing here (0.00 % hit rate on a modules build — see
ContainerHub for the measurement); do not expect a speedup from it.

## Repo-specific wiring

- **`KATAGLYPHIS_KEEP_BUILD_ROOT=1`** is passed into the container.
  `Invoke-CmakeConfigureAndBuild` otherwise wipes the build root before
  configuring (`Remove-BuildRootSafe`), which would defeat the entire point.
  Host builds are unaffected — nothing sets the variable there.
- **The `cargo/` subtree is excluded in both directions.** This repo bridges
  Rust via corrosion/cxx, and the generated cxxbridge paths are what exceed
  the Windows path limit. One such failure aborts the *whole* tar transfer, so
  excluding it is what turned a partial transfer into a working one. Cargo
  artifacts rebuild cheaply.
- **Outbound copies only what the host runs** — executables, debug info,
  `compile_commands.json`, logs. Selection is by *exclusion*, because `tar`
  does not expand globs: an earlier glob-based version reported success while
  copying nothing, and only looked correct because stale artifacts were
  already on the host.

## Delivery verification

A green build is not proof that anything was produced or delivered. Both
halves have failed silently here: a build was cut off before linking and still
looked successful, and the outbound transfer once copied nothing at all.

`Build-Windows-Container.ps1` therefore compares the executables present in
the container against those that reached the host, and **fails the build** if
the container produced none, or if any of them did not arrive. It checks
existence rather than timestamps on purpose — on a no-change build ninja does
not relink, so the executables are legitimately older than the run.

## Gotchas

- The script takes `-Configurations` (comma-separated), **not** `-Preset`.
  Passing the wrong parameter silently builds all four configurations.
- Host `cmake` (3.29) cannot read this repo's `CMakePresets.json`
  (`version: 10`); only the container's newer CMake can.
- **A file deleted on the host keeps building in the container** — sources are
  overwritten in place, never pruned. Use `-FreshContainer` after deleting
  files. Measured reproduction and the proposed fix: `BACKLOG.md`.
- Reset an incremental build if it ever behaves strangely:
  `docker rm -f bb-build-persistent`, or
  `Remove-Item -Recurse -Force build-clangcl-debug` for a cold but clean host
  tree.
