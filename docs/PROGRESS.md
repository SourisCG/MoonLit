# PROGRESS — MoonLit build log

Single source of truth for phase status. Updated at the end of every phase.
Details per phase live in `ROADMAP_PHASES.md`; technical specs in `01_*`–`08_*`.

| Phase | Scope | Status | Commit | Acceptance |
|---|---|---|---|---|
| 0 | Bare scaffold (Tauri v2 + React-TS + Tailwind v3) + `docs/` spec | ✅ done | `6965fac` (squashed in) | `pnpm build` + `cargo check` green |
| 1 | Tray + F9 hotkey + glass UI + starfield + i18n ES/EN | ✅ done | `6965fac` | F9 fires globally, tray hide/show works |
| 1-fixes | Frameless custom topbar + MoonLit CSS logo + smooth starfield + F9 dedupe | ✅ done | `8e4b5c1` | Single-count verified by user, no tray glitch |
| license | GPL-3.0-only (required by gpu-screen-recorder) | ✅ done | `2b99add` | Verbatim LICENSE + metadata + README |
| 2 | rusqlite persistence (relative paths) + keyring secrets + settings UI | ✅ done | `c72edba` | CRUD, vault OK, folder picker fixed |
| 3 | Capture engine Linux (GSR embedded, 3-track mix-first, gains, ladder, 30/60fps, monitor select) | ✅ done (Linux) | `5ffd70d`+ui | F9 → `.mp4` 3×aac, thumbs, durations, gains — user-verified |
| 3-win | Capture engine Windows trip (WGC + WASAPI + AMF/QSV, same behaviors) | ⬜ next | — | See `09_WINDOWS_HANDOFF.md`; closes Phase 3 |
| 3-ui | Transparent tray icon, i18n codec labels, opener perms, disk note | ✅ done | `7c5d733` (batch) | user-verified pending |

## Cross-platform gate (project rule)

Any phase with per-OS code is implemented and tested on Linux first, then
**tested on Windows immediately before the phase is closed** — and likewise
in reverse whenever needed. No phase closes with an untested platform stub.
Applies from Phase 3 on (capture, detection, editor/FFmpeg, packaging).
| 4 | Game detection + launchers + custom apps | ⬜ pending | — | Native + Wine/Proton + Minecraft detected |
| 5 | Lazy editor + FFmpeg pipeline | ⬜ pending | — | Lossless <1s, vertical HW, no leak |
| 6 | Drive + social sharing | ⬜ pending | — | Public link copied + notified |
| 7 | CI/CD packaging | ⬜ pending | — | Tag produces all installers |

## Log

