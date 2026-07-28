# MoonLit AI Context

Updated: 2026-07-28 (ReplayBackend v1 portable core)

## Project Overview

MoonLit is a Windows-first game clip recorder with GPU acceleration and advanced audio mixing, inspired by Medal and OBS. Linux version planned for future release.

## Current State

### Phase: ReplayBackend v1

**Foundation and bootstrap status**:
- Canonical `ReplayBackend` contract is connected to Tauri runtime
- FakeBackend covers sources, capabilities and start/save/stop without hardware
- Windows backend is an explicit unavailable boundary until WGC/NVENC
- Comprehensive documentation created (7 documents)
- NSIS installer configuration is not yet verified
- Windows baseline remains compilable and launch-tested on the RTX 3060 workstation

**⏳ Phase 1 Pending (Windows Required)**:
- Implement WindowsCaptureBackend (Windows.Graphics.Capture)
- Implement WindowsAudioMixer (WASAPI)
- Implement GPU encoders (NVENC/AMF/QuickSync)
- Implement HotkeyService (Win32 API)
- Implement GameDetector (process/window enumeration)
- Testing and validation on Windows 10/11

**Status**: Contract, fake flow and portable GOP replay core migrated; native capture implementation has not started

### ReplayBackend v1 Changes (2026-07-28)

- `traits.rs` now contains one `ReplayBackend` contract and canonical DTOs.
- `recorder.rs` owns a bounded actor, snapshots, transitions and events.
- `backends/fake.rs` is the only fake capture implementation and writes an explicit simulation manifest.
- `backends/gsr.rs` is compiled only on Linux and uses the same contract.
- `backends/windows.rs` exposes an unavailable native boundary without false capabilities.
- `replay.rs` owns a GOP-aware encoded packet window and only saves clips from a decodable keyframe.
- Tauri no longer registers GSR-specific or external executable commands.
- `src/lib/capture/client.ts` centralizes capture IPC and Vitest covers its command payloads.
- No encoded frame data crosses Tauri IPC; WGC/NVENC remains next.

### Windows Bootstrap Verification (2026-07-28)

- Environment: Windows 11 Pro x64, Rust 1.97.1 MSVC, WebView2, RTX 3060 12 GB
- `npm run check`, `npm run build`, Rust format and strict clippy pass
- `cargo test --no-run` compiles the test binaries; full Windows `cargo test` is blocked by a loader entrypoint failure
- `cargo check --locked --target x86_64-pc-windows-msvc` passes
- `npm run tauri -- info` passes
- `npm run tauri -- build --no-bundle` produces `moonlit.exe`
- The executable starts successfully with the current FakeBackend
- No Windows.Graphics.Capture, WASAPI or NVENC path is implemented yet

### What Exists (Linux Implementation)

- Tauri 2 scaffold with Svelte 5 UI
- Rust backend with shared `CaptureService` trait
- `FakeBackend` for development/testing
- Basic GSR (gpu-screen-recorder) backend structure
- Diagnostic system (doctor command)
- SQLite library structure (planned)
- Hotkey system (planned)

### What Needs Windows Implementation

- Windows.Graphics.Capture API integration
- WASAPI audio capture and mixing
- NVENC/AMF/QuickSync GPU encoding
- Hotkey registration (WinAPI)
- Process/game detection (Windows)
- Real capture and encoding
- Testing and validation

## Locked Decisions

### Platform & Distribution
- **Initial platform**: Windows 10 1903+ and Windows 11
- **Future platform**: Linux (portable architecture)
- **Primary distribution**: .exe installer (NSIS/WiX)
- **Secondary distribution**: MSIX for Microsoft Store (future, when account available)
- **ARM64**: Not supported initially (x86_64 only)

### Capture Technology
- **Capture API**: Windows.Graphics.Capture (WinRT)
- **GPU Encoding**: NVENC (NVIDIA), AMF (AMD), QuickSync (Intel)
- **CPU Fallback**: x264/x265 software encoding
- **Audio**: WASAPI (Windows Audio Session API)
- **Audio Separation**: System audio (loopback) + microphone + specific apps
- **Container**: MP4 (default) and MKV (configurable)

### Features
- **Replay buffer**: 30 seconds default, configurable (10s - 5min)
- **Hotkey**: F8 default, configurable
- **Quality presets**: Low/Medium/High/Ultra + advanced configuration
- **Game detection**: Automatic + manual
- **Notifications**: Windows system notifications (like Medal)
- **Clip storage**: `%USERPROFILE%\Videos\MoonLit` (configurable)
- **Audio mixing**: Separate volumes for system, microphone, and apps (OBS-style)

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Frontend (Svelte 5)                   │
│  - Dashboard, Capture, AudioMixer, Library, Settings    │
└─────────────────────────────────────────────────────────┘
                          ↓ IPC (Tauri)
