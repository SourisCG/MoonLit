# MoonLit Agent Instructions

## Before changing code

1. Read `docs/PLAN.md`.
2. Read `docs/AI_CONTEXT.md`.
3. Read the latest relevant report under `test-results/`, if one exists.
4. Inspect `git status` and the current commit.
5. Preserve user changes. Never reset or checkout unrelated work.

## Project rules

- MoonLit is a Windows-first game clip recorder built with Tauri 2, Svelte 5,
  TypeScript and Rust. Linux version planned for future release.
- The application must remain usable from either development workstation.
- Do not make the RTX 3060 a runtime requirement.
- Keep capture, portals and hardware capabilities behind interfaces so the
  `FakeBackend` can exercise the complete UI without a recorder or GPU.
- Never inject code, hooks or overlays into games.
- Never execute user-controlled command strings through a shell.
- Do not commit clips, credentials, private paths or large generated files.
- Update `docs/AI_CONTEXT.md` when a milestone, hardware result or decision
  changes.
- Put unavailable hardware tests in `docs/VALIDATION_QUEUE.md`; do not block
  ordinary development on a test that needs the other workstation.

## Verification

Run the light checks on any development machine:

```text
npm run check
npm run build
cargo fmt --manifest-path src-tauri/Cargo.toml -- --check
cargo test --manifest-path src-tauri/Cargo.toml
```

When the native toolchain and Tauri system libraries are installed, also run:

```text
npm run tauri -- info
npm run tauri -- dev
```

The application itself exposes the runtime capability check through the
`run_doctor` command and the UI. The Tauri CLI does not provide a `doctor`
subcommand in the currently pinned major version.

Heavy capture and NVENC tests are optional during development and must be
recorded with the exact Git commit and machine profile.
