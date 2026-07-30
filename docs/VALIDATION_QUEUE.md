# Validation Queue

These checks require Windows hardware, specific GPUs, or long-running sessions that may not be available during Linux development. They do not block ordinary feature development but must be completed before release.

## ✅ Phase 0: Foundation and Windows Bootstrap (2026-07-28)

### Project Setup
- [x] Update package.json name to "moonlit"
- [x] Update Cargo.toml package name to "moonlit"
- [x] Update Cargo.toml lib name to "moonlit_lib"
- [x] Update tauri.conf.json productName to "MoonLit"
- [x] Update all project references to MoonLit in codebase

### Windows Configuration
- [x] Add Windows-specific dependencies to Cargo.toml
- [x] Configure Windows crate features for WinRT APIs
- [x] Set up platform-conditional compilation
- [x] Configure tauri.conf.json for Windows NSIS installer
- [x] Add WebView2 runtime handling configuration

### Portable Architecture
- [x] Define the canonical `ReplayBackend` contract
- [x] Define serializable backend, source, config and error DTOs
- [x] Create the recorder actor and revisioned snapshot model
- [x] Keep encoded frame data and native handles outside IPC
- [ ] Add audio, hotkey, game detector and library contracts when implemented

### Backend Implementations
- [x] Create `src-tauri/src/backends/` module structure
- [x] Implement the connected FakeBackend
- [x] Add the Windows native boundary with monitor-first WGC/D3D11/NVENC H.264 support
- [x] Move GSR to a Linux-only legacy adapter
- [x] Set up the platform backend factory

### Documentation
- [x] Update docs/PLAN.md with MoonLit implementation plan
- [x] Update docs/AI_CONTEXT.md with comprehensive project context
- [x] Update docs/TESTING.md with Windows testing strategy
- [x] Update docs/VALIDATION_QUEUE.md (this file) with Windows validation checklist
- [x] Create docs/PACKAGING.md with Windows packaging strategy (NSIS, MSIX)
- [x] Create docs/ARCHITECTURE.md with complete architecture documentation
- [x] Create docs/WINDOWS_APIS.md with detailed Windows API documentation
- [x] Update README.md with MoonLit information

### Code Quality
- [x] Windows target compiles without errors
- [x] Code formatted with `cargo fmt`
- [x] Frontend checks pass (`npm run check`, `npm test`, `npm run build`)
- [x] Strict clippy passes
- [ ] Rust test harness executes on this workstation (`0xC0000139` DLL entrypoint issue)

### Windows Bootstrap Evidence
- [x] `npm run tauri -- info` recognizes the Windows toolchain
- [x] `npm run tauri -- build --no-bundle` produces `moonlit.exe`
- [x] Release executable starts successfully with FakeBackend
- [ ] Manual Tauri UI start/save/stop interaction
- [x] Native Windows.Graphics.Capture monitor capture on the RTX 3060 workstation
- [ ] Native WASAPI capture
- [x] Direct NVENC H.264 Annex B encoding on the RTX 3060 workstation

## Windows Native Capture Spike (2026-07-28)

- [x] Enumerate two physical monitor sources
- [x] Detect WGC support and NVENC H.264 capability
- [x] Capture the default monitor through a free-threaded WGC frame pool
- [x] Copy captured D3D11 textures to a persistent encoder texture on the GPU
- [x] Encode synchronous H.264 Annex B packets with one initial IDR keyframe
- [x] Complete orderly capture shutdown without a native crash
- [ ] Exercise `WindowsNativeBackend::start`, `save_replay` and `stop` through the Tauri runtime
- [ ] Produce the final MP4/MKV media artifact
- [ ] Validate 720p30, 1080p60, long-running capture and performance budgets
- [ ] Validate window capture, permission handling and display changes

## Windows API Validation

