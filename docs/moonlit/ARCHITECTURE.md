# MoonLit ARCHITECTURE.MD — Session Context 2026-08-04

> Continuity block: paste this file (or this section) at the start of future
> sessions so the context survives chat compaction. It mirrors the state
> after commits `bbbe2e622..895c6d8be` on top of release `v1.0.0`
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
  (`portable_mode` marker) → NSIS per-user → sign → SHA256SUMS. Orden
  corregido: los binarios se firman ANTES del audit y del ZIP (antes el
  audit corría sobre staging sin firmar y su `exit 1` no propagaba — por eso
  había que firmar el rundir a mano). `-SkipSign` produce artifacts sin
  firmar (audit con `-AllowUnsigned`); `-CertPath`/`-PasswordFile` permiten
  firmar con un cert distinto (CI usa el secret `MOONLIT_PFX_B64`).
- Workflow `moonlit-package.yml`: **solo manual** (`workflow_dispatch`, con
  input de versión, default 0.1.0) — nada corre en pushes. En `main`
  compila el frontend Windows completo (preset `windows-moonlit-x64`,
  obs-deps+Qt6 descargados, NSIS por winget) y sube
  `MoonLit-0.1.0-x64.zip` + `MoonLit-0.1.0-Setup.exe` + `SHA256SUMS.txt`;
  ejecutado sobre un tag (p. ej. `0.1.0`) además publica el release con los
  artifacts del mismo run y un body markdown (descarga, funciones,
  checksums). Si el secret de firma existe, el cert (self-signed dev) se
  importa a `LocalMachine\Root` del runner y los binarios salen firmados;
  sin secret, el job no falla (artifacts sin firmar). Versión fijada en
  `OBS_VERSION_OVERRIDE=0.1.0` (presets moonlit), About, NSIS y
  package.ps1.
- El cert de desarrollo es self-signed: en otras máquinas SmartScreen sigue
  mostrando "Editor desconocido" (la azul); un cert de CA real (Azure
  Trusted Signing / OV-EV) solo cambia el secret en el workflow.

## Tests (45, ctest `moonlit-tests`)

SQLite (round trip, reopen, FTS updates, reconcile missing/restored/
discovered, v2→v3 migration + FTS rebuild, busy_timeout writer contention,
export jobs round trip + interrupt recovery), resolver, export math,
JSON import (v3), media (strip/preview, trim/full/MKV exports, audio edits),
paths, capture state machine (6 cases + target validity), timeline
(model/JSON/repository), timeline export (concat duration, trim+mute,
cancel cleanliness, missing source) and `export_queue_shutdown_completes`
(regression: shutdown must not deadlock). Shared generator in
`test/moonlit/TestMedia.hpp` (2 s 320x180 h264+aac, optional MKV+B-frames).

## Session 2026-08-04 (night) — Estabilidad, UI Dracula×MoonLit, icono

Commits: `f2fac1087`, `0f1431030`, `895c6d8be`, `3696b1a64`.

- **Locale CWD fix**: `obs-main.cpp` (MOONLIT_BUILD) fija el CWD al dir del
  ejecutable (`SetCurrentDirectoryW`) — `GetDataFilePath`/`OBS_DATA_PATH`
  son rutas relativas al CWD; esto replica el doble-clic de Explorer desde
  cualquier lanzamiento. Elimina "Failed to find locale/en-US.ini".
- **Mixer layout**: `MoonLitMixer` no tenía layout (`layout()->addItem`
  sobre nullptr) → crash al salir y al detectar juego (dump 2026-08-02).
  Ahora `QVBoxLayout` en el ctor.
- **ExportQueue shutdown deadlock**: la cola se auto-destruía en su propio
  worker (deferred delete al terminar el hilo) → `~ExportQueue → shutdown()`
  → `wait()` sobre sí mismo → cierre colgado. Fix: el widget es dueño
  (`delete` explícito tras `shutdown()`), `shutdown()` con guard de
  self-thread (`QThread::currentThread() == &workerThread_` → return) y sin
  `connect(finished → deleteLater)`. Regresión cubierta por
  `export_queue_shutdown_completes`.
