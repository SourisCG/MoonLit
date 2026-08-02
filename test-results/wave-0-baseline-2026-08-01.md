# Wave 0 Baseline Report

Date: 2026-08-01
Recorded at: 2026-08-01T18:33:19.1928342-06:00
Base SHA: `9486f6ccc8f6ee49becec4375456c99d4ea83541`
Branch: `main` (ahead of `origin/main` by 2 commits)
Worktree: dirty; `docs/AI_CONTEXT.md`, `src-tauri/src/lib.rs`,
`.opencode/`, `docs/V1_EXECUTION.md`, `docs/V1_REQUIREMENTS.md`, and
`opencode.json` are changed or untracked. This is development evidence only.

## Environment

- OS: Windows 11 Pro x64, build 26200
- CPU: AMD Ryzen 5 5500
- RAM: 15.88 GiB
- GPU: NVIDIA RTX 3060
- Driver: 32.0.16.1062
- Node: 24.18.0
- npm: 11.16.0
- Rust/Cargo: 1.97.1 MSVC
- Tauri CLI: 2.11.4

## Commands

| Command | Exit | Result |
|---|---:|---|
| `npm run check` | 0 | Pass; 0 Svelte errors and warnings |
| `npm run build` | 0 | Pass; Vite production build |
| `cargo fmt --manifest-path src-tauri/Cargo.toml -- --check` | 0 | Pass |
| `cargo test --locked --manifest-path src-tauri/Cargo.toml` | 0 | Pass; 15 tests |
| `cargo test --locked --manifest-path src-tauri/native/libobs-protocol/Cargo.toml` | 0 | Pass; 6 tests |
| `cargo test --locked --manifest-path src-tauri/native/moonlit-recorder/Cargo.toml` | 0 | Pass; 0 tests |
| `cargo test --locked --manifest-path src-tauri/native/windows-native/Cargo.toml` | 0 | Pass; 2 tests |
| `cargo clippy --locked --manifest-path src-tauri/Cargo.toml --all-targets --all-features --target x86_64-pc-windows-msvc -- -D warnings` | 0 | Pass |
| `git diff --check` | 0 | Pass |

## Development Artifact Hashes

These are frontend build outputs only and do not prove production media:

- `dist/index.html`: `3AA779E462EADB50F84CB15F58CAE4FC0539A11CD80146C31A4415A486837601`
- `dist/assets/index-BN4X-TCD.css`: `EABA27D57322030594A8887164C734BD47332018811FE24A94FA52542BF90C47`
- `dist/assets/index-BlSLJ7BU.js`: `935F3243FAB1BD9BF4B9D4DE53C37AEC30DABAC988C10ECECC40E9CDC3A40766`

No real MP4/MKV, runtime manifest, staged runtime, installer, or signed
artifact was produced.

## Gate Result

Wave 0 remains `blocked`, not `verified`:

- The current tree is dirty and has no clean-SHA evidence.
- The production bridge is still an unavailable stub.
- CMake, Ninja, `cl.exe`, `dumpbin.exe`, `signtool.exe`, NSIS, and `ffprobe`
  are unavailable on this workstation.
- Runtime, license, SBOM, signing, clean-machine, GPU-vendor, and soak gates
  remain absent or blocked.
- Existing historical reports remain inadmissible because they use older
  commits or dirty trees.

## Next Owner

`moonlit-coordinator` must resolve the baseline truth/documentation gate and
toolchain blockers before starting Wave 1. No dependent implementation wave is
authorized by this report.