### Windows.Graphics.Capture
- [ ] Basic monitor capture on Windows 10 1903+
- [x] Basic monitor capture on Windows 11
- [ ] Window capture (specific application window)
- [ ] Full screen capture (entire monitor)
- [ ] Permission popup handling (first-time use)
- [ ] Permission persistence (subsequent uses)
- [x] Multi-monitor enumeration
- [ ] Monitor configuration changes during capture
- [ ] Window resize/move during capture
- [ ] Window close during capture
- [ ] Zero-copy frame delivery (performance)
- [ ] Frame rate consistency (30, 60, 144 FPS)
- [ ] Capture with high-DPI displays
- [ ] Capture with HDR displays

### WASAPI Audio
- [ ] System audio capture (loopback) on Windows 10
- [ ] System audio capture (loopback) on Windows 11
- [ ] Microphone capture
- [ ] Multiple audio devices simultaneously
- [ ] Audio device enumeration
- [ ] Audio device changes (connect/disconnect)
- [ ] Audio device default changes
- [ ] Audio volume control (per source)
- [ ] Audio mute/unmute (per source)
- [ ] Audio sync with video (< 50ms drift)
- [ ] Audio quality (no clipping/distortion)
- [ ] Application-specific audio capture (OBS-style)
- [ ] Audio mixing (system + mic + apps)

### GPU Encoding
- [x] NVENC detection (NVIDIA)
- [ ] NVENC H.264 encoding (720p30)
- [x] NVENC H.264 encoding (1080p30)
- [ ] NVENC H.264 encoding (1080p60)
- [ ] NVENC H.264 encoding (1440p60)
- [ ] NVENC H.265 encoding (1080p60)
- [ ] NVENC H.265 encoding (1440p60)
- [ ] NVENC performance (RTX 3060)
- [ ] NVENC performance (other NVIDIA GPUs)
- [ ] AMF detection (AMD)
- [ ] AMF H.264 encoding
- [ ] AMF H.265 encoding
- [ ] AMF performance (Radeon)
- [ ] QuickSync detection (Intel)
- [ ] QuickSync H.264 encoding
- [ ] QuickSync H.265 encoding
- [ ] QuickSync performance (Intel HD/UHD)
- [ ] Automatic GPU selection
- [ ] Fallback to CPU encoding (x264/x265)
- [ ] CPU encoding performance
- [ ] GPU driver version compatibility
- [ ] GPU driver update during session

### Hotkey Service
- [ ] F8 hotkey registration (Windows 10)
- [ ] F8 hotkey registration (Windows 11)
- [ ] Custom hotkey configuration
- [ ] Global hotkey (works in background)
- [ ] Global hotkey (works in foreground)
- [ ] Global hotkey (works during games)
- [ ] Hotkey conflicts detection
- [ ] Hotkey persistence across restarts
- [ ] Hotkey with other modifiers (Ctrl+F8, etc.)
- [ ] Hotkey performance (latency < 100ms)

### Game Detector
- [ ] Process scanning (Windows 10)
- [ ] Process scanning (Windows 11)
- [ ] Window matching
- [ ] Known games database
- [ ] Automatic game detection
- [ ] Manual game addition
- [ ] Game name detection
- [ ] Game process detection
- [ ] Game window detection
- [ ] Game exit detection
- [ ] Multiple games running

### Notifications
- [ ] Windows toast notifications (Windows 10)
- [ ] Windows toast notifications (Windows 11)
- [ ] "Clip saved" notification
- [ ] "Buffer active" notification
- [ ] Error notifications
- [ ] Notification settings
- [ ] Notification permissions
- [ ] Notification click handling
- [ ] Notification persistence
- [ ] Do Not Disturb mode handling

## Feature Validation

### Replay Buffer
- [ ] 10-second buffer
- [ ] 30-second buffer (default)
- [ ] 60-second buffer
- [ ] 5-minute buffer
- [ ] RAM storage (fast)
- [ ] Disk storage (large buffers)
- [ ] Buffer overflow handling
- [ ] Save clip from buffer (hotkey)
- [ ] Buffer state persistence
- [ ] Buffer performance (CPU/GPU usage)
- [ ] Buffer with high FPS games
- [ ] Buffer with 4K games