- **Phase 0/1** — Scaffold, tray minimize-to-tray, F9 global shortcut with notification, MoonLit glass layout, pausable canvas starfield, ES/EN i18n. Manual test: F9 counted globally, tray restore OK.
- **Phase 1-fixes** — Frameless window + custom topbar (drag, minimize, maximize, close-to-tray), MoonLit moon+play CSS logo from moonlit.souriscg.dev, time-based soft starfield twinkle, Rust 400ms + frontend 300ms F9 dedupe. Manual test passed by user.
- **License** — Adopted `GPL-3.0-only` (gpu-screen-recorder is GPL-3.0-only per Arch/Alpine/Artix). Verbatim FSF text, metadata in `package.json`/`Cargo.toml`, README section.
- **Phase 2-fixes** — Single-source locale (`useLocale`: DB + i18next in sync, both directions), added missing `dialog:allow-open` capability (folder picker was silently rejected), visible errors on browse/save, logo glow contained (was `z-index:-1` leaking → corner artifact).
- **Maximize flicker (root-caused)** — Tauri natively toggles maximize on double-click over `data-tauri-drag-region` (internal-toggle-maximize, tauri#12006); our own JS dblclick handler double-toggled (flicker). Fix: removed JS dblclick, native owns the gesture; buttons keep atomic `toggleMaximize` + debounce + OS-synced state. Capability `allow-internal-toggle-maximize` added explicitly.
- **Dev env** — `src-tauri/.cargo/config.toml` sets `WEBKIT_DISABLE_DMABUF_RENDERER=1` for all cargo runs (fixes Wayland `Error 71` crash at first paint on Fedora/GNOME); `pnpm tauri:dev` is the official launch command.
- **UI batch** — MoonLit icon set regenerated from `build-aux/moonlit-icon.svg`
  (taskbar + tray via `default_window_icon`); `lang.es/lang.en` translated in
  sidebar + select (last hardcoded Spanish gone); `opener:allow-open-path` +
  `opener:allow-reveal-item-in-dir` added (`opener:default` does not include
  those commands — root cause of the open-video error); disk-space note under
  the resolution selector (ES/EN).
- **Native scaler patch (scheduled, not implemented)** — After Flatpak + MS
  Store ship, before signing `.exe`/`.msi`: raise GSR's downscale filter from
  hardcoded `GL_LINEAR` (bilinear, no mipmaps — proven in pinned source
  `src/window_texture.c`) to Bicubic (Lanczos under test). Same RAM/bitrate/
  latency/save path. Acceptance: stock vs patched 720p on a 1:1 monitor.
- **Authorship rewrite (2026-09-06)** — All 26 commits re-signed from
  `SourisCG <souris@souriscg.dev>` (assumed by the agent at `git init`, never
  confirmed) to `Sebastián García <sebastian.garciab2004@gmail.com>` (user's
  global identity). Content, messages and dates byte-identical
  (`git diff` old vs new tip: empty). Original history preserved in branch
  `main-backup-20260906` (local + remote, keep until told otherwise).
  Old `build <hash>` seals below resolve via the backup branch + map.
- **Wayland taskbar association, dev flow (2026-09-06)** — Window showed the
  generic Wayland icon in the taskbar while the tray icon rendered fine.
  Root cause: on Wayland the taskbar matches by `appId ↔ .desktop`
  (`dev.souriscg.moonlit` via `app.enableGTKAppId`), and no dev `.desktop`
  existed in the repo; a stale `com.souriscg.MoonLit.desktop` (wrong
  identifier/WMClass, `Exec=MoonLit` pointing nowhere) also conflicted.
  Fix: `app.enableGTKAppId: true` in `tauri.conf.json`,
  `build-aux/dev.souriscg.moonlit.desktop.template` +
  `build-aux/install-dev-desktop.sh` (`pnpm desktop:install`) installing a
  validated dev entry + removing the stale one. Tray is unaffected (explicit
  `TrayIconBuilder` pixels). Verified by user on KDE Wayland.
- **Transparent icon set (2026-09-06)** — `src-tauri/icons/` regenerated from
  `build-aux/moonlit-icon.svg` as fully transparent (rounded artwork, no
  opaque backdrop). `tray-icon.png` currently mirrors the set; the dedicated
  tray asset separation is documented as future work in `07_UI_MOONLIT.md`.
- **HEVC save fix (2026-09-06)** — h265 clips failed to save: NVENC HQ opts
  are now per-codec (`nvenc_hq_opts`: `high` for h264, `main` for hevc) with
  unit tests in `video_quality.rs`.
- **Purge missing clips (2026-09-06)** — New `purge_missing_clips` command +
  gallery button: drops DB rows whose files no longer exist on disk.

## Hash map (old → new, same order/messages/dates)

| Old | New | Subject |
|---|---|---|
| `6965fac` | `ccf9451` | feat(phase1): tray minimize-to-tray + F9 global hotkey + MoonLit glass UI + pausable starfield + i18n es/en |
| `8e4b5c1` | `9a81b9a` | fix(phase1): custom frameless topbar + MoonLit CSS logo + smooth starfield + F9 dedupe |
| `2b99add` | `2f1234e` | chore(license): adopt GPL-3.0-only (required by gpu-screen-recorder sidecar) |
| `c72edba` | `b8f218e` | feat(phase2): rusqlite persistence with relative paths + keyring secrets + settings UI |
| `f8884c6` | `94536d3` | docs: mark phase 2 done in PROGRESS |
| `982890a` | `9f3cc75` | fix(phase2): synced locale source, dialog permission, logo glow containment |
| `1d14c82` | `943d024` | fix(dev): permanent Wayland workaround via .cargo/config env |
| `259dcdb` | `a885c28` | docs: log wayland dev fix in PROGRESS |
| `69fdd4a` | `f051f0c` | fix(phase2): stateless locale from i18n + hardened maximize toggle |
| `3c93467` | `4b14c1e` | debug(phase2): temp maximize flicker logging (to be removed with fix) |
| `4a9d44d` | `75052f0` | fix(phase2): atomic maximize toggle + delayed drag-area maximize |
| `2b2c5e2` | `71848fc` | fix(phase2): let Tauri own drag-area dblclick maximize |
| `502601e` | `bbddf69` | docs: log maximize root cause in PROGRESS |
| `00a70b3` | `ff9e586` | feat(phase3): GSR replay engine + dual audio + record UI (Linux) |
| `d0fec68` | `a44eb46` | feat(phase3): embedded GSR build script + live per-track volumes |
| `94d6d8a` | `f371bba` | feat(phase3): embedded GSR binary + aac tracks |
| `cbb29bd` | `a60d51a` | fix(phase3): stream matching, thumbs, duration restart, slider UX |
| `b0ab0a5` | `4ae01b3` | fix(phase3): exact stream match, thumbs, restart keys, devices, quick-open |
| `efec83a` | `ce54586` | refactor+fix(phase3): OBS-style os/ layout, asset protocol, real durations |
| `2e06904` | `2f075da` | fix(phase3): gallery state+errors, stream debug log, responsive pass |
| `0c6dd61` | `949eaae` | fix(phase3): icon-only gallery, self-clearing errors, build stamp |
| `a9e5377` | `7a2ca1b` | feat(phase3): 3-track layout, mix first (plays everywhere) |
| `e95189c` | `3229e91` | fix(phase3): reveal fallback to openPath(parent) |
| `4f30c08` | `1981222` | fix(phase3): camelCase revert, scroll, title, resize grips |
| `328e911` | `eae081d` | fix(phase3): clip filename as row title, probe cleanup |
| `f290be2` | `a098988` | feat(phase3): Medal bitrate ladder + NVENC HQ recipe + video settings |
