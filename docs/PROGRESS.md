# PROGRESS — MoonLit build log

Single source of truth for phase status. Updated at the end of every phase.
Details per phase live in `ROADMAP_PHASES.md`; technical specs in `01_*`–`08_*`.

| Phase | Scope | Status | Commit | Acceptance |
|---|---|---|---|---|
| 0 | Bare scaffold (Tauri v2 + React-TS + Tailwind v3) + `docs/` spec | ✅ done | `6965fac` (squashed in) | `pnpm build` + `cargo check` green |
| 1 | Tray + F9 hotkey + glass UI + starfield + i18n ES/EN | ✅ done | `6965fac` | F9 fires globally, tray hide/show works |
| 1-fixes | Frameless custom topbar + MoonLit CSS logo + smooth starfield + F9 dedupe | ✅ done | `8e4b5c1` | Single-count verified by user, no tray glitch |
| license | GPL-3.0-only (required by gpu-screen-recorder) | ✅ done | `2b99add` | Verbatim LICENSE + metadata + README |
| 2 | rusqlite persistence (relative paths) + keyring secrets + settings UI | ✅ done | `c72edba` | Awaiting user manual test |
| 3 | Capture engine (GSR Linux + WGC Windows, dual audio) | ⬜ pending | — | F9 → `.mp4` <1s, 2 audio tracks |
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
