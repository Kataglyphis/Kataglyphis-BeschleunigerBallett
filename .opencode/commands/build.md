---
description: Build the project with a specified configuration preset
agent: executor
---
Build the project with the specified configuration.

**Windows (container build via Stevedore):**
```
pwsh -ExecutionPolicy Bypass -File .\Scripts\Windows\Build-Windows-Container.ps1 -Configurations $ARGUMENTS
```

**Linux:**
```
Scripts/Linux/cmake-configure-build.sh --preset $ARGUMENTS --build-dir build
```

Report build success or failures. If the build fails, read the error output and
attempt to fix it.

$ARGUMENTS