# Windows Native WGC/NVENC Spike Report

Date: 2026-07-28
Base commit: `1c59205`
Working tree: native spike changes are uncommitted

## Machine

- OS: Windows 11 Pro x64, build 26200
- CPU: AMD Ryzen 5 5500, 6 cores / 12 threads
- GPU: NVIDIA GeForce RTX 3060 12 GB
- NVIDIA driver: 610.62
- Rust: 1.97.1, `x86_64-pc-windows-msvc`
- Windows SDK: 10.0.26100.0

## Scope

- Monitor-first `Windows.Graphics.Capture` through a free-threaded frame pool.
- D3D11 BGRA device and persistent GPU-side encoder texture.
- Direct NVENC H.264 through dynamically loaded `nvEncodeAPI64.dll`.
- Rust-only packet delivery into the portable `ReplayBuffer` boundary.
- Initial output format is raw H.264 Annex B; MP4/MKV muxing is not part of this spike.

## Evidence

| Command | Result |
| --- | --- |
| `cargo fmt --manifest-path src-tauri/native/windows-native/Cargo.toml -- --check` | Pass |
| `cargo check --locked --manifest-path src-tauri/native/windows-native/Cargo.toml` | Pass |
| `cargo test --locked --manifest-path src-tauri/native/windows-native/Cargo.toml` | Pass, 1 test |
| `cargo clippy --locked --manifest-path src-tauri/native/windows-native/Cargo.toml -- -D warnings` | Pass |
| `cargo run --locked --manifest-path src-tauri/native/windows-native/Cargo.toml --example capture_probe` | Pass, 104 packets, 1 keyframe, 2,960,586 bytes |
| `npm test` | Pass, 2 tests |
| `npm run check` | Pass |
| `npm run build` | Pass |
| `cargo fmt --manifest-path src-tauri/Cargo.toml -- --check` | Pass |
| `cargo check --locked --manifest-path src-tauri/Cargo.toml --target x86_64-pc-windows-msvc` | Pass |
| `cargo clippy --locked --manifest-path src-tauri/Cargo.toml --all-targets --all-features -- -D warnings` | Pass |
| `npm run tauri -- info` | Pass |
| `npm run tauri -- build --no-bundle` | Pass; produced `src-tauri/target/release/moonlit.exe` |
| `cargo test --locked --manifest-path src-tauri/Cargo.toml` | Blocked by `0xC0000139 STATUS_ENTRYPOINT_NOT_FOUND` before the test harness |

## Native Probe

The capability probe reported WGC support, NVENC H.264 support, a 1920x1080
default monitor limit and a 60 FPS capability. Two monitor sources were
enumerated. The five-second probe produced encoded packets and an IDR keyframe,
then released the WGC, D3D11 and NVENC resources without the earlier shutdown
crash.

## Limitations

- Only monitor capture is implemented; window capture and permission flows remain pending.
- The initial spike requires the requested capture size to match the monitor size.
- `NvencEncoder::finish` does not submit EOS because the current SDK wrapper faults on EOS after synchronous packet locking; each submitted frame is locked before shutdown.
- The root backend save path and Tauri UI start/save/stop flow still need a native end-to-end test.
- No performance, long-run, Windows 10, non-NVIDIA or multi-GPU validation was performed.
