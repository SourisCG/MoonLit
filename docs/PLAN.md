# MoonLit Implementation Plan

## Product

MoonLit is a Windows-first, local-first desktop application for saving game clips in the style of Medal. It features GPU-accelerated capture, advanced audio mixing, replay buffer, and a simple editor. Linux version planned for future release.

## Locked Decisions

### Platform & Distribution
- Tauri 2, Svelte 5, TypeScript and Rust.
- Windows 10 1903+ and Windows 11 x86_64.
- Linux x86_64 support planned (future, portable architecture).
- ARM64 not supported initially (x86_64 only).
- Primary distribution: .exe installer (NSIS/WiX).
- Secondary distribution: MSIX for Microsoft Store (future).
- GPL-3.0-only application license.

### Capture Technology
- Windows.Graphics.Capture API (WinRT) for screen/window capture.
- WASAPI (Windows Audio Session API) for audio capture.
- GPU encoding: NVENC (NVIDIA), AMF (AMD), QuickSync (Intel).
- CPU fallback: x264/x265 software encoding.
- No game injection, anti-cheat hooks or in-game overlays.

### Features
- Replay buffer with 30 seconds default (configurable 10s - 5min).
- Hotkey: F8 default (configurable).
- Audio separation: System audio, microphone, and specific apps (OBS-style).
- Volume control: Both system and microphone adjustable independently.
- Game detection: Automatic + manual.
- Notifications: Windows system notifications (like Medal).
- Clip storage: `%USERPROFILE%\Videos\MoonLit` (configurable).
- Formats: MP4 (default) and MKV (configurable).
- Quality: Presets (Low/Medium/High/Ultra) + advanced configuration.

## Current Milestone

The project is designed as a Windows-first application with portable architecture for future Linux support.

**Phase 0: Preparation (Linux - Current)**
- ✅ Project configuration and setup
- ✅ Update package.json, Cargo.toml, tauri.conf.json
- ✅ Update documentation (PLAN.md, README.md, AI_CONTEXT.md)
- ⏳ Configure Tauri for Windows (cross-compilation setup)
- ⏳ Create portable backend structure (traits, interfaces)
- ⏳ Document Windows APIs (Windows.Graphics.Capture, WASAPI)
- ⏳ Prepare NSIS/WiX installer configuration

**Phase 1: Windows Backend Implementation (Windows Required)**
- ⏳ Implement WindowsCaptureBackend (Windows.Graphics.Capture)
- ⏳ Implement WindowsAudioMixer (WASAPI loopback + capture)
- ⏳ Implement GpuEncoder (NVENC/AMF/QuickSync)
- ⏳ Implement HotkeyService (WinAPI global hotkeys)
- ⏳ Implement GameDetector (process/window scanning)
- ⏳ Testing on Windows 10 and Windows 11
- ⏳ Testing with NVIDIA, AMD, Intel GPUs

**Phase 2: Frontend Adaptation (Linux - Current)**
- ⏳ Redesign UI for Windows (Fluent Design-inspired)
- ⏳ Create Dashboard view
- ⏳ Create Capture view (monitors, windows, apps)
- ⏳ Create AudioMixer view (system, mic, apps)
- ⏳ Create Library view
- ⏳ Create Settings view
- ⏳ Connect to portable backend

**Phase 3: Integration & Testing (Windows Required)**
- ⏳ Integrate all backend components
- ⏳ Connect frontend to real backend
- ⏳ End-to-end testing
- ⏳ Performance optimization
- ⏳ Bug fixes

**Phase 4: Packaging & Distribution (Windows Required)**
- ⏳ Create NSIS installer configuration
- ⏳ Configure WiX (alternative)
- ⏳ Sign code (optional)
- ⏳ Test installer on clean Windows
- ⏳ Prepare GitHub release workflow
- ⏳ User documentation

**Phase 5: Future Linux Port (Linux Required)**
- ⏳ Implement LinuxCaptureBackend (PipeWire + X11/Wayland)
- ⏳ Implement LinuxAudioMixer (PipeWire)
- ⏳ Implement Linux hotkeys
- ⏳ Implement Linux notifications
- ⏳ Create DEB and RPM packages
- ⏳ Testing on various Linux distributions

