---
description: Run clang-tidy and cmake-format static analysis and formatting checks
agent: executor
---
Run code quality checks and fix any issues found.

**Windows:**
```
powershell -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows.ps1 -Configurations clangcl-debug -SkipBuild -SkipTests -SkipPerfTests -SkipMsix
```
This runs clang-format (check mode) and clang-tidy. To apply formatting fixes,
add `-ApplyFormat`.

**Linux:**
```
Scripts/Linux/run_static_analysis_format.sh
```
This runs clang-format, clang-tidy, and cmake-format.

Report the results. Fix any issues found in files you or the agentic loop have
touched. Do not reformat the entire tree — scope to changed files only.

$ARGUMENTS