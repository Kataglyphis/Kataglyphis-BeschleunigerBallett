---
description: Run clang-tidy and cmake-format static analysis and formatting checks
agent: executor
---
Run code quality checks and fix any issues found.

**Windows:** quality = clang-format and cmake-format; use the commands and
scoping in `docs/code-quality.md`. clang-tidy needs the
`compile_commands.json` path-rewrite dance described there (the database is
generated in-container with `C:/ws` paths, so a plain host run fails), and
containerized builds never run it (`Build-Windows-Container.ps1` hard-codes
`-SkipTidy`).

**Linux:**
```
Scripts/Linux/run_static_analysis_format.sh
```
This runs clang-format, clang-tidy, and cmake-format.

Report the results. Fix any issues found in files you or the agentic loop have
touched. Do not reformat the entire tree — scope to changed files only.

$ARGUMENTS