# ROADMAP — Phased Execution (Spec-Driven)

Execute strictly in order. Do not start phase N+1 until phase N acceptance passes.

## Phase 1 — Scaffold, Tray, Hotkey, MoonLit UI base

- Tauri v2 + tray-icon (minimize-to-tray), `global-shortcut` F9 → test event/log.
- Tailwind MoonLit theme, `MoonlitStarfield.tsx` canvas (85 stars, pause on `hidden`), glass layout, i18n skeleton (`en`/`es`).
- **Accept:** F9 fires in any app; minimized <40 MB, 0% CPU; starfield pauses off-screen.

## Phase 2 — Safe persistence

- `rusqlite` (or `plugin-sql`) migrations: `clips`, `custom_apps`, `settings` (relative paths only). `keyring` module for `google_drive_refresh_token`.
- IPC: `list_clips`, `toggle_favorite`, `register_app`, `get_settings/set_settings`, `resolve_clip_src`.
- **Accept:** CRUD works; `base_dir + file_name` resolves; no absolute path in DB; token round-trips in OS vault.

## Phase 3 — Capture engine (replay + dual audio)

- `CaptureEngine` trait; Linux GSR sidecar (`-r 30 -a "default_output|default_input"`, `SIGUSR1` via `nix`); Windows `windows-capture` + `cpal` stub; `rodio` ding; tray status.
- **Accept:** F9 → `.mp4` <1s with 2 audio tracks; indexed in DB with thumbnail.

## Phase 4 — Game detection + launchers

- GPU FD scan, Wine cmdline parser + blacklist, `SteamAppId` + `.acf`, Minecraft/Prism, Heroic/Epic manifests, Battle.net child, Xbox title, `get_running_applications` + `matcher.rs`.
- Frontend `AppManager` + process picker (running / click-window / browse).
- **Accept:** native + Wine/Proton + Minecraft report correct titles; custom app overrides duration.

## Phase 5 — Lazy editor + FFmpeg

- `ClipEditor` lazy + Wavesurfer Regions + dual waveforms + `destroy` + temp purge; Rust `ffmpeg.rs` (thumb, extract, lossless, vertical HW, remix).
- **Accept:** landscape <1s lossless; vertical 1080x1920 HW; RAM back to baseline on close.

## Phase 6 — Sharing (Drive + social)

- Drive PKCE loopback + resumable chunks + progress + public link + clipboard; Discord webhook; Twitter intent+clipboard; YouTube (+`#Shorts`); TikTok draft/fallback.
- **Accept:** Drive URL public + copied + notified; Discord/Twitter/YouTube flows work.

## Phase 7 — CI/CD packaging

- Narrow `tauri.conf.json` targets, `release.yml` matrix, SmartScreen README note, versioned Release assets.
- **Accept:** `v*` tag produces `.exe/.msi/.AppImage/.rpm/.deb` downloadables.

---

## Agent prompt pattern (per phase)

> "Follow `SPEC.md` + `docs/XX_*.md`. Implement ONLY Phase N. Do not write later-phase logic. Verify acceptance checklist before finishing."
