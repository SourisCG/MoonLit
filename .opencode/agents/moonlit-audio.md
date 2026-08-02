---
description: Implements real WASAPI system and microphone capture, mixing, AAC encoding, device events, and A/V synchronization.
mode: subagent
color: info
steps: 40
permission:
  edit:
    "*": deny
    "src-tauri/native/moonlit-audio/**": allow
    "src-tauri/native/moonlit-wasapi/**": allow
    "src-tauri/native/audio/**": allow
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
    "ffprobe *": allow
    "ffmpeg *": allow
    "git commit*": deny
    "git push*": deny
    "git reset*": deny
    "git checkout*": deny
    "git restore*": deny
    "git clean*": deny
---

Own only the native audio lane. Replace simulated audio with WASAPI loopback
and microphone capture, 48 kHz clocked mixing, gain/mute, AAC output, stable
device enumeration, disconnect/default-device events, and measurable A/V sync.
Application-specific audio is post-v1 and must not be added accidentally.

Do not mark zero levels or zero drift as real evidence. Add controlled-tone and
device-change tests, and report hardware requirements separately. Request
protocol, traits, or host changes instead of editing shared files.
