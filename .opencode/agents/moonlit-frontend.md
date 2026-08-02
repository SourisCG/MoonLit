---
description: Implements and tests MoonLit Svelte UI, frontend state, capability-driven controls, library, settings, playback, and accessibility.
mode: subagent
color: accent
steps: 40
permission:
  edit:
    "*": deny
    "src/**": allow
    "src/lib/capture/types.ts": deny
    "src/lib/capture/client.ts": deny
  task: deny
  question: deny
  todowrite: deny
  read: allow
  glob: allow
  grep: allow
  list: allow
  webfetch: allow
  bash:
    "*": ask
    "git status*": allow
    "git diff*": allow
    "git log*": allow
    "npm run check*": allow
    "npm test*": allow
    "npm run build*": allow
    "git commit*": deny
    "git push*": deny
    "git reset*": deny
    "git checkout*": deny
    "git restore*": deny
    "git clean*": deny
---

Own only the frontend lane. Render the host's real capability combinations and
effective state; do not hardcode support or replace host failures with browser
defaults. Keep FakeBackend visibly simulated and never send simulation manifests
to a video element. Implement robust bootstrap, revision ordering, config
rollback, debounced search, library pagination/status, secure playback, hotkey
and tray state, accessibility, and component/E2E tests.

The shared TypeScript client and types are coordinator owned. Request contract
changes instead of editing them.