┌─────────────────────────────────────────────────────────┐
│              Backend Rust (Portable Core)                │
│  ┌──────────────────────────────────────────────────┐  │
│  │ CaptureService (trait)                           │  │
│  │ ├─ WindowsCaptureBackend (Windows.Graphics.Capture)│ │
│  │ ├─ LinuxCaptureBackend (future)                  │  │
│  │ └─ FakeBackend (development/testing)             │  │
│  └──────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────┐  │
│  │ AudioMixerService                                │  │
│  │ ├─ WindowsAudioMixer (WASAPI loopback + capture) │  │
│  │ ├─ LinuxAudioMixer (future: PipeWire)            │  │
│  │ └─ FakeAudioMixer (development/testing)          │  │
│  └──────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────┐  │
│  │ EncodingService                                  │  │
│  │ ├─ NvencEncoder (NVIDIA)                         │  │
│  │ ├─ AmfEncoder (AMD)                              │  │
│  │ ├─ QuickSyncEncoder (Intel)                      │  │
│  │ └─ SoftwareEncoder (x264/x265 CPU fallback)      │  │
│  └──────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────┐  │
│  │ GameDetector                                     │  │
│  │ ├─ ProcessScanner (detects game processes)       │  │
│  │ └─ WindowMatcher (detects game windows)          │  │
│  └──────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────┐  │
│  │ LibraryService (SQLite)                          │  │
│  │ - Clip metadata, tags, favorites, search         │  │
│  └──────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────┐  │
│  │ HotkeyService                                    │  │
│  │ - Global hotkey registration (WinAPI)            │  │
│  │ - Persistent configuration                       │  │
│  └──────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

## Technical Stack

### Frontend (Cross-Platform)
- **Framework**: Svelte 5
- **UI Library**: Tauri 2 (WebView2 on Windows)
- **Language**: TypeScript
- **Build Tool**: Vite
- **Styling**: Custom CSS (Fluent Design-inspired)

### Backend (Portable)
- **Language**: Rust
- **Framework**: Tauri 2
- **Database**: SQLite (rusqlite)
- **Serialization**: serde

### Backend (Windows-Specific)
- **Capture**: Windows.Graphics.Capture API via `windows` crate
- **Audio**: WASAPI via `wasapi` crate
- **Encoding**: 
  - NVENC via `nvenc-sys` or direct API
  - AMF via `amf-sys` or direct API
  - QuickSync via `qsv` or Intel Media SDK
- **Hotkeys**: Win32 API via `windows` crate
- **Notifications**: Windows Toast Notifications via `windows` crate

### Future (Linux-Specific)
- **Capture**: PipeWire + X11/Wayland APIs
- **Audio**: PipeWire
- **Encoding**: VAAPI, NVENC (Linux), software
- **Hotkeys**: X11/XWayland/Wayland APIs
- **Notifications**: D-Bus/Freedesktop

## Implementation Phases

### Phase 0: Preparation and Windows Bootstrap
**Status**: In progress on Windows (started 2026-07-28)

1. ✅ Project configuration and setup
2. ✅ Update package.json, Cargo.toml, tauri.conf.json
3. ✅ Update documentation (PLAN.md, README.md, AI_CONTEXT.md)
4. ✅ Verify Tauri and Rust build on Windows
5. ✅ Create and connect the canonical replay backend contract
6. ✅ Document Windows APIs (Windows.Graphics.Capture, WASAPI)
7. ⏳ Prepare and verify Windows installer configuration

**Completed Files:**
- `src-tauri/src/traits.rs` - Canonical `ReplayBackend` contract and serialized DTOs
- `src-tauri/src/replay.rs` - GOP-aware encoded packet buffer with synthetic H.264 tests
- `src-tauri/src/backends/mod.rs` - Backend module structure
- `src-tauri/src/backends/fake.rs` - Complete FakeBackend implementation for development/testing
- `src-tauri/src/backends/windows.rs` - Windows backend stubs (ready for implementation)
- `src-tauri/src/backends/gsr.rs` - Linux-only legacy backend adapter
- `src-tauri/src/recorder.rs` - Bounded recorder actor and state transitions
- `docs/PLAN.md` - Complete implementation plan for MoonLit
- `docs/AI_CONTEXT.md` - Comprehensive project context (this file)
- `docs/TESTING.md` - Testing strategy for both Linux and Windows
- `docs/VALIDATION_QUEUE.md` - Windows-specific validation checklist
- `docs/PACKAGING.md` - Windows packaging strategy (NSIS, MSIX)
- `docs/ARCHITECTURE.md` - Complete architecture documentation with trait-based design
- `docs/WINDOWS_APIS.md` - Detailed Windows API documentation

**Can do on Linux**: Replay core, fake flows, IPC client and frontend; native Windows verification remains here

### Phase 1: Windows Backend Implementation (Windows Required)
**Status**: Pending

