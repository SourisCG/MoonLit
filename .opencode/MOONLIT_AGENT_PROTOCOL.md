# MoonLit Agent Protocol

This project uses a coordinated multi-agent workflow. The coordinator owns
integration; specialists own isolated lanes. The product is not complete until
the relevant implementation, integration, and external validation gates have
evidence tied to one clean Git commit and one artifact hash.

## Non-Negotiable Rules

- Read `AGENTS.md`, `docs/PLAN.md`, `docs/AI_CONTEXT.md`, the latest relevant
  report in `test-results/`, `git status`, and the current commit before work.
- Preserve changes made by the user or another agent. Never reset, checkout,
  restore, clean, amend, commit, or push.
- Never claim that a contract, scaffold, FakeBackend result, probe, build, or
  source file proves a real media feature.
- Never use game injection, hooks, overlays, Game Capture, `win-capture`, or a
  shell to execute user-controlled strings.
- Never mark a runtime, license, hardware, signature, or legal gate approved
  without the required external evidence.
- Do not commit clips, credentials, private paths, or large generated files.
- Keep the RTX 3060 optional. Missing hardware belongs in
  `docs/VALIDATION_QUEUE.md` and remains a release blocker when the requirement
  is mandatory.
- Use ASCII for new files unless an existing file clearly requires otherwise.
- Use `apply_patch` for manual edits.

## State Model

Use only these states in plans, reports, and handoffs:

- `pending`: not started.
- `in_progress`: actively being implemented or verified.
- `blocked`: blocked by an explicit technical, hardware, legal, or credential
  dependency.
- `verified`: implementation and required evidence passed.

Do not use `completed` as a substitute for evidence.

## Coordinator Rules

- The coordinator is the only agent allowed to change shared contracts.
- At most three implementation or verification subagents may be active at once.
- Do not start a dependent wave until its gate is green.
- Keep at most two heavy native/build jobs active simultaneously.
- Pause workers when a `CONTRACT_CHANGE_REQUEST` is needed; update the shared
  contract once, then resume only affected workers.
- Assign non-overlapping lanes. If two tasks need the same file, they are not
  parallel tasks.
- A worker's report is input to review, not proof of completion.
- A worker must not approve its own implementation.
- After each wave, run the affected checks, then the light baseline checks, and
  create or update a report tied to the exact full Git SHA and worktree status.
- Do not create a release installer until all required media, legal, signing,
  clean-machine, hardware, and soak gates are verified.

## Handoff Format

Every specialist must return:

```text
STATUS: pending | in_progress | blocked | verified
BASE_SHA: <full SHA>
FILES_CHANGED: <paths or none>
REQUIREMENTS: <IDs or names>
IMPLEMENTED: <short factual list>
TESTS_RUN: <commands and results>
EVIDENCE: <report paths, hashes, or none>
BLOCKERS: <explicit blockers or none>
CONTRACT_CHANGE_REQUEST: <none or exact requested interface change>
NEXT_OWNER: <agent name>
```

Never report "works" without naming the command, artifact, and acceptance
criterion that proves it.

## Shared Ownership

Only `moonlit-coordinator` edits these shared areas:

- `src-tauri/src/traits.rs`
- `src-tauri/src/lib.rs`
- `src-tauri/native/libobs-protocol/**`
- `src-tauri/native/moonlit-obs-bridge/include/**`
- `src/lib/capture/types.ts`
- `src/lib/capture/client.ts`
- `package.json`, `package-lock.json`
- `src-tauri/Cargo.toml`, `src-tauri/Cargo.lock`
- `opencode.json`, `.opencode/**`
- `docs/V1_REQUIREMENTS.md`, `docs/V1_EXECUTION.md`

Specialists request changes to these areas instead of editing them.

## Execution Waves

### Wave 0: Truth And Baseline

Run `moonlit-assurance`, `moonlit-verifier`, and `moonlit-release` in parallel.
Freeze v1 requirements, identify stale evidence, repair test discovery, and
inventory the native toolchain. The coordinator records the baseline SHA and
does not advance until the repository claims match reality.

### Wave 1: Contracts And Host Integrity

Run `moonlit-runtime`, `moonlit-host`, and `moonlit-frontend` in parallel.
Implement explicit capability tuples, effective settings, safe config/storage/
SQLite behavior, and truthful frontend state. Contract requests go through the
coordinator.

### Wave 2: Native Runtime

Run `moonlit-native-core`, `moonlit-capture`, and `moonlit-assurance` in parallel
only after Wave 1 contracts are frozen. Build the pinned libobs runtime, bridge
ABI, explicit DLL closure, and MoonLit-owned WGC source. Keep the runtime
fail-closed until real checks pass.

### Wave 3: First Real Clip

Run `moonlit-native-core`, `moonlit-runtime`, and `moonlit-verifier`. Implement
monitor WGC, x264 H.264, replay, MP4, atomic finalization, and real sidecar
start/save/stop. This is the first point where a real media file may be claimed.

### Wave 4: Codec Matrix

Run `moonlit-codecs`, `moonlit-capture`, and `moonlit-runtime`. Add H.264
hardware paths, MKV, HEVC, x265, explicit fallback, and effective encoder
metadata. Each tuple requires independent probe, save, decode, and playback
evidence.

### Wave 5: Audio And Proxy

Run `moonlit-audio`, `moonlit-host`, and `moonlit-frontend`. Add WASAPI,
mixing, AAC, shared timestamps, device events, bounded HEVC proxy jobs, and
secure playback. Audio synchronization is a release gate, not a simulation.

### Wave 6: Product Integration

Run `moonlit-host`, `moonlit-frontend`, and `moonlit-reviewer`. Finish the
host-owned save action, configurable hotkey, tray/lifecycle policy,
notifications, library reconciliation, settings, accessibility, and E2E flows.

### Wave 7: Release Closure

Run `moonlit-release`, `moonlit-assurance`, and `moonlit-verifier`. Complete
runtime closure, imports, SBOM, licenses, corresponding source, NSIS standard
and offline installers, signing, clean-machine tests, and release workflow
gates. `design-only` and unsigned output are failures.

### Wave 8: External Qualification

Run the exact signed artifacts on Windows 10 Enterprise LTSC 2021 and Windows
11 with NVIDIA, AMD, Intel, and CPU-only profiles. Execute the required codec
matrix, repeated saves, lifecycle cycles, A/V sync, and 24-hour soak. Missing
hardware remains `blocked`; it is never silently marked verified.

### Wave 9: Promotion Review

Freeze source edits. Run `moonlit-assurance`, `moonlit-verifier`, and
`moonlit-reviewer` independently against the same signed artifact hashes. The
coordinator may promote only when every requirement is `verified`.

## Evidence Requirements

Every report must include the full Git SHA, clean/dirty status, OS build, CPU,
RAM, GPU and driver where relevant, tool versions, exact commands, exit codes,
artifact SHA-256 values, and timestamps. A report from a dirty or older tree is
development evidence only and cannot satisfy a release gate.

The direct WindowsNative Annex-B benchmark proves only the benchmark. FakeBackend
proves only UI and portable behavior. A sidecar probe proves only discovery.
None of these proves a production MP4/MKV recording.
