---
description: Run the executor to process the next unchecked task from BACKLOG.md
agent: executor
---
Read `BACKLOG.md` and find the first unchecked task (`- [ ]`). Implement it
fully: make the code changes, add or update tests, and build with the
appropriate preset (default: `clangcl-debug` on Windows, `linux-debug-clang`
on Linux). Once the task is complete and the build passes, mark it as checked
(`- [x]`) in `BACKLOG.md` with a brief summary of what was done. Then commit
the changes.

$ARGUMENTS