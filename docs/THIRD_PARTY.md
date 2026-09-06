# THIRD_PARTY — Bundled components and license compliance

MoonLit is `GPL-3.0-only` (see `LICENSE`). This file tracks every third-party
component shipped inside MoonLit installers and what the GPL requires for each.

## gpu-screen-recorder (Linux capture engine)

- **What:** `gpu-screen-recorder` CLI + `gsr-kms-server` helper, built with
  `-Dcapabilities=false -Dffmpeg_static=true` (same recipe as upstream Flathub).
- **Upstream:** https://git.dec05eba.com/gpu-screen-recorder/ by dec05eba.
- **License:** `GPL-3.0-only` (per Arch, Alpine, Artix packaging). Compatible:
  MoonLit as a whole is GPL-3.0-only.
- **Pinned source (Phase 7 CI builds from this):**
  `https://dec05eba.com/snapshot/gpu-screen-recorder.git.<rev>.<hash>.tar.gz`
  (same snapshot scheme as
  `flathub/com.dec05eba.gpu_screen_recorder`). The exact URL + sha512 used
  for each release is recorded in the GitHub Release notes.
- **Source offer (GPL §6):** Corresponding Source = upstream snapshot above
  plus our build flags (this file). No modifications are made to GSR itself.
- **Ship model per format:**
  - `.rpm` / `.deb` / `.AppImage`: prebuilt sidecar under
    `<app>/moonlit-gsr/`; system deps (`libdrm`, `libva`, pipewire, pulse)
    come from official distro repos via `Requires`/`Depends`.
  - Flatpak/Flathub: `gpu-screen-recorder` compiled as a manifest module
    (mirrors upstream manifest), `finish-args: --device=all
    --socket=pulseaudio --socket=wayland --socket=fallback-x11`.
  - Windows: not shipped (native WGC/AMF/NVENC APIs instead).
- **Runtime resolution order:** `MOONLIT_GSR_BIN` override → bundled sidecar
  → system `PATH` → clear error. Dev machines use a native install
  (Terra/COPR rpm on Fedora); end users never install anything extra.

## FFmpeg (editor pipeline)

- **What:** `ffmpeg` CLI sidecar for thumbnails, lossless cuts, vertical presets.
- **Upstream:** https://ffmpeg.org (static builds: BtbN for Windows,
  johnvansickle musl for Linux).
- **License:** depends on build flags. MoonLit uses LGPL-compatible builds
  (external hardware encoders only: nvenc/amf/qsv/vaapi — no libx264/libx265
  linked) AND keeps FFmpeg as a separate process (CLI boundary), so the
  GPL-3.0-only status of MoonLit comes from GSR, not FFmpeg.
- **Dev fallback:** system `ffmpeg` from `PATH` when no sidecar is present,
  with a log warning. Production always ships the pinned sidecar.

## Reference code (NOT shipped)

- Cap (AGPL-3.0) and LosslessCut (GPL-3.0): architecture/FFmpeg-args reference
  only. No code copied. If any snippet is ever adapted, its origin and license
  must be recorded here.