- **Dashboard REC button**: el box-model de Qt QSS **reescribe los
  min/max del widget** (min-height/max-height computados del font/borde) →
  el layout apilaba el texto del estado sobre el botón. Solución:
  `MoonLitRecordButton` — QWidget pintado a mano (120x120 exactos, sin
  stylesheet; `Q_OBJECT` en el .cpp + `#include "MoonLitDashboard.moc"`).
- **TimelineStrip**: rects por pasada acumulativa local (immune a
  `timelineStartMs` stale / `sourceEndMs=-1`) + gap de 2px entre segmentos.
- **Tema Dracula×MoonLit**: `frontend/widgets/MoonLitTheme.hpp` — fuente
  única de la paleta (`#1e1f29/#282a36/#343746/#44475a/#f8f8f2/#6272a4/
  #7667f5/#8b7cf9/#ff5555/#50fa7b/#ffb86c`) usada por dashboard,
  biblioteca, editor de timeline, mixer y strip (QSS con `.arg()`).
- **Icono oficial** (repo SourisCG/MoonLit-Page): luna creciente con
  gradiente `#ef4444→#3b82f6` + triángulo play.
  - `frontend/forms/images/moonlit-icon.png` = favicon oficial 256px.
  - `moonlit-icon.svg` recreado con **dos subpaths de bobinado opuesto**
    (regla nonzero, anillo) — NO usar `fill-rule="evenodd"` (QtSvg lo
    renderiza mal). Verificado con QSvgRenderer (hueco vacío + rojo/azul).
  - `frontend/cmake/windows/MoonLit.ico` = favicon.ico oficial (6 tamaños).
  - `OBSApp.cpp`: `QApplication::setWindowIcon` (PNG) — los diálogos sin
    parent (crash/safe-mode) usan el icono de aplicación; sin esto mostraban
    el "Q" azul por defecto de Qt.
  - `obs-main.cpp`: título/mensaje del diálogo de crash → "MoonLit has
    crashed!" (MOONLIT_BUILD).
  - `en-US.ini`: `CrashHandling.*` → MoonLit.
  - `.ui` (OBSAbout/OBSBasic/OBSPermissions): pixmap/icon OBS → moonlit.
  - NSIS `moonlit.nsi`: `Icon`/`UninstallIcon` con MoonLit.ico (define
    `MOONLIT_ICON`, fallback relativo).
  - Las ramas no-MOONLIT conservan obs.png a propósito.

## Session 2026-08-05 — Control de audio de entrada/salida

- **Volumen físico de dispositivos** (`frontend/moonlit/ui/EndpointVolume.{hpp,cpp}`):
  wrapper RAII de `IMMDeviceEnumerator` + `IAudioEndpointVolume` — resuelve el
  endpoint por id (o el default del sistema; entrada → `eCommunications`,
  salida → `eConsole`, igual que win-wasapi) y expone volumen escalar + mute.
  Se usa en **Ajustes → sección Audio**: sliders 0-100 + % + mute junto a
  "Microfono (entrada)" y "Audio de escritorio (salida)". Aplicación
  inmediata (es volumen del SO, lo que se oye); re-resolución al cambiar el
  combo o abrir el diálogo. El balance COM es pareado (CoInitializeEx con
  S_OK → CoUninitialize en close).
- **Mezclador siempre visible y persistido**: las fuentes mic/chat/desktop se
  crean **una vez en el ctor del backend** (antes se recreaban en cada
  attach); `removeGameAudio()` solo limpia el audio del juego; el add al
  scene es idempotente (`ensureAudioItems`). El Mezclador muestra las 4
  pistas siempre (filas placeholder deshabilitadas "X (sin fuente)" cuando
  no hay source). Niveles persistidos en config (`MoonLit.MixerVolume*` /
  `MoonLit.MixerMute*`, defaults 100/false registrados en el ctor del
  backend) y re-aplicados al crear cada fuente (`applyPersistedMixerSettings`
  en ctor + attach). `refreshMixer()` también en `CaptureController::start()`.
