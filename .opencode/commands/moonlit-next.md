---
description: Advance exactly one MoonLit execution wave through its evidence gate.
agent: moonlit-coordinator
---

Advance MoonLit by at most one execution wave. Read `AGENTS.md`,
`.opencode/MOONLIT_AGENT_PROTOCOL.md`, `docs/PLAN.md`, `docs/AI_CONTEXT.md`,
the latest relevant `test-results/` report, the current Git status and full
SHA. Determine the current gate from evidence before assigning work.

Use no more than three active subagents and no overlapping file lanes. Do not
start dependent work while the current gate is red or blocked. At the end,
integrate only verified results, run the affected checks, inspect the diff and
status, and return the mandatory handoff with all remaining blockers.

$ARGUMENTS
