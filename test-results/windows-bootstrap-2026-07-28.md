# Windows Bootstrap Report

Date: 2026-07-28
Base commit: `2a5add2` (`main` at start of bootstrap)
Working tree: bootstrap changes were uncommitted when this report was created

## Machine

- OS: Windows 11 Pro x64, build 26200
- CPU: AMD Ryzen 5 5500, 6 cores / 12 threads
- GPU: NVIDIA GeForce RTX 3060 12 GB
- NVIDIA driver: 610.62
- Rust: 1.97.1, `x86_64-pc-windows-msvc`
- Node: 24.18.0
- npm: 11.16.0
- WebView2: 150.0.4078.99
- Tauri CLI: 2.11.4

## Checks

| Command | Result |
| --- | --- |
| `npm ci` | Pass, 0 vulnerabilities; npm reported a pending esbuild install script approval |
| `npm run check` | Pass, 0 errors and 0 warnings |
| `npm run build` | Pass |
| `cargo fmt --manifest-path src-tauri/Cargo.toml -- --check` | Pass |
| `cargo test --locked --manifest-path src-tauri/Cargo.toml` | Pass, 12 tests |
| `cargo clippy --locked --manifest-path src-tauri/Cargo.toml --all-targets --all-features -- -D warnings` | Pass |
| `cargo check --locked --manifest-path src-tauri/Cargo.toml --target x86_64-pc-windows-msvc` | Pass |
| `npm run tauri -- info` | Pass |
| `npm run tauri -- build --no-bundle` | Pass; produced `src-tauri/target/release/moonlit.exe` |

## Smoke Test

The release executable started successfully and remained running for eight
seconds before being stopped by the test harness. The current FakeBackend
flow is covered by Rust unit tests; no native capture or audio was exercised.

## Remaining Blockers

- `bundle.active` remains disabled and no installer was produced.
- The connected runtime still uses the minimal fake/GSR contract.
- Windows.Graphics.Capture, WASAPI and direct NVENC are not implemented.
- The generated icon set currently includes only the Windows/Linux assets
  needed by the active configuration.
