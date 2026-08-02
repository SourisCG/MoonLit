# MoonLit Windows v1 Project Plan

Last updated: 2026-08-02
Status: implementation in progress

## Scope

MoonLit is a local-first Windows x64 clip recorder based on OBS Studio 32.2.1.
This delivery targets a usable Windows executable, portable package and installer.
Cloud sync, accounts, social integrations and Linux support are explicitly deferred.

## Non-Negotiable Decisions

- Windows x64 first; minimum supported Windows build is 19041.
- WGC window capture is the primary backend.
- DXGI monitor capture is the safe fallback.
- No Game Capture, graphics hooks, injection, remote threads, drivers or virtual camera.
- Loss of foreground focus must fail closed to an opaque black frame.
- MKV is the authoritative clip. MP4 is an explicit export/remux result.
- Replay buffer is enabled by default and starts only after a valid game target is ready.
- Audio tracks are separated as: mixed, game, microphone and chat.
- Hardware encoders are selected dynamically from encoders actually available in OBS.
- Fallback order is NVENC, QSV, AMF, then x264, without silently changing the saved preference.
- The local clip library keeps the original file and never performs destructive edits.
- Commits are created per phase; one final push publishes the complete history.
- The legacy Rust/Tauri history is preserved before replacing the GitHub `main` branch.

## Repository Baseline

- OBS base commit: `0052d024f` (`32.2.1`).
- Current MoonLit commit: `08386836a`.
- Current branch: `main`.
- Legacy branch: `legacy/rust-tauri-v2`.
- Legacy tag: `legacy-rust-tauri-v2-2026-08-01`.
- Archive branch: `archive/pre-obs-reset`.
- GitHub `origin/main` still points to the old Rust/Tauri history and will be replaced only at release.

## Implemented Before This Plan

- New OBS-based `main` history with the original Rust/Tauri implementation preserved locally.
- Qt MoonLit dashboard with replay, save, settings and library actions.
- OBS preview, menu, status bar and normal docks hidden by the MoonLit shell.
- Replay buffer enabled by default.
- MKV defaults and automatic remux setting.
- Dynamic display of registered video encoders.
- Windows foreground detector using HWND, PID and process creation time.
- Initial Steam, Epic, GOG and game-directory heuristics.
- Automatic WGC `window_capture` source creation.
- Automatic replay start after target detection.
- Scene item hidden while the target is not foreground.
- Window, monitor and duplicator capture remain available; Game Capture is not built.
- Active CMake targets no longer build graphics hooks, offset helpers or inject helpers.
- Optional QSV load failure no longer blocks the MoonLit startup dialog.
- Build and startup smoke test passed on an NVIDIA RTX 3060 host.
- Current runtime contains no hook, injector or Game Capture artifacts.

## Known Gaps At Start

- MoonLit still uses OBS executable, mutex and configuration identity.
- Existing OBS profiles can bypass MoonLit defaults.
- Detection is path-based and does not handle all launchers, UWP games or HWND recreation.
- WGC health and first-frame readiness are not exposed to the shell.
- Monitor fallback is not connected to the MoonLit state machine.
- The current black behavior hides only the current capture item and can expose other scene sources.
- Game audio is coupled to the video source and global desktop audio can duplicate it.
- Microphone and Discord/chat routing are not managed by MoonLit.
- Encoder listing exists, but startup fallback and codec preference do not.
- Replay save notification still depends on OBS status behavior.
- The library button is a placeholder.
- There is no SQLite repository, thumbnail service, editor or export workflow.
- Tray, login startup, installer, signing and product branding are incomplete.
- Historical hook source directories and stale ignored build artifacts still require release cleanup.
- There are no automated MoonLit tests or a deterministic Windows capture fixture.

## Architecture Target

The MoonLit-specific code should move toward these boundaries:

```text
frontend/moonlit/
  MoonLitController.*
  MoonLitPaths.*
  capture/
  output/
  persistence/
  media/
  services/
  ui/
```

`MoonLitController` owns state transitions and OBS integration. Domain, persistence,
media and query code must not depend on `OBSBasic` so it can be tested separately.

## Phase Ledger

### P0 - Durable Plan And Baseline

Status: complete

Deliverables:

- This document committed to the repository.
- Current behavior, decisions, risks and acceptance criteria recorded.
- Plan updated with commit SHA after every completed phase.

Exit criteria:

- A fresh session can resume from this document without conversation history.

### P1 - Product Identity And Safe Build

Status: in progress

Deliverables:

