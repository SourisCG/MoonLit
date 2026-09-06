# MoonLit — SPEC (Index)

> Open-source, lightweight, zero-cloud Medal.tv alternative for Linux + Windows. Bilingual app (ES/EN). Docs in English.

## Status: Phase 0 complete (bare scaffold + docs)

Scaffold: Tauri v2 + React 19 + TS + Vite + Tailwind v3 + `react-i18next` + Wavesurfer + Lucide. Plugins: `global-shortcut`, `clipboard-manager`, `notification`, `dialog`, `sql`, `opener`. Package manager: **pnpm**.

## Doc map

- `docs/01_ARCHITECTURE.md` — rules, stack, tree, IPC contract.
- `docs/02_CAPTURE_ENGINE.md` — GSR Linux (`SIGUSR1`, `-r`, dual `-a`), WGC Windows, `CaptureEngine` trait, rodio cue.
- `docs/03_GAME_DETECTION.md` — GPU FD filter, Wine cmdline + blacklist, `SteamAppId` + `.acf`, Minecraft/Prism/Bedrock, Heroic/Epic/Battle.net/Xbox, `custom_apps` + picker + matcher.
- `docs/04_EDITOR_PIPELINE.md` — lazy `ClipEditor`, Wavesurfer Regions, FFmpeg sidecar (lossless/vertical/remix), keyframe note.
- `docs/05_STORAGE_SECURITY.md` — SQLite relative paths, `clips`/`custom_apps`/`settings`, ghost-clip reconcile, LRU prune, `keyring`.
- `docs/06_SOCIAL_INTEGRATIONS.md` — Drive PKCE + resumable + public link, Discord/Twitter/YouTube/TikTok, IG/FB deferred.
- `docs/07_UI_MOONLIT.md` — palette (`#050608`, `#0b0f19`, `#38bdf8→#818cf8`), pausable starfield canvas, glass layout, i18n.
- `docs/08_CI_CD_DISTRIBUTION.md` — `nsis/msi/appimage/deb/rpm` now, signing/MS Store/WinGet/Flathub later.
- `docs/ROADMAP_PHASES.md` — phased acceptance checklists.

## How to work (OpenCode / Cursor / Windsurf)

1. Read this file + the relevant `docs/0X_*.md`.
2. Implement **only** the requested phase from `ROADMAP_PHASES.md`.
3. Respect non-negotiables in `01_ARCHITECTURE.md` (relative paths, lossless-first, lazy editor, zero-cloud).
4. Verify acceptance checklist with real commands (`pnpm build`, `cargo check`, `ffprobe`).

### Next: Phase 1

> "Follow SPEC.md + docs/01_ARCHITECTURE.md + docs/07_UI_MOONLIT.md. Implement ONLY Phase 1: tray + F9 shortcut + MoonLit glass layout + MoonlitStarfield.tsx (pausable) + i18n skeleton. Do not write DB/capture/editor logic."
