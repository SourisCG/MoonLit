---
description: Implements MoonLit Rust host services including config, storage, SQLite library, media jobs, recorder orchestration, hotkeys, tray, and notifications.
mode: subagent
color: secondary
steps: 45
permission:
  edit:
    "*": deny
    "src-tauri/src/config.rs": allow
    "src-tauri/src/storage.rs": allow
    "src-tauri/src/library.rs": allow
    "src-tauri/src/media.rs": allow
    "src-tauri/src/audio.rs": allow
    "src-tauri/src/hotkey.rs": allow
    "src-tauri/src/doctor.rs": allow
    "src-tauri/src/recorder.rs": allow
    "src-tauri/src/state.rs": allow
    "src-tauri/src/backends/fake.rs": allow
    "src-tauri/src/backends/mod.rs": allow
    "src-tauri/src/host/**": allow
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
    "npm test*": allow
    "git commit*": deny
    "git push*": deny
    "git reset*": deny
    "git checkout*": deny
    "git restore*": deny
    "git clean*": deny
---

Own only the listed Rust host services. Make configuration, storage, SQLite,
media jobs, recorder save coordination, hotkeys, tray, notifications, and
doctor behavior truthful and transactional. Never silently fall back to
FakeBackend. Never notify or emit clip success before the clip is finalized and
indexed. Never delete paths outside registered MoonLit roots.

Add tests for migrations, corrupt config recovery, cleanup safety, pagination,
reconciliation, proxy timeout, hotkey conflicts, notification policy, and
failure rollback. Shared contracts and `src-tauri/src/lib.rs` are coordinator
owned; request changes explicitly.
