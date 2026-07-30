# libobs Sidecar Scaffold Report

Date: 2026-07-29

Base commit: `4ddee14`

Working tree: implementation is uncommitted

## Implemented

- Bounded protocol crate with version 1, length-prefixed JSON frames and
  malformed/truncated/oversized frame tests.
- Supervised Windows process transport with absolute executable validation,
  five-second request deadlines, bounded stderr collection, EOF detection and
  kill/reap cleanup.
- `LibobsSidecarBackend` behind the existing `ReplayBackend` contract.
- Fail-closed `moonlit-recorder --self-test --json` scaffold.
- Recorder actor health polling and recovery from save/worker failures.
- Release-only Tauri resource configuration and design-only OBS runtime locks.
- Selective removal of the broken Media Foundation branch from the native
  benchmark path.

## Verification

| Command | Result |
| --- | --- |
| `cargo fmt --manifest-path src-tauri/Cargo.toml -- --check` | Pass |
| `cargo fmt --manifest-path src-tauri/native/windows-native/Cargo.toml -- --check` | Pass |
| `cargo clippy --locked --manifest-path src-tauri/Cargo.toml --all-targets --all-features --target x86_64-pc-windows-msvc -- -D warnings` | Pass |
| `cargo clippy --locked --manifest-path src-tauri/native/windows-native/Cargo.toml --all-targets -- -D warnings` | Pass |
| `cargo test --locked --manifest-path src-tauri/native/windows-native/Cargo.toml` | Pass, 2 tests |
| `cargo test --locked --manifest-path src-tauri/native/libobs-protocol/Cargo.toml` | Pass, 5 tests |
| `cargo test --locked --manifest-path src-tauri/native/moonlit-recorder/Cargo.toml` | Pass |
| `npm run check` | Pass |
| `npm test` | Pass, 2 tests |
| `npm run build` | Pass |
| `npm run tauri -- info` | Pass |
| `npm run tauri -- build --no-bundle` | Pass; produced `src-tauri/target/release/moonlit.exe` |
| `cargo test --locked --manifest-path src-tauri/Cargo.toml` | Blocked by existing `0xC0000139 STATUS_ENTRYPOINT_NOT_FOUND` before the harness |

## Sidecar self-test

The recorder was run with the current application resource directory. It
correctly returned `ready: false` and listed the missing `obs.dll`,
`libobs-d3d11.dll`, bridge and mux helper rather than pretending that libobs is
available.

## Blockers

- This workstation has no `cmake`, `cl.exe` or `ninja.exe` on PATH.
- The pinned OBS source/dependency build and custom WGC bridge are therefore
  not built.
- The runtime manifests remain `design-only` and cannot be used by the staging
  script until the exact closure and license audit are approved.