- **Bug de Qt encontrado (importante)**: widgets creados con la ventana
  oculta nacen **hidden** (`WA_WState_Hidden`) porque Qt marca los hijos de
  padres no visibles. `QWidgetItem::isEmpty()` los reporta vacíos →
  `sizeHint() == 0` → el QVBoxLayout colapsa el mixer a altura 0 (y las
  filas nuevas tras `show()` seguían ocultas). El síntoma es un widget con
  `sizeHint()==0` aunque sus hijos midan bien. Fix: `show()` explícito en
  cada widget de la fila al crearla. No afecta al layout (los hijos se
  muestran con el padre); es el idiom estándar de Qt.

## Session 2026-08-05 (night) — Modo manual: pantalla completa + selección de proceso

- **Modos de captura** (`CaptureController::CaptureMode`): `Auto`
  (detector), `Fullscreen` (monitor primario entero, sin juego) y `Manual`
  (proceso fijado por el usuario, detector bloqueado).
  - `setFullscreenMode(bool)`: frena el detector, `attachFullscreen()`
    (monitor_capture DXGI del monitor primario, sin shield) y el buffer
    arranca vía la rama `monitorFallback` del state machine (sin pausa por
    Alt+Tab; pista 2 silenciosa). Al desactivar: stop del buffer + labels
    reseteados + detector vuelve.
  - `selectGame(MoonLitTarget)`: `configure()` directo (WGC + audio del
    proceso en pista 2); el detector se detiene y el health tick vigila la
    vida del proceso (`WindowsProcessUtil::processAlive`); si muere →
    `manualTargetLost()`: stop buffer, clear, vuelve a Auto.
- **`WindowsProcessUtil.{hpp,cpp}`**: helpers compartidos (extraídos del
  detector): `readWindowTarget`, `processAlive`, `enumerateTopLevelTargets`
  (EnumWindows + dedupe por proceso), `isIgnoredExecutable`. El matching
  puro de `GameList` vive en `GameListMatch.hpp` (core, testeable).
- **`MoonLitGamePickerDialog`**: lista filtrable de ventanas visibles
  (proceso — título), doble click o "Capturar", checkbox "Recordar este
  juego" → `MoonLit.GameList` (config, lista por líneas). La lista se
  edita en Ajustes ("Juegos recordados") y el detector la aplica en
  `isLikelyGame` (además de los paths de launchers).
- **OBS 32 gotcha**: `monitor_capture` identifica el monitor por
  `monitor_id` (string, p. ej. `\\.\DISPLAY1`), NO por índice — sin el id
  queda "DUMMY" y nunca captura. `attachFullscreen` resuelve el primario
  con `MonitorFromPoint` + `GetMonitorInfoW` → `szDevice`.
- **`reveal()` sin shield** ocultaba el capture item (comportamiento del
  path WGC donde el shield siempre existe) → en pantalla completa la fuente
  perdía frames tras el primer reveal y el buffer quedaba en
  "inicializando". Fix: sin shield, `reveal()` es no-op.
- Verificado: fullscreen → buffer activo → clip h264 1280x720 con audio
  real; selección manual de Brave (WGC + process loopback) → clip 18.6 s.

## Known notes

- ExportQueue: el widget dueño llama `shutdown()` + `delete`; `shutdown()`
  es seguro desde cualquier hilo (self-thread → no-op).
- `Q_DECLARE_METATYPE` debe vivir solo en Timeline.hpp (MSVC C2908).
- Timeline export requiere audio en cada segmento cuando el primero lo tiene.
- Los huérfanos descubiertos no tienen thumbnail hasta re-ingest.
- Qt QSS reescribe min/max del widget: no ponerle stylesheet a widgets con
  `setFixedSize` crítico; pintarlos a mano.
- `QApplication::setWindowIcon` (PNG) es necesario para los diálogos sin
  parent; el SVG solo para la ventana principal/tray.
- Widgets creados con la ventana oculta nacen hidden → `QWidgetItem` da
  `sizeHint 0` y colapsa layouts: llamar `show()` explícito al crearlos.
- Los sliders del Mezclador usan `setFixedHeight` (20/22/28) para que las
  filas sean robustas ante hints QSS raros, además del `show()`.
- Siguiente: correr `moonlit-ci.yml` en GitHub; matriz manual C/A/L/P/R
  (con juego real); verificar packaging P8 con los fixes.
