---
description: Implements MoonLit-owned Windows Graphics Capture monitor and window sources plus source lifecycle robustness.
mode: subagent
color: success
steps: 40
permission:
  edit:
    "*": deny
    "src-tauri/native/windows-native/**": allow
    "src-tauri/native/moonlit-obs-source/**": allow
    "src-tauri/native/moonlit-wgc/**": allow
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
    "cmake --build*": allow
    "ctest *": allow
    "git commit*": deny
    "git push*": deny
    "git reset*": deny
    "git checkout*": deny
    "git restore*": deny
    "git clean*": deny
---

Own only the WGC source lane. The WindowsNative code is a benchmark and must
remain truthfully labeled; do not make raw Annex-B output look like MP4 or MKV.
Implement MoonLit-owned monitor and window capture without hooks or injection,
stable source identities, frame pacing, resize/frame-pool recreation, DPI and
HDR policy, adapter affinity, permission/source-ended handling, and safe
shutdown.

Request bridge or protocol changes instead of editing shared files. Add tests
for source enumeration, lifecycle changes, errors, and unsupported display
conditions. Record hardware-only checks as blocked when the machine cannot run
them.
