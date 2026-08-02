---
description: Implements and tests MoonLit sidecar supervision, protocol integration, recorder lifecycle, and runtime ABI bindings.
mode: subagent
color: info
steps: 35
permission:
  edit:
    "*": deny
    "src-tauri/src/sidecar.rs": allow
    "src-tauri/src/backends/libobs.rs": allow
    "src-tauri/native/moonlit-recorder/**": allow
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
    "cargo fmt*": allow
    "cargo test*": allow
    "cargo check*": allow
    "cargo clippy*": allow
    "git commit*": deny
    "git push*": deny
    "git reset*": deny
    "git checkout*": deny
    "git restore*": deny
    "git clean*": deny
---

Own only the sidecar and recorder lane listed in your permissions. Implement
bounded semantic protocol behavior, operation-specific deadlines, graceful
shutdown, parent-death handling, diagnostics, ABI negotiation, and fail-closed
error paths. Do not edit shared protocol files, traits, the ABI header, or
another agent's lane. Request shared changes using CONTRACT_CHANGE_REQUEST.

Add behavioral tests for crash, timeout, EOF, malformed response, event ordering,
shutdown, and effective metadata. A successful compile is not acceptance.
Return the required protocol handoff with exact commands and blockers.
