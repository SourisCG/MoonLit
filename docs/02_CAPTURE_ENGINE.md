# 02 — Capture Engine (Replay Buffer + Dual Audio)

## 1. Concept

A clip app does NOT record continuously to disk. It keeps already-encoded packets in a RAM FIFO and remuxes to `.mp4`/`.mkv` on hotkey (no re-encode).

- GPU zero-copy capture → hardware encode (NVENC / AMF / QuickSync / VA-API) → ring buffer in RAM → flush on shortcut.

## 2. Linux: `gpu-screen-recorder` (GSR) as sidecar daemon

GSR is the fastest path on Linux (X11 + Wayland, KMS/DRM direct, no portal dialog).

### 2.1 CLI (replay + dual audio)

```bash
gpu-screen-recorder \
  -w screen \
  -f 60 \
  -k h264 \
  -c mp4 \
  -r 30 \
  -a "default_output|default_input" \
  -o /home/user/Videos/MoonLit
```

- `-w screen`: primary/focused monitor via KMS/NVFBC (no screencast dialog).
- `-r 30`: N-second RAM ring. `-o` is a **directory** in replay mode; GSR names files `replay_YYYY-MM-DD_HH-MM-SS.mp4`.
- `-a "default_output|default_input"`: Track 1 = desktop/game, Track 2 = mic (AAC stereo each).

### 2.2 Control from Rust

- Spawn as `tokio::process::Child` on app start / game start.
- `save_clip()`:
  1. `nix::sys::signal::kill(pid, SIGUSR1)`
  2. `sleep(300ms)`
  3. Pick newest `.mp4` in `output_dir` by `modified()` time.
- `stop()`: `SIGINT` + `child.wait()`.
- `Drop`: `start_kill()` to avoid zombies.

### 2.3 Permissions (avoid portal UX pain)

- Preferred: direct KMS capture, zero dialogs. Requires capability once at install/first run:
  ```bash
  sudo setcap cap_sys_admin+ep $(which gpu-screen-recorder)
  ```
- Fallback: XDG Portal `ScreenCast` with `persist_mode=2` + saved `restore_token` + onboarding screen ("Pick Entire Screen → Check Remember → Share"). If stream metadata looks like a single window, warn the user.
- Audio discovery: `pactl list short sources` / `pw-dump`. `*.monitor` = output, others = inputs.

### 2.5 Live per-track gain (no editing, no monitoring side effects)

GSR has no volume flag, so gain is applied at the PipeWire layer:
each `-a` input is its own recording stream (source-output). Setting
`pactl set-source-output-volume <idx> <pct>` changes ONLY what GSR captures —
device volumes (what the user hears) are untouched. Same mechanism as
pavucontrol's Recording tab, which upstream itself recommends.

- Streams are found via `pactl -f json list source-outputs`, matched by
  `application.name` (~gpu-screen-recorder) and `media.name` (~monitor = game),
  with index-order fallback. Re-polled after spawn (streams appear async).
- Gains (`gain_game/gain_mic`, `mute_game/mute_mic`) persist in `settings`,
  apply live through `set_track_gain`/`set_track_mute`, and re-apply on every
  `start_buffer`. Range 0–150%.
- Windows: gains persist identically; software multiplication lands on the
  Windows trip (we own the cpal path there).

### 2.6 Deps (Linux)

```toml
[target.'cfg(target_os = "linux")'.dependencies]
nix = { version = "0.29", features = ["signal", "process"] }
tokio = { version = "1", features = ["process", "time"] }
rodio = "0.21" # confirmation ding (synthesized, no assets)
```

## 3. Windows: `windows-capture` + hardware encoder

- No DLL injection (anti-cheat safe for Valorant/CS2). Use Windows Graphics Capture (WGC) / DXGI via `windows-capture` crate (Win10 1903+, Win11), 60/120 FPS.
- Audio: two WASAPI streams via `cpal`:
  - Thread A: `default_output` loopback (`eRender`) = game.
  - Thread B: `default_input` (`eCapture`) = mic.
- Mux as two AAC tracks in same `.mp4` with titles `Desktop/Game` and `Microphone`, or via FFmpeg sidecar:
  ```bash
  ffmpeg -f ddagrab -i desktop -f wasapi -i "default" -f wasapi -i "audio=Mic Name" \
    -map 0:v -map 1:a -map 2:a -metadata:s:a:0 title="Game" -metadata:s:a:1 title="Mic" \
    -c:v h264_nvenc -c:a aac output.mp4
  ```

## 4. Rust trait (frozen interface)

```rust
// src-tauri/src/capture/mod.rs
use std::path::PathBuf;
use async_trait::async_trait; // or manual async in trait (Rust 1.75+)

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct CaptureConfig { pub duration_seconds: u32, pub fps: u32, pub output_dir: PathBuf }

#[async_trait]
pub trait CaptureEngine: Send + Sync {
    async fn start_buffer(&mut self, config: CaptureConfig) -> Result<(), String>;
    async fn save_clip(&mut self) -> Result<PathBuf, String>;
    async fn stop_buffer(&mut self) -> Result<(), String>;
    fn backend_name(&self) -> &'static str; // running state = Option<Engine> in AppState
}
```

Use `async-trait` if toolchain needs it; otherwise native `async fn` in traits.

## 5. Feedback (must-have for fullscreen)

- Audio cue: `rodio` plays embedded `clip_saved.wav` (~0.2s ding) after flush. WebView notifications are invisible in exclusive fullscreen.
- Optional overlay: secondary Tauri window (`transparent:true, decorations:false, alwaysOnTop:true, skipTaskbar:true`, `set_ignore_cursor_events(true)`), auto-hide after 2s. Linux caveat: layer-shell behavior varies on Wayland.

## 6. Acceptance (Phase 3)

- [ ] F9 in-game writes `.mp4` of last 30s in < 1s.
- [ ] File has 2 audio tracks (game + mic).
- [ ] Idle: no disk writes from capture; RAM ring only.
