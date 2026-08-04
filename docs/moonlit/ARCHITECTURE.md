# MoonLit ARCHITECTURE.MD — Session Context 2026-08-04

> Continuity block: paste this file (or this section) at the start of future
> sessions so the context survives chat compaction. It mirrors the state
> after commits `bbbe2e622..618d70d45` on top of release `v1.0.0`
> (`a6cdd107a`).

Base: OBS Studio 32.2.1 fork (`0052d024f`), C++17, Qt6, CMake 3.28-3.30,
VS 17 2022 (Windows preset `windows-moonlit-x64`). Product identity:
`MOONLIT_BUILD` (`cmake/common/bootstrap.cmake:44`), exe `MoonLit`.
Core lib: `moonlit-clip-core` STATIC (alias `MoonLit::ClipCore`) — Qt6
Core/Gui + FFmpeg(avcodec/avformat/avutil/swscale/swresample) + vendored
sqlite3 3.53.4 (FTS5, THREADSAFE=1, OMIT_LOAD_EXTENSION, DQS=0).

Guardrail: **no `#ifdef _WIN32` inside `moonlit-clip-core`**. OS deps only in
`platform/PlatformServices_win.cpp` / `PlatformServices_linux.cpp`, selected
by CMake. CI (`moonlit-ci.yml`) compiles the core standalone on Ubuntu GCC and
Windows MSVC to enforce this.

## Capture state machine (implemented, refactored)

`frontend/moonlit/capture/`:
- Core (pure, tested): `CaptureTypes.hpp` (`BackendKind{Wgc,DxgiMonitor,
  PipeWire,Xshm}`, `CaptureTarget{name,windowClass,executablePath,processId,
  creationTimeNs,WindowHandle(std::variant<uintptr_t,void*,std::string>)}`,
  `CaptureHealth`), `CaptureStateMachine` — pure tick decision engine
  (`TickAction`: ConfigureRetry/StatusInitializing/WgcReady/StartReplay/
  ResetReplayFailures/MonitorFallbackReady/FallbackBlocked/TryMonitorFallback).
- Frontend: `CaptureController` (QObject; owns detector, 250 ms health timer,
  1 s retries, 5 s WGC timeout, replay lifecycle flags) + `ICaptureBackend`
  + `ICaptureHost` (implemented by OBSBasic: `moonlitCurrentScene()`,
  `replayBufferActive()`, `startReplayBuffer()`, `stopReplayBuffer()`,
  `activeConfig()`, inline `isClosing()`).
- `WindowsCaptureBackend`: WGC `window_capture` (method=2,
  `moonlit_hwnd/process_id/creation_time`, `moonlit_require_wgc=true`),
  4 audio sources (track mixers `1<<0..3`), black `color_source` shield,
  DXGI `monitor_capture` fallback with `monitor_id`, noise suppression
  (`noise_suppress_filter` rnnoise -40 dB + `noise_gate_filter`
  open/close -40/-45, attack 10/hold 20/release 50).
- `MoonLitShell.cpp` is now thin: dashboard/library wiring, ReplayBuf signal
  forwarding to the controller, ICaptureHost impls. Hotkeys via
  `frontend/moonlit/hotkeys/HotkeyManager` (obs_hotkey_register_frontend
  "MoonLit.SaveClip", default `{0, OBS_KEY_F8}`, queued UI-thread action;
  delivery is engine GetAsyncKeyState polling — no hooks, anti-cheat safe).
- Replay save: proc_handler "save" on replay_buffer output →
  `ReplayClipSaved(path)` → library ingest. AutoRemux compiled out under
  MOONLIT_BUILD (MKV authoritative).

Linux plan (deferred, needs a machine): `PipeWire`/`Xshm` backends behind
`ICaptureBackend`; portal hotkeys via `obs-nix-wayland.c` vtable;
`linux-pipewire/linux-capture/linux-pulseaudio/linux-alsa` plugins already in
the allowlist (`plugins/CMakeLists.txt`).

## Platform services

`frontend/moonlit/platform/IPlatformServices.hpp`: `revealInFileManager`,
`setLoginStartup`, `isLoginStartupEnabled`, `setWorkerThreadPriority`,
`capabilities{processLoopbackAudio,secureDesktopHandling}`; factory
`IPlatformServices::create()` defined in each per-platform impl (CMake
selects). Windows: explorer /select, HKCU Run, SetThreadPriority
(BELOW_NORMAL). Linux: xdg-open parent dir, XDG autostart desktop entry,
setpriority(10), capabilities{false,false}. Adopted by
`MoonLitLibraryWidget::revealSelected` and `MoonLitSettingsDialog`
(autostart checkbox).

## SQLite (user_version 3)

`SqliteClipRepository` (single logical writer; opened once by
`MoonLitLibraryWidget` and shared with the export queue — no open() from
worker threads):
- open(): `sqlite3_busy_timeout(db,5000)` + `journal_mode=WAL;
  synchronous=NORMAL; wal_autocheckpoint=1000; journal_size_limit=-1;
  cache_size=-64000`; close(): `PRAGMA optimize`.
- Tables: `clips` (v2 fields + edits) + `clips_fts` (external-content FTS5,
  unicode61, triggers with 'delete' command) + v3 `timelines(id, name,
  segments JSON, created_at, updated_at)` + `export_jobs(id AUTOINCREMENT,
  kind trim|timeline, params JSON, state queued|running|done|failed|
  cancelled, progress, error, created_at, finished_at)`.
