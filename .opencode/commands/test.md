---
description: Run the test suite for the current build
agent: executor
---
Run the full test suite and report results.

**Windows:** host ctest cannot read the container-built tree (`C:/ws`
paths). Run the delivered gtest executables directly from the repo root:
```
./build-clangcl-debug/commitTestSuite.exe
```
or run ctest inside the container via `docker exec`.

**Linux:**
```
scripts/linux/run-ctest.sh --build-dir build
```

Report pass/fail counts and any failures with details. If tests fail, read the
failure output and attempt to fix the code or the test.

$ARGUMENTS