- MoonLit executable, mutex and configuration namespace.
- Dedicated MoonLit portable and Windows x64 presets.
- Disable OBS updater, first-run streaming wizard and third-party module paths.
- Disable browser, scripting, WebSocket, WebRTC, streaming-only, VLC and virtual camera modules.
- Remove historical hook/injector source and stale build payloads from release staging.
- Add package denylist checks.

Primary files:

- `frontend/OBSApp.cpp`
- `frontend/obs-main.cpp`
- `frontend/utility/platform-windows.cpp`
- `frontend/CMakeLists.txt`
- `CMakePresets.json`
- `cmake/windows/`

Acceptance:

- OBS and MoonLit can run together.
- MoonLit has no idle update/network behavior.
- A clean package contains no forbidden capture artifacts.

Current result: the MoonLit x64 preset, product identity and minimal module
selection are implemented. Portable staging, denylist validation and the
release package remain pending.

### P2 - Output Configuration And Encoder Resolver

Status: pending

Deliverables:

- Versioned MoonLit profile migration.
- Dedicated settings dialog for output, replay, video and encoder preferences.
- Requested/effective encoder separation.
- NVENC, QSV, AMF and x264 resolver.
- Runtime start failure fallback with bounded retries and visible reason.
- Four-track MKV output configuration.

Primary files:

- `frontend/widgets/OBSBasic.cpp`
- `frontend/utility/SimpleOutput.cpp`
- `frontend/utility/AdvancedOutput.cpp`
- `frontend/settings/`
- new `frontend/moonlit/output/`

Acceptance:

- Existing profiles migrate to valid MoonLit settings.
- Unsupported encoders are filtered or fall back without a loop.
- Settings survive relaunch and apply safely while replay is stopped.

### P3 - Robust Capture State Machine

Status: in progress

Deliverables:

- Testable detector with discovery and privacy polling.
- UWP/store/launcher support and manual allow/deny entries.
- Process-family and HWND-rebind handling.
- Dedicated MoonLit scene with black shield, WGC source and monitor fallback.
- WGC first-frame health signal and timeout.
- DXGI monitor fallback and backend status.
- Focus-loss privacy guarantee.

Primary files:

- `frontend/widgets/MoonLitGameDetector.*`
- `frontend/widgets/MoonLitShell.cpp`
- `plugins/win-capture/window-capture.c`
- `plugins/win-capture/duplicator-monitor-capture.c`
- `libobs-winrt/winrt-capture.cpp`

Acceptance:

- Windowed and borderless games capture without hooks.
- WGC failure falls back to monitor capture.
- Alt+Tab, minimize, secure desktop and protected content remain black.
- A recreated game window does not create a stale capture.

Current result: detector identity checks, WGC health, monitor fallback gating,
shield ordering and private runtime sources are implemented. Live windowed,
borderless, Alt+Tab and protected-content testing remains pending.

### P4 - Independent Audio Graph

Status: in progress

Deliverables:

- Independent game process audio source.
- Explicit microphone source and device persistence.
- Discord/chat process source and restart recovery.
- Mixer masks for mixed/game/mic/chat tracks.
- Desktop audio disabled when it would duplicate process audio.

Primary files:

- `plugins/win-capture/audio-helpers.c`
- `plugins/win-wasapi/win-wasapi.cpp`
- `frontend/components/ApplicationAudioCaptureToolbar.cpp`
- `frontend/components/AudioCaptureToolbar.cpp`
- new `frontend/moonlit/capture/`

Acceptance:

- Four saved tracks have distinct expected signals.
- No game audio duplication.
- Device disconnect, Discord restart and microphone changes recover cleanly.

Current result: process-loopback audio now carries PID, HWND and creation-time
identity and is isolated from the window capture source. Four-track routing
and device-recovery testing remain pending.

### P5 - Replay Lifecycle And Local Library

Status: in progress

Deliverables:

- Exact save-path lifecycle signals.
- Dashboard and tray save/error notifications.
- Vendored SQLite with transactional migrations and FTS5.
- Clip ingest, metadata probe, thumbnails and startup reconciliation.
- Library list/grid, search, filters, reveal, import and delete-to-trash.

Primary files:

- `frontend/widgets/OBSBasic_ReplayBuffer.cpp`
- `frontend/utility/RemuxWorker.cpp`
- new `deps/sqlite/`
- new `frontend/moonlit/persistence/`
- new `frontend/moonlit/services/`
- new `frontend/moonlit/media/`
- new `frontend/moonlit/ui/`

Acceptance:

- Every completed replay creates one durable clip record.
- MKV remains authoritative after remux/export.
- Missing files, duplicate imports and thumbnail failures are recoverable.
- Large libraries do not block the UI.