### Audio Mixer
- [ ] System audio volume control
- [ ] Microphone volume control
- [ ] Application audio volume control
- [ ] Individual mute/unmute
- [ ] Master volume
- [ ] Volume persistence
- [ ] Real-time mixing
- [ ] Audio levels visualization
- [ ] Audio source selection
- [ ] Audio source addition/removal
- [ ] Audio mixer performance

### Library
- [ ] SQLite database creation
- [ ] Clip metadata storage
- [ ] Clip search (by name)
- [ ] Clip search (by tag)
- [ ] Clip search (by date)
- [ ] Clip tags
- [ ] Clip favorites
- [ ] Clip deletion
- [ ] Library performance (1000+ clips)
- [ ] Library persistence
- [ ] Library migration
- [ ] Library backup/restore

### Settings
- [ ] Settings persistence
- [ ] Settings UI
- [ ] Reset to defaults
- [ ] Import/export settings
- [ ] Settings validation
- [ ] Settings conflict resolution

## Quality Validation

### Video Quality
- [ ] Low preset (720p30) quality
- [ ] Medium preset (1080p30) quality
- [ ] High preset (1080p60) quality
- [ ] Ultra preset (1440p60/4K60) quality
- [ ] Custom settings quality
- [ ] H.264 quality
- [ ] H.265 quality
- [ ] Video playback compatibility
- [ ] Video file size (reasonable)

### Audio Quality
- [ ] System audio quality
- [ ] Microphone quality
- [ ] Mixed audio quality
- [ ] Audio sync with video
- [ ] No audio clipping/distortion
- [ ] Audio bitrate (reasonable)

## Performance Validation

### CPU Usage
- [ ] CPU usage during capture (idle): < 5%
- [ ] CPU usage during capture (gaming): < 15%
- [ ] CPU usage during save: < 30%
- [ ] CPU usage during library operations: < 20%

### GPU Usage
- [ ] GPU usage during capture (idle): < 20%
- [ ] GPU usage during capture (gaming): < 50%
- [ ] GPU usage during encoding: < 80%
- [ ] GPU memory usage: < 500 MB

### Memory Usage
- [ ] RAM usage (idle): < 200 MB
- [ ] RAM usage (capturing): < 500 MB
- [ ] RAM usage (with 30s buffer): < 600 MB
- [ ] RAM usage (with 5min buffer): < 1.5 GB
- [ ] No memory leaks (24h test)

### Save Performance
- [ ] Save latency: < 2 seconds
- [ ] Save performance with large buffer
- [ ] Save performance with high FPS
- [ ] Save performance with 4K
- [ ] Disk I/O during save

## Stability Validation

### Short Tests
- [ ] 1-hour continuous capture
- [ ] 4-hour continuous capture
- [ ] Multiple save operations (100+)
- [ ] Rapid start/stop cycles (50+)
- [ ] Hotkey spam (100+ presses)

### Long Tests
- [ ] 24-hour continuous capture
- [ ] 48-hour continuous capture
- [ ] 72-hour continuous capture
- [ ] Multiple restarts (100+)
- [ ] System restart during capture

### Stress Tests
- [ ] Application restart during capture
- [ ] System restart during capture
- [ ] Sleep/hibernate during capture
- [ ] Multiple MoonLit instances
- [ ] Capture with other screen recorders
- [ ] Capture with streaming software

## Compatibility Validation

### Windows Versions
- [ ] Windows 10 1903 (minimum)
- [ ] Windows 10 20H2
- [ ] Windows 10 21H2
- [ ] Windows 11 21H2
- [ ] Windows 11 22H2
- [ ] Windows 11 23H2 (latest)

### Windows Editions
- [ ] Windows 10 Home
- [ ] Windows 10 Pro
- [ ] Windows 11 Home
- [ ] Windows 11 Pro
- [ ] Windows 11 Enterprise (if available)

