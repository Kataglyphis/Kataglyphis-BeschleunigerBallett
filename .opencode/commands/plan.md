---
description: Run the planner to analyze the codebase and add tasks to BACKLOG.md
agent: planner
---
Analyze the current state of the codebase. Review `BACKLOG.md` for existing open
tasks. Identify new work opportunities — bugs, improvements, missing tests,
technical debt, performance issues. Write detailed, actionable task entries to
`BACKLOG.md` following the existing format (size prefix, descriptive title,
bullet-point implementation guidance with file paths, steps, tests, and build
instructions). Do NOT duplicate existing tasks. Completed tasks are deleted
from the backlog, so also check `git log --oneline -30` before adding tasks
to avoid re-planning work already done. Entries marked `- [b]` are blocked:
do not duplicate them either, and only flip a `- [b]` back to `- [ ]` when
its stated blocker is verified gone. Add at most 5 new tasks.

$ARGUMENTS