- migrate(): fresh → 3; v1 JSON index → import + rename `.migrated`;
  v2 → v3 runs `INSERT INTO clips_fts(clips_fts, rank) VALUES('rebuild', 0)`.
- reconcile(): one `BEGIN IMMEDIATE` transaction; phase 1+2 refresh
  missing/restored, phase 3 discovers orphan media files in `clips/`
  (`ReconcileSummary{scanned,nowMissing,restored,discovered}`).
- Export jobs API: `enqueueExportJob/updateExportJob/listExportJobs/
  failInterruptedExportJobs` (running→failed at startup).

## Threading

- `ClipJobs` (repo + probe/thumbnails/search/timelines CRUD) on
  `workerThread_`; `ExportQueue` (serial trim/timeline exporter, persisted
  jobs, `cancelCurrent()` atomic, `THREAD_PRIORITY_BELOW_NORMAL` via
  IPlatformServices) on `queueThread_`. Both share the repository pointer;
  SQLite FULLMUTEX serializes. Widget destructor drains both threads.
- Trim job params JSON: `{clipId,startMs,endMs}`; timeline job:
  `{timelineId}`. Trim uses clip-record mute/gain; destination
  `MoonLitPaths::exportPath(id,"mp4")`.

## Timeline editor

- Core model: `editor/Timeline.hpp` — `TimelineSegment{clipId,
  sourceStartMs, sourceEndMs(-1=end), timelineStartMs, gainDb, muted}`,
  `TimelineProject{id UUID, name, segments}`, `isValid` (≥100 ms segments),
  `recomputePositions()` (cumulative), JSON via `timelineToJson/FromJson`;
  `Q_DECLARE_METATYPE` lives in Timeline.hpp (single definition point).
- `FfmpegTimelineExporter` (`TimelineExportRequest/Result`): decode-encode
  unified h264 (libx264, time_base 1/90000, gop 60) + shared AAC encoder;
  per-segment decoder/resampler via `AudioEncodeStream::initializeSegment`;
  **frame timestamps are timeline-absolute** (`setFramePtsOffset(
  timelineStartMs - sourceStartMs)` + `setOutputOffset(0)`) so delayed
  encoder packets stay monotonic (mp4 muxer rejects dts regression);
  `drain(flushEncoder=segmentIndex==last)`; `.part` + duration verification
  (tolerances 500 ms/10 s) + atomic rename; cooperative cancel.
- `FFmpegPipeline.hpp` (shared internal header): RAII deleters, OutputContext,
  `AudioEncodeStream` (offset pinning, per-segment reinit, drain with endUs
  and optional encoder flush), `verifyOutput`, `appendExportLog`,
  `samePath/normalizedPath`, `packetTimestampMs`/`mediaDurationMs`.
- UI: `TimelineStrip` (proportional segments + thumbnails, click select,
  edge-drag trim ≥100 ms, middle-drag reorder) + `MoonLitTimelineEditor`
  (name, add clip from library combo, per-segment mute/gain, save/export/
  back/new). Library gets a QStackedWidget (library page + editor) and a
  "Timeline" button; `ClipJobs` timeline slots + signals wired.

## Build & packaging

- Presets: `windows-moonlit-x64` (VS17 2022, build_moonlit_v1_x64),
  `ubuntu-moonlit-x64` (Ninja, build_moonlit_ubuntu, same ENABLE_* offs,
  `MOONLIT_BUILD=true`, OBS_VERSION_OVERRIDE=1.0.0).
- Plugins under MOONLIT_BUILD: common image-source obs-ffmpeg obs-filters
  obs-outputs obs-transitions obs-x264; Windows += obs-nvenc obs-qsv11
  win-capture win-wasapi; Linux += linux-pipewire linux-capture
  linux-pulseaudio linux-alsa.
- Standalone core configure (CI smoke): `cmake -S frontend/moonlit -B
  build-core` — needs `MOONLIT_REPO_ROOT` handling + `enable_language(C)`
  in deps/sqlite + `enable_testing()` before test subdir + swresample in
  FFmpeg COMPONENTS. Windows CI downloads obs-deps/qt6 zips to `.deps/` and
  uses CMAKE_PREFIX_PATH.
- Packaging Win (unchanged): package.ps1 → staging → audit → portable ZIP
  (`portable_mode` marker) → NSIS per-user → sign → SHA256SUMS.

## Tests (44, ctest `moonlit-tests`)

SQLite (round trip, reopen, FTS updates, reconcile missing/restored/
discovered, v2→v3 migration + FTS rebuild, busy_timeout writer contention,
export jobs round trip + interrupt recovery), resolver, export math,
JSON import (v3), media (strip/preview, trim/full/MKV exports, audio edits),
paths, capture state machine (6 cases + target validity), timeline
(model/JSON/repository) and timeline export (concat duration, trim+mute,
cancel cleanliness, missing source). Shared generator in
`test/moonlit/TestMedia.hpp` (2 s 320x180 h264+aac, optional MKV+B-frames).

## Known notes

- ExportQueue `shutdown()` blocks (BlockingQueuedConnection) until the
  active export finishes/cancels — do not call from the worker thread.
- `Q_DECLARE_METATYPE` must stay only in Timeline.hpp (MSVC C2908 otherwise).
- Timeline export requires audio in every segment when the first has it.
- Discovered orphan clips get no thumbnail until re-ingested.
- Session next steps: run new CI workflow on GitHub; manual matrix rows
  C/A/L for capture, audio and timeline editing.
