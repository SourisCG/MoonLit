# MoonLit Windows v1 Manual Test Matrix

Every row below must be executed on the target hardware (host or clean VM)
with a signed package. Results go in the right-hand column.

## Build And Startup

| ID | Check | Result |
|---|---|---|
| S1 | `ctest --test-dir build_moonlit_v1_x64 -C RelWithDebInfo` passes (11 tests) | PASS 2026-08-02 (11 tests, 0 failures) |
| S2 | `pwsh -NoProfile -File .github/scripts/audit.ps1` passes on the package staging (no forbidden artifacts, all binaries signed) | PASS 2026-08-02 (52 binaries, 0 unsigned) |
| S3 | MoonLit starts from the portable ZIP and from the installer | PASS 2026-08-02 (installer install/uninstall round trip ok) |
| S4 | Dashboard shows "Buffer detenido", no error dialogs, no crash handler windows | PASS 2026-08-02 |
| S5 | Windows close (X) hides to tray; the tray menu shows Guardar clip / Abrir biblioteca / Ajustes / Salir | PENDING (manual click) |
| S6 | Tray "Salir" quits cleanly; Windows shutdown while hidden quits instead of hiding | PENDING (manual) |
| S7 | No CodeIntegrity 3076/3077 events and no Defender detections after starting the app | PASS 2026-08-02 (0 blocks, 0 detections) |

## Capture And Replay

Requires a real game window (windowed and borderless).

| ID | Check | Result |
|---|---|---|
| C1 | Windowed game is detected and replay starts automatically | |
| C2 | Borderless game is detected; WGC window capture shows the game | |
| C3 | Alt+Tab away: preview goes black (shield), replay keeps running | |
| C4 | Alt+Tab back: capture resumes without reconfiguration | |
| C5 | Close the game: replay stops, capture cleared, dashboard returns to "Sin juego detectado" | |
| C6 | Protected content (DRM video) stays black and does not crash | |
| C7 | DXGI monitor fallback engages when WGC cannot capture (verify via "DXGI monitor fallback" status) | |

## Audio

| ID | Check | Result |
|---|---|---|
| A1 | Four saved tracks contain distinct signals: 1 mixed, 2 game only, 3 mic only, 4 chat only | |
| A2 | No game audio duplication when desktop audio is also present | |
| A3 | Microphone unplug/replug recovers on next session | |
| A4 | Discord restart: chat track recovers via exe-based capture | |

## Library And Export

| ID | Check | Result |
|---|---|---|
| L1 | Saving a replay creates one clip with thumbnail, probe metadata and FTS searchable title | |
| L2 | Search matches title tokens; filter Todos/Disponibles/Faltantes works | |
| L3 | Import an external MKV: file is copied into the clips folder and indexed without duplicates | |
| L4 | Reveal opens Explorer with the file selected; Abrir plays the clip | |
| L5 | Enviar a papelera moves the file to the recycle bin and removes the record | |
| L6 | Export a trim range: MP4 duration matches the range within keyframe tolerance; no `.part` file remains | |
| L7 | Cancel mid-export leaves no final file and no `.part` file | |
| L8 | Delete a clip file on disk and reconcile: the clip is flagged as missing, then restored when the file returns | |

## Persistence And Migration

| ID | Check | Result |
|---|---|---|
| P1 | Existing install with clips: first launch migrates `index.json` to `MoonLit.db` and renames it to `index.json.migrated` | |
| P2 | Restart keeps library contents; `PRAGMA user_version` is 2 | |
| P3 | Upgrade install over the previous version keeps clips and database | |
| P4 | Uninstall keeps `%APPDATA%\MoonLit` and `%LOCALAPPDATA%\MoonLit` (clips, database) | |

## Login Startup

| ID | Check | Result |
|---|---|---|
| R1 | "Iniciar MoonLit con Windows" writes the HKCU Run entry with `--minimize-to-tray` | |
| R2 | After login the app starts hidden in the tray; tray "Mostrar" reveals it | |
| R3 | Unchecking the option removes the Run entry | |
