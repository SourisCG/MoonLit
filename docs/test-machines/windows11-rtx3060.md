# Windows 11 RTX 3060 Workstation

Role: Windows bootstrap, native backend development and NVIDIA validation.

- OS: Windows 11 Pro x64, build 26200
- CPU: AMD Ryzen 5 5500, 6 cores / 12 threads
- GPU: NVIDIA GeForce RTX 3060 12 GB
- NVIDIA driver: 610.62
- Rust: stable MSVC toolchain, `x86_64-pc-windows-msvc`
- Windows SDK: 10.0.26100
- WebView2: installed
- Primary encoder target: direct NVENC, H.264 first

Native capture, audio, encoder, performance and long-running tests must be
recorded with the exact commit and remain subject to `docs/VALIDATION_QUEUE.md`.
