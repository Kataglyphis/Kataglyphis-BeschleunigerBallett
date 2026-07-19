# Container Build Caching

Why Windows container builds took ~6 minutes every time, and what now makes
them faster. `AGENTS.md` links here rather than restating it.

## The transport

`Scripts/Windows/Build-Windows-Container.ps1` prefers a bind mount, but Dev
Drive hosts reject the filesystem minifilter, so it falls back to a
**tar-pipe**: sources are streamed into a fresh container, built there, and
the build tree is streamed back. Build trees are excluded on the way in
(`--exclude "./build-*"`).

## What was wrong (fixed 2026-07-19)

`sccache` ran on every compile, but its cache lived under
`C:\Users\ContainerAdministrator\AppData\Local\Mozilla\sccache\cache` —
**inside the container**, which is destroyed after each build. Every build
therefore started with an empty cache and recompiled all ~900 objects. The
statistics in the build log made this visible: `Cache hits 0`,
`Cache misses 0`, `Cache hits rate -`.

**Attempted fix (mechanism works, benefit NOT yet realised):** a persistent
Docker **named volume** (`kataglyphis-sccache`) mounted at `C:\sccache`, with
`SCCACHE_DIR` pointing at it and a 20 GB budget, wired into both the
bind-mount and tar-pipe code paths.

**Measured 2026-07-19 — it does not help yet.** sccache reports the volume as
its location (`Cache location Local disk: "C:\sccache"`, `Max cache size
20 GiB`), so the env var and mount take effect. But after a full build:

```
Compile requests            907
Cache hits rate            0.00 %
Cache misses                780
Cache size                0 bytes   <-- nothing is being written
```

Zero bytes stored means this is a **write** problem, not a key-stability
problem. Candidates, in order of suspicion:

1. The container user (`ContainerAdministrator`) may lack write permission on
   the mounted Windows named volume.
2. The build stops the sccache server (`sccache --stop-server`, see
   `WindowsCMake.Common.psm1`); if it is killed before flushing, entries may
   never land.
3. Windows named-volume semantics under process isolation.

Next diagnostic: run `sccache --show-stats` inside a container with the
volume mounted, write a file to `C:\sccache` manually, and check for an
error. Until then, builds remain ~350-480 s cold.

Inspect or reset it:

```powershell
docker volume ls | Select-String sccache
docker volume rm kataglyphis-sccache   # force a cold rebuild
```

## Still open: incremental builds

The build tree is **not** shared with the container (~8.2 GB for
`build-clangcl-debug`), so ninja always starts from scratch and rebuilds its
dependency graph even when sccache serves the objects. Two options, neither
implemented:

- **Named volume for the build directory** (e.g.
  `kataglyphis-build-clangcl-debug` mounted at `C:\ws\build-clangcl-debug`) —
  gives true incremental builds without moving gigabytes. CMake stores
  absolute paths, so the in-container path must stay stable (`C:\ws\...`),
  which it does.
- **Streaming the build tree in and out** — simple, but moves ~8 GB per build
  and is likely slower than it saves.

Measure before choosing. Tracked in `BACKLOG.md`.

## Gotchas

- The script takes `-Configurations` (comma-separated), **not** `-Preset`.
  Passing the wrong parameter silently builds all four configurations.
- Containers occasionally survive a successful build (`wcifs teardown lock`).
  A lingering container makes it look like a build is still running — compare
  the newest `logs/windows/build-summary-*.json` timestamp against the
  container start time before assuming.
- Host `cmake` (3.29) cannot read this repo's `CMakePresets.json` (`version:
  10`); only the container's newer CMake can.
