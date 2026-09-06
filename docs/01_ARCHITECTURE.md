# 01 — Architecture

> MoonLit — Open-source, lightweight, zero-cloud alternative to Medal.tv for Linux and Windows.

## 1. Goals

- Clip the last N seconds (replay buffer) with a global hotkey (default `F9`) while gaming.
- Idle footprint: **< 80 MB RAM, ~0% CPU** while playing.
- No proprietary cloud. Local-first + user-owned storage (Google Drive).
- MIT/Apache-2.0 core where possible. **GPL-3.0-only** for the project as a whole, required because `gpu-screen-recorder` (Linux) is GPL-3.0-only. See `05_STORAGE_SECURITY.md` and `08_CI_CD_DISTRIBUTION.md` for FFmpeg sidecar isolation.

## 2. Non-negotiable rules

1. **Zero-Copy / Hardware Encoding First.** No CPU frame processing, no software rendering in the capture path.
2. **Lossless Cut by Default.** Time-only trims use FFmpeg `-c copy`. Re-encode only for vertical 9:16 / filters / volume remix.
3. **Zero-Cloud Backend.** No central server. SQLite locally, secrets in OS keyring, uploads client-to-service (Drive API, Discord webhooks).
4. **Relative Paths Only in DB.** Never store absolute paths. Resolve at runtime as `base_dir.join(file_name)`.
5. **Lazy Editor.** The editor (Wavesurfer + `<video>`) is `React.lazy`-loaded and fully destroyed on close (`ws.destroy()` + temp purge + optional window destroy).
6. **Platform Abstraction.** Rust `CaptureEngine` trait decouples Windows (native APIs) from Linux (sidecar subprocess).

## 3. Stack

- **Runtime:** Tauri v2 (`@tauri-apps/api`, `@tauri-apps/cli`)
- **Frontend:** React 19 + TypeScript + Vite + Tailwind CSS v3 + `wavesurfer.js v7` + `lucide-react` + `clsx`/`tailwind-merge`
- **i18n:** `react-i18next` + `i18next` — UI must be bilingual ES/EN from day one (`src/locales/es.json`, `en.json`).
- **Backend:** Rust + `tokio` (full), `serde`/`serde_json`, `rusqlite` (or `tauri-plugin-sql` for Phase 2), `keyring`, `sysinfo`, `rodio`, `oauth2`, `tiny_http`, `reqwest`
- **Platform crates:**
  - Linux: `nix` (signals)
  - Windows: `windows-capture`, `cpal`
- **Sidecars (Tauri `binaries/`):** `ffmpeg` (cross-platform), `gpu-screen-recorder` (Linux only)
- **Tauri plugins (already added in Phase 0):** `global-shortcut`, `clipboard-manager`, `notification`, `dialog`, `sql`, `opener`

## 4. Project tree (target)

```text
moonlit/
├── docs/                        # This spec (EN)
├── SPEC.md                      # Index + acceptance map
├── src-tauri/
│   ├── binaries/                # ffmpeg-<target>, gpu-screen-recorder (Linux)
│   ├── migrations/              # SQL migrations (Phase 2)
│   ├── capabilities/
│   ├── src/
│   │   ├── capture/             # mod.rs (trait), linux.rs (GSR), windows.rs (WGC)
│   │   ├── detector/            # gpu scan, wine parser, steam, minecraft, launchers, matcher
│   │   ├── storage/             # sqlite.rs, keyring.rs, paths.rs
│   │   ├── uploader/            # drive_auth.rs, drive_upload.rs, discord.rs, youtube.rs
│   │   ├── editor/              # ffmpeg.rs (thumb, extract, trim, vertical, mix)
│   │   ├── commands.rs          # Tauri IPC handlers
│   │   ├── state.rs             # AppState (engine, config, db pool)
│   │   ├── lib.rs / main.rs
│   ├── tauri.conf.json
│   └── Cargo.toml
├── src/
│   ├── components/starfield/    # MoonlitStarfield.tsx
│   ├── components/gallery/      # ClipGallery.tsx, ClipCard.tsx
│   ├── components/editor/       # ClipEditor.tsx (lazy)
│   ├── components/settings/     # SettingsModal.tsx, AppManager.tsx, ProcessPicker.tsx
│   ├── components/common/       # GlassModal, buttons, sliders
│   ├── hooks/                   # useClips.ts, useDriveUpload.ts, etc.
│   ├── types/index.ts           # Mirrors Rust structs
│   ├── locales/es.json, en.json
│   ├── App.tsx / main.tsx / index.css
```

## 5. IPC contract (preview, frozen in Phase 2)

```ts
// src/types/index.ts
export interface CaptureConfig { duration_seconds: number; fps: number; output_dir: string }
export interface ClipMetadata {
  id: string; file_name: string; thumbnail_name: string;
  duration_ms: number; file_size_bytes: number;
  createdAt: string; gameTitle?: string;
  is_favorite: number; drive_file_id?: string; drive_web_url?: string;
}
export interface TrimRequest {
  sourcePath: string; startTimeSeconds: number; endTimeSeconds: number;
  format: 'landscape' | 'portrait_blurred';
}
// invoke('start_replay_buffer', {config}) -> void
// invoke('save_replay_clip') -> ClipMetadata
// invoke('trim_clip', {request}) -> ClipMetadata
// invoke('start_google_auth') -> boolean
// invoke('upload_clip_to_drive', {clipId}) -> string (webViewLink)
// invoke('get_running_applications') -> ActiveApp[]
```

## 6. Resource strategy

- While gaming: main window hidden to tray, WebView paused, capture in VRAM + encoded packets in RAM ring (`~100-150 MB` for 60s 1080p 15 Mbps).
- React never touches pixels. It sends `{start, end}` numbers to Rust; Rust runs FFmpeg CLI.
- Starfield canvas pauses on `document.hidden` / `window blur` via `cancelAnimationFrame`.
