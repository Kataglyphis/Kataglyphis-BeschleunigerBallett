# Code Quality Tooling (clang-format, clang-tidy, cmake-format)

Canonical home for formatting and static-analysis commands. `AGENTS.md`
links here rather than restating them.

Config lives at the repo root: `.clang-format`, `.clang-tidy`,
`.cmake-format.yaml`.

## Where the tools are

On this Windows host LLVM is installed but **not on `PATH`**:

```pwsh
$CF = 'C:\Program Files\LLVM\bin\clang-format.exe'   # 22.1.8
$CT = 'C:\Program Files\LLVM\bin\clang-tidy.exe'
```

## clang-format

Works on the host with no build directory — it needs only the source and
`.clang-format`.

**Scope matters.** `git ls-files '*.cpp'` from the repo root also matches
vendored third-party code under `ExternalLib/` (198 files, of which ~141
have drift that is not ours to fix). Always scope to our own sources:

```pwsh
$own = git ls-files 'Src/*.cpp' 'Src/*.hpp' 'Src/*.ixx' 'Test/*.cpp' 'Test/*.hpp'
```

**Check only (CI-style, writes nothing, non-zero exit on drift):**

```pwsh
$dirty = @()
foreach ($f in $own) {
  & $CF --dry-run --Werror $f 2>&1 | Out-Null
  if ($LASTEXITCODE -ne 0) { $dirty += $f }
}
"$($dirty.Count) / $($own.Count) files need formatting"
```

**Apply in place:**

```pwsh
foreach ($f in $own) { & $CF -i $f }
```

**Only what you touched** (the low-risk everyday version — reformatting the
whole tree at once buries real changes in noise):

```pwsh
git diff --name-only HEAD -- 'Src/*' 'Test/*' |
  Where-Object { $_ -match '\.(cpp|hpp|ixx|h)$' } |
  ForEach-Object { & $CF -i $_ }
```

Linux equivalent: `Scripts/Linux/run_static_analysis_format.sh` (also runs
`cmake-format`, provisioning it through `uv` + `requirements.txt` if absent).

## clang-tidy

Needs `compile_commands.json`. Two host-specific traps:

1. **Container paths.** The database in `build-clangcl-debug/` is generated
   *inside* the build container, where the workspace is `C:/ws`. Running
   host clang-tidy against it fails with
   `LLVM ERROR: Cannot chdir into "C:/ws/build-clangcl-debug"`. Rewriting
   the paths into a scratch copy gets it running:

   ```pwsh
   $db = "$env:TEMP\tidydb"; New-Item -ItemType Directory -Force $db | Out-Null
   (Get-Content build-clangcl-debug\compile_commands.json -Raw) `
     -replace 'C:/ws', 'D:/GitHub/Kataglyphis-BeschleunigerBallett' |
     Set-Content "$db\compile_commands.json" -NoNewline
   & $CT -p $db --quiet Src/GraphicsEngineVulkan/Main.cpp
   ```

2. **C++23 modules.** Even with paths fixed, TUs that `import` a module fail
   (`cannot open file '...App.ixx'`) because module BMIs still reference the
   container layout. `Scripts/Windows/modules/WindowsClang.Common.psm1`
   deliberately **skips files using module syntax** for this reason. What
   remains checkable is the non-module surface — still worthwhile
   (`cppcoreguidelines-special-member-functions`,
   `modernize-use-trailing-return-type` and friends fire on real code).

The clean alternative is to let the build run it, where paths are
consistent by construction (see below).

## Running them as part of a build

`Build-Windows.ps1` runs both unless told otherwise:

```pwsh
# format + tidy included (slower)
.\Scripts\Windows\Build-Windows.ps1 -Configurations 'clangcl-debug'

# the fast loop, skipping them
.\Scripts\Windows\Build-Windows.ps1 -Configurations 'clangcl-debug' -SkipFormat -SkipTidy
```

**Caveat worth knowing:** `Scripts/Windows/Build-Windows-Container.ps1`
hard-codes `-SkipTidy` when it invokes `Build-Windows.ps1`, so
containerized builds never run clang-tidy — but they always run the
clang-format check (only `-SkipTidy` is hard-coded; the container script
has no `-SkipFormat` to forward). Host `Build-Windows.ps1` accepts
`-SkipFormat`. That is why tidy drift accumulates even when every build is
green — and format drift accumulates too, for a different reason: the
clang-format check that does run reports its deviating count but never
fails the build on it (see "Known state" below).

## Suggested cadence

- **Per change:** format the files you touched (the `git diff` variant).
- **Weekly / before a PR:** full check across `Src/` + `Test/`; fix what is
  yours.
- **Periodically:** a clang-tidy pass over the non-module TUs, and
  `Scripts/Linux/run_static_analysis_format.sh` on Linux (it adds
  `scan-build` static analysis on top).

## Known state (2026-08-04)

<!-- format-drift-denominator: 215 -->

**140 of 215** own sources under `Src/` and `Test/` differ from
`.clang-format` (up from 72 of 125 on 2026-07-19). `Invoke-ClangFormatCheck`
(see the caveat above) reports this count on every container build and
**never fails the build** on it — that is why it grew from 72 to 140 while
every build stayed green. The "tidy drift accumulates even when every build
is green" sentence above applies to format drift too, not just clang-tidy.
Reformatting them is a **decision, not a chore**: it touches most of the
engine in one commit and will collide with in-flight work. Tracked in
`BACKLOG.md` — do it deliberately, ideally right after a merge point, and
add the commit to `.git-blame-ignore-revs` so history stays readable.
