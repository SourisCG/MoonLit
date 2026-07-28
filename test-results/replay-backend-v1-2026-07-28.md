# ReplayBackend v1 Report

Date: 2026-07-28
Base commit: `f924c41`
Working tree: migration changes are uncommitted

## Implemented

- One portable `ReplayBackend` trait and canonical serialized DTOs.
- Bounded recorder actor with snapshots, transitions and recorder events.
- GOP-aware encoded replay buffer with synthetic H.264 packet tests.
- FakeBackend source enumeration and atomic simulation manifests.
- Platform factory with Fake and unavailable Windows native backend.
- Linux-only `LegacyGsrBackend` adapter using the same contract.
- Tauri commands no longer expose GSR-specific executable selection.
- Typed frontend IPC client and event subscription.

## Verification

| Command | Result |
| --- | --- |
| `npm install --save-dev vitest@3.2.6` | Pass, 0 vulnerabilities |
| `npm test` | Pass, 2 tests |
| `npm run check` | Pass |
| `npm run build` | Pass |
| `cargo fmt --manifest-path src-tauri/Cargo.toml -- --check` | Pass |
| `rustc --edition=2021 --test src-tauri/src/replay.rs` | Pass, 5 tests |
| `cargo test --locked --manifest-path src-tauri/Cargo.toml --no-run` | Pass; test binaries compile |
| `cargo test --locked --manifest-path src-tauri/Cargo.toml` | Blocked; `moonlit_lib` exits with `0xC0000139` before the harness |
| `cargo clippy --locked --manifest-path src-tauri/Cargo.toml --all-targets --all-features -- -D warnings` | Pass |
| `cargo check --locked --manifest-path src-tauri/Cargo.toml --target x86_64-pc-windows-msvc` | Pass |
| `npm run tauri -- build --no-bundle` | Pass; produced `moonlit.exe` |
| Release executable smoke start | Pass; remained alive for 8 seconds |

## Environment Limitation

`cargo test` compiles but the `moonlit_lib` Windows test executable exits before
the harness with `0xC0000139 STATUS_ENTRYPOINT_NOT_FOUND`. This reproduces
after `cargo clean` and direct execution; the small `moonlit` test executable
does run, and the replay module tests pass when compiled as an isolated
standard-library test binary. The failing binary imports Windows API-set and
CRT dependencies, so the full harness gate remains open for a clean native
loader/toolchain environment.

## Next Gate

Implement the WGC/D3D11/NVENC spike. It remains Windows-only and must not
change this IPC contract.