### Display Configurations
- [ ] Single monitor
- [ ] Dual monitors
- [ ] Triple monitors
- [ ] 4K monitor
- [ ] High refresh rate (144Hz+)
- [ ] HDR display
- [ ] High-DPI display (150%, 200%)

### GPU Configurations
- [ ] NVIDIA GPU only
- [ ] AMD GPU only
- [ ] Intel GPU only
- [ ] Laptop with iGPU + dGPU
- [ ] Desktop with multiple GPUs
- [ ] Integrated graphics only

## Edge Cases and Error Handling

### Capture Errors
- [ ] Permission denied
- [ ] Monitor disconnected during capture
- [ ] Window closed during capture
- [ ] GPU driver crash during capture
- [ ] GPU out of memory
- [ ] Insufficient disk space
- [ ] File locked by another process
- [ ] Invalid output path
- [ ] Network drive issues

### Audio Errors
- [ ] Audio device disconnected during capture
- [ ] Audio device busy
- [ ] Audio driver crash
- [ ] Insufficient audio permissions
- [ ] Audio sync issues (> 50ms)
- [ ] Audio clipping/distortion
- [ ] No audio input

### Encoding Errors
- [ ] GPU encoding failure
- [ ] Fallback to CPU encoding
- [ ] Encoding timeout
- [ ] Invalid encoding parameters
- [ ] Corrupted output file
- [ ] Encoding quality issues

### Hotkey Errors
- [ ] Hotkey conflict with other application
- [ ] Hotkey registration failure
- [ ] Hotkey not working
- [ ] Hotkey working but not triggering action
- [ ] Hotkey conflicts with Windows shortcuts

### Library Errors
- [ ] Database corruption
- [ ] Database locked
- [ ] Database migration failure
- [ ] Missing clip file
- [ ] Invalid metadata
- [ ] Disk full

## User Acceptance Testing

### Basic User Flow
- [ ] Install MoonLit on fresh Windows
- [ ] Launch MoonLit for first time
- [ ] Grant capture permissions
- [ ] Select capture source
- [ ] Start replay buffer
- [ ] Save clip with hotkey
- [ ] Find clip in library
- [ ] Play clip
- [ ] Adjust settings
- [ ] Uninstall MoonLit

### Advanced User Flow
- [ ] Configure custom hotkey
- [ ] Configure custom quality settings
- [ ] Capture with audio mixing
- [ ] Capture with game detection
- [ ] Manage clip library (tags, favorites)
- [ ] Export clips
- [ ] Backup settings
- [ ] Restore settings

### New User Experience
- [ ] Installation is easy
- [ ] First launch is smooth
- [ ] Permission request is clear
- [ ] UI is intuitive
- [ ] Default settings work well
- [ ] Help/documentation is accessible
- [ ] Error messages are helpful

## Validation Timeline

### Phase 1: Basic Validation (Week 1-2 on Windows)
- [ ] Windows.Graphics.Capture basic tests
- [ ] WASAPI basic tests
- [ ] GPU encoding basic tests
- [ ] Hotkey basic tests
- [ ] Basic save workflow

### Phase 2: Feature Validation (Week 3-4)
- [ ] Replay buffer validation
- [ ] Audio mixer validation
- [ ] Game detector validation
- [ ] Library validation
- [ ] Settings validation

### Phase 3: Quality Validation (Week 5-6)
- [ ] Video quality tests
- [ ] Audio quality tests
- [ ] Performance benchmarks
- [ ] Compatibility tests

### Phase 4: Stability Validation (Week 7-8)
- [ ] Long-term stability tests
- [ ] Stress tests
- [ ] Edge case tests

### Phase 5: Release Validation (Week 9)
- [ ] User acceptance testing
- [ ] Final bug fixes
- [ ] Performance optimization
- [ ] Documentation review

## Notes

- All Windows validation requires physical Windows machine
- GPU validation requires compatible hardware
- Multi-monitor validation requires multiple monitors
- Long stability tests require dedicated machine
- User acceptance testing requires real users
- Some tests may be automated, others require manual testing