Current result: replay save signaling, local ingest, metadata, thumbnails,
search, reveal, trash and reconciliation foundations are implemented. The
current index is atomic JSON; SQLite/FTS5 and background work remain pending.

### P6 - Basic Editor And Export

Status: in progress

Deliverables:

- Preview, seek and trim handles.
- Non-destructive mute and gain metadata.
- Whole-file remux.
- Fast keyframe-aligned trim.
- Accurate trim/gain export where codec/container compatibility permits.
- `.part` files, cancellation, verification and atomic rename.

Primary files:

- `frontend/components/MediaControls.cpp`
- `frontend/widgets/OBSQTDisplay.cpp`
- `libobs/media-io/media-remux.c`
- `frontend/utility/RemuxWorker.cpp`
- new `frontend/moonlit/media/`
- new `frontend/moonlit/ui/`

Acceptance:

- Original MKV is never modified.
- Cancelled or failed exports leave no corrupt final file.
- Final metadata and duration match the selected range.

Current result: keyframe-aligned MP4 export with trim controls is implemented.
Background export, cancellation, verification and fractional-duration tests
remain pending.

### P7 - Tray, Startup And Product UI

Status: pending

Deliverables:

- MoonLit-only tray actions.
- Close/minimize to tray with explicit Exit.
- Windows login startup under HKCU.
- MoonLit icons, version resources and About dialog.
- Linux and cloud code excluded from this release.

Primary files:

- `frontend/widgets/OBSBasic_SysTray.cpp`
- `frontend/obs-main.cpp`
- `frontend/utility/platform-windows.cpp`
- `frontend/forms/obs.qrc`
- `frontend/cmake/windows/`

Acceptance:

- Startup launches one hidden instance.
- Close-to-tray does not swallow Windows shutdown.
- Explicit Exit cleanly finalizes output.

### P8 - Portable Package, Installer And Release Gate

Status: pending

Deliverables:

- Clean portable ZIP.
- Windows installer.
- Upgrade/uninstall behavior preserving clips and database.
- Signed binaries and installer when certificate is available.
- License/notice bundle, SBOM and SHA-256 checksums.
- Defender scan and forbidden-file audit.

Primary files:

- `cmake/bundle/`
- `cmake/windows/`
- `CMakePresets.json`
- `.github/workflows/`
- new release scripts under `.github/scripts/`

Acceptance:

- Fresh VM install, upgrade and uninstall pass.
- End-user package has no PDBs, hooks, injectors or virtual camera.
- Signatures verify, or the build is explicitly marked unsigned.

### P9 - Test Matrix And GitHub Publication

Status: pending

Deliverables:

- Unit tests for resolver, detector, paths, migrations and trim rules.
- SQLite/media tests.
- Deterministic Windows capture fixture.
- Hardware/manual matrix documentation.
- Per-phase commits and final plan update.
- Legacy refs pushed first.
- New `main` pushed with `force-with-lease`.

Acceptance:

- Clean build and smoke test pass.
- Runtime starts and exits cleanly.
- No forbidden runtime artifacts.
- GitHub contains both legacy recovery refs and the complete MoonLit main history.

## Explicitly Deferred

- Cloud storage.
- User accounts.
- Social publishing.
- Linux capture and packaging.
- Advanced timeline editor.
- Effects, overlays, waveform and transitions.
- Multi-user sessions.

## Execution Log

| Date | Phase | Result | Commit |
|---|---|---|---|
| 2026-08-02 | P0 | Plan document created and committed | 2bc158d86 |
| 2026-08-02 | P1 | Product identity, Windows x64 preset and clip-core foundation implemented; packaging pending | bbdbcf1c7 |
| 2026-08-02 | P3/P4 | Capture privacy/lifecycle and process-audio foundations implemented; runtime smoke pending | 97f3b714a |
| 2026-08-02 | P5/P6 | Replay library, metadata, thumbnails, search and export UI foundations implemented; SQLite/background work pending | 9fbd8101b |

## GitHub Release Procedure

The local GitHub CLI is currently unauthenticated. Before publication:

1. Authenticate `gh`.
2. Inspect `git status`, `git diff`, `git log` and all included commits.
3. Push `legacy/rust-tauri-v2`, `archive/pre-obs-reset` and the legacy tag.
4. Verify those refs remotely.
5. Push MoonLit `main` with `--force-with-lease` against the known old `origin/main` SHA.
6. Verify remote `main`, tags and branch history.

No force push is performed until all legacy recovery refs are confirmed on GitHub.