1. Implement WindowsCaptureBackend (Windows.Graphics.Capture)
2. Implement WindowsAudioMixer (WASAPI)
3. Implement GpuEncoder (NVENC/AMF/QuickSync)
4. Implement HotkeyService (WinAPI)
5. Implement GameDetector (process/window scanning)
6. Testing on Windows 10 and Windows 11
7. Testing with NVIDIA, AMD, Intel GPUs

**Requires Windows**: All of Phase 1

### Phase 2: Frontend Adaptation (Linux - Current)
**Status**: Pending

1. Redesign UI for Windows (Fluent Design-inspired)
2. Create Dashboard view
3. Create Capture view (monitors, windows, apps)
4. Create AudioMixer view (system, mic, apps)
5. Create Library view
6. Create Settings view
7. Connect to portable backend

**Can do on Linux**: All of Phase 2

### Phase 3: Integration & Testing (Windows Required)
**Status**: Pending

1. Integrate all backend components
2. Connect frontend to real backend
3. End-to-end testing
4. Performance optimization
5. Bug fixes

**Requires Windows**: All of Phase 3

### Phase 4: Packaging & Distribution (Windows Required)
**Status**: Pending

1. Create NSIS installer configuration
2. Configure WiX (alternative)
3. Sign code (optional)
4. Test installer on clean Windows
5. Prepare GitHub release workflow
6. Documentation for users

**Requires Windows**: All of Phase 4

### Phase 5: Future Linux Port (Linux Required)
**Status**: Future

1. Implement LinuxCaptureBackend (PipeWire + X11/Wayland)
2. Implement LinuxAudioMixer (PipeWire)
3. Implement Linux hotkeys
4. Implement Linux notifications
5. Create DEB and RPM packages
6. Testing on various Linux distributions

**Requires Linux**: All of Phase 5

## Development Strategy

### Current: Contract and Windows Preparation
- **Environment**: Windows 11 Pro x64 with RTX 3060 12 GB
- **Tools**: Rust MSVC, Visual Studio Build Tools, Windows SDK, WebView2 and Tauri CLI
- **Limitations**: Native capture, audio and encoder paths are still stubs; Rust test execution is blocked by a local DLL entrypoint issue
- **Focus**: WGC/NVENC spike and native validation

### Parallel Linux Work
- **Environment**: Fedora workstations
- **Tools**: Portable Rust/frontend development and FakeBackend tests
- **Limitations**: Cannot validate Windows APIs or Windows GPU drivers
- **Focus**: fake flows, IPC tests and frontend; native Windows validation remains on this workstation

## Key APIs Documentation

### Windows.Graphics.Capture
- **Purpose**: Screen and window capture
- **Requirements**: Windows 10 1903+
- **Pros**: Official API, no drivers needed, good performance
- **Cons**: Requires user permission (popup)

### WASAPI
- **Purpose**: Audio capture (system + microphone)
- **Features**: Loopback for system audio, capture for microphone
- **Integration**: Real-time mixing of multiple sources

### NVENC/AMF/QuickSync
- **Purpose**: GPU-accelerated video encoding
- **NVIDIA**: NVENC (GeForce, Quadro, Tesla)
- **AMD**: AMF (Radeon)
- **Intel**: QuickSync (Intel HD Graphics)
- **Fallback**: x264/x265 CPU encoding

## Testing Strategy

### Linux Testing (Can Do Now)
- Unit tests for portable logic
- Frontend testing with FakeBackend
- Code quality checks (cargo fmt, clippy)
- Documentation review

### Windows Testing (Requires Windows)
- Integration tests with real Windows APIs
- Performance testing (CPU/GPU usage)
- Audio sync testing
- Multi-GPU testing (NVIDIA, AMD, Intel)
- Windows 10 and Windows 11 compatibility
- Stability testing (24+ hours)

## Next Steps

### Immediate
1. Implement the Windows.Graphics.Capture and D3D11 capture boundary.
2. Add the direct NVENC H.264 Annex B spike without changing the IPC contract.
3. Validate the native path on Windows hardware and keep the FakeBackend flow green.

## Important Notes

- **Microsoft Store**: Ignored for now (no account yet)
- **ARM64**: Not supported initially
- **Multiple GPUs**: Not supported initially (single GPU capture)
- **Overlay indicators**: Not implemented (system notifications only)
- **Game injection**: Never (no anti-cheat bypass)
- **Shell commands**: Never execute user-controlled strings

## References

- [Windows.Graphics.Capture API](https://docs.microsoft.com/windows/win32/direct3d11/windows-graphics-capture)
- [WASAPI](https://docs.microsoft.com/windows/win32/coreaudio/wasapi)
- [NVENC](https://developer.nvidia.com/nvidia-video-codec-sdk)
- [AMD AMF](https://gpuopen.com/advanced-media-framework/)
- [Intel Quick Sync](https://software.intel.com/content/www/us/en/develop/documentation/video-tutorial/getting-started-with-intel-quick-sync-video.html)