## Feature Roadmap

### MVP (Minimum Viable Product)
- Screen/monitor capture
- Window capture
- Replay buffer (30s default)
- Save clip with hotkey (F8)
- Basic audio mixing (system + mic)
- MP4/MKV output
- Local library with SQLite
- Basic settings

### Later
- Application-specific audio capture (OBS-style)
- Game detection (automatic + manual)
- Advanced quality settings (bitrate, FPS, resolution)
- Clip editing (trim, split, join)
- Audio track separation
- Export presets (YouTube, Discord, Twitter)
- Notifications and system tray
- Overlay indicators (optional)
- Linux port

## Architecture

```text
┌─────────────────────────────────────────────────────────┐
│                    Frontend (Svelte 5)                   │
│  ┌──────────────────────────────────────────────────┐  │
│  │ Views:                                           │  │
│  │ - Dashboard (status, quick access)               │  │
│  │ - Capture (monitors, windows, apps)              │  │
│  │ - AudioMixer (system, mic, apps)                 │  │
│  │ - Library (saved clips)                          │  │
│  │ - Settings (configuration)                       │  │
│  └──────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
                          ↓ IPC (Tauri)
┌─────────────────────────────────────────────────────────┐
│              Backend Rust (Portable Core)                │
│  ┌──────────────────────────────────────────────────┐  │
│  │ CaptureService (trait)                           │  │
│  │ ├─ WindowsCaptureBackend (Windows.Graphics.Capture)│ │
│  │ ├─ LinuxCaptureBackend (future: PipeWire + X11/Wayland)│ │
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

`CaptureService`, `AudioMixerService`, and other services use traits to enable platform-specific implementations while maintaining a portable core. The `FakeBackend` implementations are supported development tools, not temporary hacks. No frame data should cross Tauri IPC.

## Adaptive Testing

- L0: Unit tests and FakeBackend on any workstation.
- L1: 720p30, 10-second smoke capture on Windows with real GPU.
- L2: Full integration test with audio mixing and library.
- L3: RTX 3060 NVENC performance, long buffers, and soak tests.
- L4: Release matrix across Windows 10, Windows 11, and GPU vendors.

L3 and L4 are not development blockers. They are release evidence and belong in `docs/VALIDATION_QUEUE.md` until Windows hardware is available.

## Hardware Profiles

### Windows Test Machine
- **OS**: Windows 10 1903+ or Windows 11
- **GPU**: NVIDIA RTX 3060 12 GB (or other GPU)
- **Encoding**: NVENC H.264/H.265
- **Audio**: WASAPI loopback + microphone
- **Role**: Complete development and testing environment

### Future Linux Machine
- **OS**: Fedora 44+ (GNOME or KDE)
- **GPU**: AMD/Intel/NVIDIA
- **Encoding**: VAAPI/NVENC (Linux)
- **Audio**: PipeWire
- **Role**: Linux port development and testing

## Important Constraints

- Windows.Graphics.Capture requires user permission (popup on first use).
- WASAPI loopback captures system audio but not microphone simultaneously without mixing.
- NVENC/AMF/QuickSync require compatible GPU and drivers.
- H.265 (HEVC) patent and redistributable codec questions require review.
- Do not assume that multiple audio sources create independent tracks; prove with tests.
- Never inject into games or bypass anti-cheat.
- Never execute user-controlled command strings through a shell.
- Microsoft Store distribution requires developer account (not available yet).

## Milestones

1. **Foundation and Fake Flows** (Linux - Adapted)
   - ✅ Project foundation established
   - ⏳ Portable backend structure with traits
   - ⏳ FakeBackend for Windows simulation
   - ⏳ Frontend structure with views

2. **Windows Backend Implementation** (Windows Required)
   - ⏳ Windows.Graphics.Capture integration
   - ⏳ WASAPI audio capture and mixing
   - ⏳ GPU encoding (NVENC/AMF/QuickSync)
   - ⏳ Hotkey service
   - ⏳ Game detector

3. **Frontend Adaptation** (Linux - Current)
   - ⏳ Windows-inspired UI design
   - ⏳ All views implemented
   - ⏳ Connected to portable backend

4. **Integration and Testing** (Windows Required)
   - ⏳ Full integration
   - ⏳ End-to-end testing
   - ⏳ Performance optimization

5. **Packaging and Distribution** (Windows Required)
   - ⏳ NSIS installer
   - ⏳ GitHub release workflow
   - ⏳ User documentation

6. **Release** (Windows Required)
   - ⏳ Initial release
   - ⏳ User feedback collection
   - ⏳ Bug fixes and improvements

7. **Linux Port** (Linux Required)
   - ⏳ Linux backend implementations
   - ⏳ DEB/RPM packages
   - ⏳ Linux testing and validation

## Feature Specifications

### Replay Buffer
- **Default**: 30 seconds
- **Range**: 10 seconds to 5 minutes
- **Storage**: RAM (fast) or disk (large buffers)
- **Save**: Hotkey (F8 default) saves last N seconds to MP4/MKV

### Audio Mixing
- **Sources**: System audio (loopback), microphone, specific apps
- **Volumes**: Independent control per source
- **Mute**: Individual mute/unmute
- **Mixing**: Real-time mixing before encoding

### Capture Modes
- **Full screen**: Capture entire monitor
- **Window**: Capture specific window
- **Application**: Capture specific application (when available)
- **Region**: Capture custom region (future)

### Quality Settings
- **Presets**: Low (720p30), Medium (1080p30), High (1080p60), Ultra (1440p60/4K60)
- **Advanced**: Custom bitrate, FPS, resolution, codec
- **GPU**: NVENC/AMF/QuickSync (automatic detection)
- **CPU**: x264/x265 fallback

### Game Detection
- **Automatic**: Scan running processes for known games
- **Manual**: Add games manually with name and process
- **Database**: Community-maintained game database (future)

### Hotkeys
- **Save clip**: F8 (default, configurable)
- **Start/stop**: Configurable
- **Mute mic**: Configurable
- **Toggle capture**: Configurable

### Notifications
- **Clip saved**: Windows toast notification
- **Buffer active**: Tray icon indicator
- **Errors**: Error notifications with details

### Library
- **Storage**: SQLite database
- **Metadata**: Filename, path, duration, size, date, tags
- **Search**: By name, tag, date
- **Favorites**: Mark clips as favorites
- **Preview**: Thumbnail and basic info

## Technical Requirements

### Windows
- **OS**: Windows 10 1903+ or Windows 11
- **Runtime**: WebView2 (installed by Tauri)
- **GPU**: Optional (for hardware encoding)
- **Audio**: WASAPI (built into Windows)

### Development
- **Node.js**: 20 or newer
- **npm**: 10 or newer
- **Rust**: Stable (1.77.2+)
- **Tauri**: 2.x
- **Windows SDK**: For Windows-specific features

### Build
- **Target**: x86_64-pc-windows-msvc
- **Installer**: NSIS or WiX
- **Signing**: Code signing (optional)

## Distribution

### .exe Installer
- **Tool**: NSIS (Nullsoft Scriptable Install System)
- **Installation**: Current user (no admin required)
- **Uninstallation**: Clean removal
- **Updates**: Manual (download new version) or automatic (future)

### Microsoft Store (Future)
- **Format**: MSIX
- **Requirements**: Microsoft Store developer account
- **Sandbox**: More restrictive (may limit features)
- **Updates**: Automatic through Store

## References

- [Tauri 2 Documentation](https://v2.tauri.app/)
- [Svelte 5 Documentation](https://svelte.dev/)
- [Windows.Graphics.Capture](https://docs.microsoft.com/windows/win32/direct3d11/windows-graphics-capture)
- [WASAPI](https://docs.microsoft.com/windows/win32/coreaudio/wasapi)
- [NVENC SDK](https://developer.nvidia.com/nvidia-video-codec-sdk)
- [AMD AMF](https://gpuopen.com/advanced-media-framework/)
- [Intel Quick Sync](https://software.intel.com/content/www/us/en/develop/documentation/video-tutorial/getting-started-with-intel-quick-sync-video.html)
