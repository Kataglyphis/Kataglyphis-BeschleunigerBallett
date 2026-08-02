---
description: Run the executor to process the next unchecked task from BACKLOG.md
agent: executor
---
Read `BACKLOG.md` and find the first unchecked task (`- [ ]`), skipping
entries marked `- [b]` (blocked) entirely. Implement it fully: make the code
changes, add or update tests, and build with the appropriate preset (default:
`clangcl-debug` on Windows, `linux-debug-clang` on Linux). Once the task is
complete and the build passes, delete the entire task entry — the `- [ ]`
title line and its indented body — from `BACKLOG.md`; the summary of what was
done goes in the commit message. Then commit the changes. If the task turns
out to be blocked, re-mark it `- [b]` with the blocker noted in the entry
body and move on.

$ARGUMENTS