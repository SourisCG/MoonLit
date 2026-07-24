# Fedora KDE RTX 3060 Workstation

Role: complete development and functional testing environment plus heavy
performance validation.

- Distribution: Fedora (version collected by the app `run_doctor` command or
  `npm run tauri -- info`).
- Desktop: KDE Plasma.
- Session: expected Wayland; verify at runtime.
- GPU: NVIDIA RTX 3060 12 GB.
- Primary encoder: NVENC.
- Expected hardware codecs: H.264 and HEVC/H.265.
- AV1 hardware encoding: unavailable on this GPU.
- Exclusive responsibilities: none.

Use this profile for 1080p60, 1080p144, 1440p60, long replay buffers and soak
tests when physical access is available.
