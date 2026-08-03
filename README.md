# MoonLit

Local-first Windows x64 clip recorder based on OBS Studio 32.2.1. Capture your
game, keep a searchable local library and export trimmed clips — no accounts,
no cloud, no streaming.

## Features

- **Game detection and capture**: automatic target detection with Windows
  Graphics Capture (WGC), a black privacy shield on focus loss and a safe
  DXGI monitor fallback. No hooks, no injection, no virtual camera.
- **Replay buffer**: MKV clips with four audio tracks — mixed (desktop
  audio), game, microphone and chat. Clip with **F8** from inside the game,
  with a sound cue, or from the dashboard/tray.
- **Local library**: SQLite with FTS5 search, thumbnails, filters
  (Todos/Disponibles/Faltantes), external file import, reveal in Explorer,
  recycle-bin delete and startup reconciliation.
- **Clip editor**: frame-strip preview with seek, draggable trim handles,
  mute and gain. Keyframe-aligned MP4 export in the background with progress,
  cancellation and duration verification.
- **Audio**: desktop audio capture (what comes out of your speakers or
  headphones), microphone and chat channels with a compact mixer (volume +
  mute per source), device selection menus, and Krisp-style noise suppression
  (RNNoise + noise gate) on the microphone.
- **Tray and startup**: close-to-tray, minimize-to-tray, login startup with
  `--minimize-to-tray`, MoonLit branding throughout.

## Requirements

- Windows 10 build 19041 or later, x64.
- A game-capable GPU for hardware encoders (NVENC, QSV or AMF); x264 is the
  software fallback.

## Install

Two options (see the latest release at
<https://github.com/SourisCG/MoonLit/releases>):

- **Installer** (`MoonLit-*-Setup.exe`): per-user NSIS install, no admin
  needed. Installs to `%LOCALAPPDATA%\Programs\MoonLit`. Uninstalling keeps
  your clips, database and configuration.
- **Portable** (`MoonLit-*-x64.zip`): extract anywhere and run
  `bin\64bit\MoonLit.exe`. A `portable_mode` marker keeps configuration and
  data (`config/`, `MoonLitData/`) next to the extracted ZIP.

> Signing: binaries are signed with the self-signed "MoonLit Development"
> certificate. On machines other than the build host, install that
> certificate into Trusted Root/Trusted Publisher first (see
> `docs/moonlit/MANUAL_MATRIX.md`) or expect the SmartScreen "Unknown
> publisher" warning.

## Usage

- **Dashboard**: big record button starts/stops the replay buffer; "Guardar
  clip" saves the last N seconds; recent clips open the library.
- **F8** saves a clip from inside the game (only when the replay buffer is
  active).
- **Library**: thumbnail grid with search and filters; select a clip to open
  the editor (preview, trim, mute/gain) and export MP4.
- **Settings**: encoder (every registered OBS codec), replay length, audio
  tracks, recording folder, microphone and desktop-audio devices, chat
  executable, noise suppression, clip sound and login startup.

### Data locations

| Scope | Location |
|---|---|
| Clips (MKV) | `%LOCALAPPDATA%\MoonLit\clips` |
| Database | `%LOCALAPPDATA%\MoonLit\MoonLit.db` |
| Exports (MP4) | `%LOCALAPPDATA%\MoonLit\exports` |
| Configuration | `%APPDATA%\MoonLit\obs-studio` |
| Portable (ZIP) | `config/` and `MoonLitData/` next to the ZIP |

## Build from source

Prerequisites: CMake 3.28+, Visual Studio 2022 (MSVC), the OBS Windows x64
deps and Qt6 deps in `.deps/` (see the OBS build docs).

```powershell
cmake --preset windows-moonlit-x64
cmake --build build_moonlit_v1_x64 --config RelWithDebInfo --target obs-studio -- /m:1 /v:minimal
```

The app runs from `build_moonlit_v1_x64\rundir\RelWithDebInfo\bin\64bit\MoonLit.exe`.

### Tests

```powershell
cmake --build build_moonlit_v1_x64 --config RelWithDebInfo --target moonlit-tests
$env:PATH = ".deps\obs-deps-2026-07-15-x64\bin;.deps\obs-deps-qt6-2026-07-15-x64\bin;$env:PATH"
ctest --test-dir build_moonlit_v1_x64 -C RelWithDebInfo --output-on-failure
```

### Packaging and signing

```powershell
pwsh -NoProfile -File .github\scripts\sign.ps1      # sign rundir binaries
pwsh -NoProfile -File .github\scripts\audit.ps1     # release-gate audit
pwsh -NoProfile -File .github\scripts\package.ps1   # ZIP + installer + checksums
pwsh -NoProfile -File .github\scripts\matrix-smoke.ps1
```

## Project documentation

- `docs/moonlit/PROJECT_PLAN.md` — product plan, phase ledger and execution log.
- `docs/moonlit/MANUAL_MATRIX.md` — manual test matrix and signing procedure.

## License

MoonLit is free software under the **GNU General Public License v2** (see
`frontend/data/license/gplv2.txt`), like its OBS Studio base. The vendored
SQLite amalgamation and FTS5 sources are in the public domain
(`deps/sqlite/`).
