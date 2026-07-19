# Container Build Caching & Incremental Builds

How builds in this repo stay fast, and the measurements behind it.
`AGENTS.md` links here rather than restating it.

> **General Windows-container findings live in ContainerHub**, where they are
> reusable across projects:
> [`ExternalLib/Kataglyphis-ContainerHub/docs/windows-container-build-performance.md`](../ExternalLib/Kataglyphis-ContainerHub/docs/windows-container-build-performance.md)
> — the reusable-container pattern and its safety rails, why sccache cannot
> cache a C++23 modules build, why a named volume cannot be a CMake build
> directory, and the Windows path-limit trap that silently truncates tar
> transfers. This page keeps only what is specific to *this* repository.

## Summary

| Approach | Result |
| --- | --- |
| **Reusable build container (in use)** | ✅ **48 s** no-change, **63 s** one-header change |
| Streaming the build tree in/out | 🟡 ~230 s — worked, but moved ~17 GB per build |
| sccache persistent volume | ❌ 0.00 % hit rate — cache stays empty |
| Named volume for the build directory | ❌ CMake cannot configure inside it |

**Measured 2026-07-19 (clangcl-debug):**

| Scenario | Time |
| --- | --- |
| Cold (fresh container, tree seeded) | 137 s |
| No source changes | **48 s** |
| One header touched | **63 s** |
| Previously (fresh container every build) | 352–484 s |

Verified: 16/16 tests pass against the incrementally built binaries and host
executable timestamps match the build end (no stale artifacts).

## What is actually cached

Not compiler output — **build state**. One container (`bb-build-persistent`)
is reused across builds, so ninja's dependency graph, C++23 module BMIs and
object files stay exactly where they were. Nothing needs to be transferred or
recomputed:

- **Inbound:** sources only. `tar` preserves mtimes, so ninja sees just the
  files that really changed.
- **Outbound:** only what the host runs - `*.exe`, `*.dll`, `*.pdb`,
  `compile_commands.json`, logs. The host copy is no longer the incremental
  seed, so there is no reason to copy ~8.5 GB back. This also removed the
  `Artifact extraction failed` warnings, which came from deep
  `cargo/cxxbridge` paths in that bulk copy.

### Safety rails

- **Image change is detected** (`docker inspect` of the container image ID vs
  the referenced image) and the container is recreated. You cannot silently
  build against a stale toolchain.
- **`-FreshContainer`** discards the container and starts clean. Needed
  because sources are overwritten in place and never pruned: a file DELETED
  on the host still exists inside the container until it is recreated.
- Reset by hand: `docker rm -f bb-build-persistent`.

## The transport

`Scripts/Windows/Build-Windows-Container.ps1` prefers a bind mount, but Dev
Drive hosts reject the filesystem minifilter, so it falls back to a
**tar-pipe**: sources are streamed into a fresh container, built there, and
build trees + logs are streamed back out.

## Previous approach: streaming the build tree back in

(Superseded by container reuse, kept because the mechanics still apply when a
container has to be recreated.)

The host already holds the previous build tree (it is streamed *out* after
every build), so it is streamed *in* as well. ninja then rebuilds only what
changed instead of ~690 objects. The in-container path is always
`C:\ws\build-<config>`, so CMake's baked-in absolute paths stay valid.

Two details make it work:

- **`KATAGLYPHIS_KEEP_BUILD_ROOT=1`** is passed to the container.
  `Invoke-CmakeConfigureAndBuild` normally wipes the build root before
  configuring (`Remove-BuildRootSafe`), which would defeat the whole point.
  Host builds are unaffected — nothing sets the variable there.
- **The `cargo/` subtree is excluded in both directions.** Rust cxxbridge
  output nests deep enough to exceed the Windows path limit inside the
  container:

  ```
  ...cargo/.../out/cxxbridge/include/.../native_only.rs:
    Can't create '\\?\C:\ws\...': Invalid argument
  tar: Error exit delayed from previous errors
  ```

  One such failure aborts the whole transfer, so excluding it is what turned a
  partial transfer into a working one. Cargo artifacts rebuild cheaply.

**Measured (2026-07-19, clangcl-debug, no source changes):** ~229 s, versus
352–484 s for cold builds. Verified that host executables are current
afterwards (timestamps match the build end) and that 16/16 tests pass.

### Remaining cost

~17 GB moves per build (8.5 GB in, 8.5 GB out), which now dominates. Ideas,
untested:

- **Keep one long-lived build container** and re-sync only sources into it.
  The build tree never leaves the container, so both transfers disappear.
  Needs container lifecycle management (reuse if present, recreate on image
  change) and a way to pull executables out for host-side test runs.
- Stream only executables + logs back out, keeping the full tree in the
  container. Conflicts with the current design, where the *host* copy is what
  seeds the next build's incremental state.

### Known warning

The outbound step can still report `Artifact extraction failed (exit 1)`.
Executables and logs arrive regardless (verified by timestamp and by running
the tests), so it is noisy rather than harmful — but it deserves a proper
fix, since a genuinely failed extraction would leave stale binaries on the
host, and stale artifacts have already cost this project hours (see
[`shader-build-pipeline.md`](shader-build-pipeline.md)).

## What does not work: sccache

`sccache` runs on every compile and its cache location can be redirected to a
persistent named volume (`kataglyphis-sccache` at `C:\sccache`,
`SCCACHE_DIR`), which the logs confirm it uses. It still does not help:

```
Compile requests            907
Cache hits rate            0.00 %
Cache misses                780
Cache size                    0 bytes
```

Zero stored bytes on a **byte-identical tree** — this is a C++23 **modules**
build, and sccache cannot hash module compilations reliably, so nothing is
ever cached. The volume is harmless and remains wired up in case sccache
gains module support; do not expect a speedup from it today.

## What does not work: named volume for the build directory

Mounting `kataglyphis-build-<config>` at `C:\ws\build-<config>` looked like
the cleaner alternative. CMake fails inside it:

```
CMakeTestCXXCompiler.cmake:71 (message)
  ninja: error: loading 'build.ninja': The system cannot find the file specified.
```

This reproduces with a **freshly created** volume, so it is not stale state —
Windows container volumes are filter-driver backed and do not behave like an
ordinary directory for these operations. The plumbing was reverted.

## Gotchas

- The script takes `-Configurations` (comma-separated), **not** `-Preset`.
  Passing the wrong parameter silently builds all four configurations.
- Containers occasionally survive a successful build (`wcifs teardown lock`).
  A lingering container makes it look like a build is still running — compare
  the newest `logs/windows/build-summary-*.json` timestamp against the
  container start time before assuming.
- Host `cmake` (3.29) cannot read this repo's `CMakePresets.json`
  (`version: 10`); only the container's newer CMake can.
- Reset an incremental build if it ever behaves strangely:
  `Remove-Item -Recurse -Force build-clangcl-debug` — the next build is cold
  but clean.
