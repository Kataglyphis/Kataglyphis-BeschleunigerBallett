---
description: Run the test suite for the current build
agent: executor
---
Run the full test suite and report results.

**Windows:**
```
ctest --test-dir build-clangcl-debug --output-on-failure -C Debug
```

**Linux:**
```
Scripts/Linux/run-ctest.sh --build-dir build
```

Report pass/fail counts and any failures with details. If tests fail, read the
failure output and attempt to fix the code or the test.

$ARGUMENTS