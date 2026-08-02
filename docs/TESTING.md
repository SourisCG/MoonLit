# Testing Strategy

## Overview

MoonLit testing is divided into two categories:
1. **Linux Testing** (can be done now): Automated checks, FakeBackend, frontend
2. **Windows Testing** (requires Windows): Real Windows APIs, GPU encoding, audio

## Automated Checks (Any Development Machine)

### Frontend Checks
```bash
npm run check
npm test
npm run build
```

### Backend Checks
```bash
cargo fmt --manifest-path src-tauri/Cargo.toml -- --check
cargo test --manifest-path src-tauri/Cargo.toml
cargo clippy --manifest-path src-tauri/Cargo.toml --all-targets --all-features -- -D warnings
```

### Packaging Checks
```bash
rpmspec -P packaging/rpm/moonlit.spec  # For future Linux RPM
```

## Unit Tests (Linux - Can Do Now)

### Portable Replay Tests
- ✅ Canonical DTO validation and configuration bounds
- ✅ FakeBackend source/capability discovery
- ✅ FakeBackend atomic simulation manifest
- ✅ Runtime actor start/save/stop transitions
- ✅ Recorder revisioned event model
- ✅ GOP-aware encoded replay buffer with synthetic H.264 packet tests

### Frontend Tests
- ✅ IPC command names and camelCase payloads
- ✅ Recorder event forwarding and unlisten handling
- ⏳ View rendering
- ⏳ Component interactions
- ⏳ State updates
- ⏳ Error handling
- ⏳ Accessibility

### Integration Tests
- ⏳ IPC communication
- ⏳ Frontend-backend data flow
- ⏳ Configuration persistence

## Windows Testing (Requires Windows)

### Phase 1: Basic Capture Testing

#### 1.1 Windows.Graphics.Capture API
- [ ] Enumerate monitors
- [ ] Enumerate windows
- [ ] Capture full screen
- [ ] Capture specific window
- [ ] Handle permission popup
- [ ] Multiple monitor support
- [ ] Monitor changes (resolution, position)
- [ ] Window resize/move during capture

#### 1.2 WASAPI Audio Capture
- [ ] Enumerate audio devices
- [ ] Capture system audio (loopback)
- [ ] Capture microphone
- [ ] Capture specific application audio (post-v1; excluded from v1 gate)
- [ ] Multiple audio sources simultaneously
- [ ] Volume control per source
- [ ] Mute/unmute functionality
- [ ] Audio device changes (connect/disconnect)
- [ ] Audio sync with video

#### 1.3 GPU Encoding
- [ ] NVENC (NVIDIA) detection
- [ ] AMF (AMD) detection
- [ ] QuickSync (Intel) detection
- [ ] Automatic GPU selection
- [ ] Encoding quality presets (Low/Medium/High/Ultra)
- [ ] Custom encoding settings
- [ ] Fallback to CPU encoding (x264/x265)
- [ ] Encoding performance
- [ ] Encoding stability (long sessions)

### Phase 2: Feature Testing

#### 2.1 Replay Buffer
- [ ] 10-second buffer
- [ ] 30-second buffer (default)
- [ ] 60-second buffer
- [ ] 5-minute buffer
- [ ] RAM storage (fast)
- [ ] Disk storage (large buffers)
- [ ] Buffer overflow handling
- [ ] Save clip from buffer
- [ ] Buffer state persistence

#### 2.2 Hotkey Service
- [ ] F8 default hotkey
- [ ] Custom hotkey configuration
- [ ] Global hotkey registration
- [ ] Hotkey conflicts detection
- [ ] Hotkey works in background
- [ ] Hotkey works in foreground
- [ ] Hotkey works during games
- [ ] Hotkey persistence across restarts

#### 2.3 Game Detector
- [ ] Process scanning
- [ ] Window matching
- [ ] Known games database
- [ ] Automatic detection
- [ ] Manual game addition
- [ ] Game name detection
- [ ] Game process detection
- [ ] Game window detection

#### 2.4 Notifications
- [ ] Windows toast notifications
- [ ] "Clip saved" notification
- [ ] "Buffer active" notification
- [ ] Error notifications
- [ ] Notification settings
- [ ] Notification permissions
- [ ] Notification click handling

#### 2.5 Library
- [ ] SQLite database creation
- [ ] Clip metadata storage
- [ ] Clip search
- [ ] Clip tags
- [ ] Clip favorites
- [ ] Clip deletion
- [ ] Library performance (1000+ clips)
- [ ] Library persistence

#### 2.6 Audio Mixer
- [ ] System audio volume
- [ ] Microphone volume
- [ ] Application audio volume
- [ ] Individual mute/unmute
- [ ] Master volume
- [ ] Volume persistence
- [ ] Real-time mixing
- [ ] Audio levels visualization

### Phase 3: Integration Testing

#### 3.1 End-to-End Tests
- [ ] Full capture workflow (start → save → stop)
- [ ] Multiple capture sessions
- [ ] Capture with audio mixing
- [ ] Capture with hotkey
- [ ] Game detection (post-v1; not part of strict v1)
- [ ] Capture with notifications
- [ ] Capture with library integration

#### 3.2 Multi-Monitor Tests
- [ ] Capture monitor 1
- [ ] Capture monitor 2
- [ ] Capture monitor 3+
- [ ] Switch between monitors
- [ ] Capture all monitors (future)
- [ ] Monitor configuration changes

#### 3.3 Multi-GPU Tests
- [ ] NVIDIA GPU (NVENC)
- [ ] AMD GPU (AMF)
- [ ] Intel GPU (QuickSync)
- [ ] Multiple GPUs (laptop with iGPU + dGPU)
- [ ] GPU switching
- [ ] Fallback to software encoding

#### 3.4 Performance Tests
- [ ] CPU usage during capture (idle)
- [ ] CPU usage during capture (gaming)
- [ ] GPU usage during capture (idle)
- [ ] GPU usage during capture (gaming)
- [ ] Memory usage (RAM)
- [ ] Disk I/O during save
- [ ] Network impact (if streaming in future)

#### 3.5 Stability Tests
- [ ] 1-hour continuous capture
- [ ] 4-hour continuous capture
- [ ] 24-hour continuous capture
- [ ] Multiple save operations
- [ ] Rapid start/stop cycles
- [ ] Hotkey spam
- [ ] Application restart during capture
- [ ] System restart during capture
- [ ] Sleep/hibernate during capture

#### 3.6 Compatibility Tests
- [ ] Windows 10 Enterprise LTSC 2021
- [ ] Windows 10 20H2
- [ ] Windows 10 21H2
- [ ] Windows 11 21H2
- [ ] Windows 11 22H2
- [ ] Windows 11 23H2
- [ ] Different Windows editions (Home, Pro, Enterprise)
- [ ] Different language settings
- [ ] Different DPI settings
- [ ] Different color profiles

### Phase 4: Edge Cases and Error Handling

#### 4.1 Capture Errors
- [ ] Permission denied
- [ ] Monitor disconnected
- [ ] Window closed during capture
- [ ] GPU driver crash
- [ ] GPU out of memory
- [ ] Insufficient disk space
- [ ] File locked by another process
- [ ] Invalid output path
- [ ] Network drive issues

#### 4.2 Audio Errors
- [ ] Audio device disconnected
- [ ] Audio device busy
- [ ] Audio driver crash
- [ ] Insufficient audio permissions
- [ ] Audio sync issues
- [ ] Audio clipping/distortion
- [ ] No audio input

#### 4.3 Encoding Errors
- [ ] GPU encoding failure
- [ ] Fallback to CPU encoding
- [ ] Encoding timeout
- [ ] Invalid encoding parameters
- [ ] Corrupted output file
- [ ] Encoding quality issues

#### 4.4 Hotkey Errors
- [ ] Hotkey conflict
- [ ] Hotkey registration failure
- [ ] Hotkey not working
- [ ] Hotkey working but not triggering action

#### 4.5 Library Errors
- [ ] Database corruption
- [ ] Database locked
- [ ] Database migration failure
- [ ] Missing clip file
- [ ] Invalid metadata

## Test Matrix

### Windows Versions
| Version | Priority | Status |
|---------|----------|--------|
| Windows 10 Enterprise LTSC 2021 | High | ⏳ |
| Windows 10 21H2 | High | ⏳ |
| Windows 11 22H2 | High | ⏳ |
| Windows 11 23H2 | Medium | ⏳ |

### GPU Vendors
| Vendor | Priority | Status |
|--------|----------|--------|
| NVIDIA (NVENC) | High | ⏳ |
| AMD (AMF) | High | ⏳ |
| Intel (QuickSync) | Medium | ⏳ |
| Software (x264/x265) | High | ⏳ |

### Capture Scenarios
| Scenario | Priority | Status |
|----------|----------|--------|
| Full screen | High | ⏳ |
| Window capture | High | ⏳ |
| Multi-monitor | Medium | ⏳ |
| Monitor/window capture only (Game Capture prohibited) | High | ⏳ |
| Desktop capture | High | ⏳ |

### Audio Scenarios
| Scenario | Priority | Status |
|----------|----------|--------|
| System audio only | High | ⏳ |
| Microphone only | High | ⏳ |
| Both (mixed) | High | ⏳ |
| App-specific audio | Medium | ⏳ |
| Multiple sources | Medium | ⏳ |

## Performance Benchmarks

### Target Metrics
- **CPU Usage**: < 5% (idle), < 15% (gaming)
- **GPU Usage**: < 20% (idle), < 50% (gaming)
- **Memory Usage**: < 500 MB
- **Save Latency**: < 2 seconds
- **A/V Sync**: < 50 ms drift in 1-hour test
- **Dropped Frames**: < 0.5% in target profile

### Measurement Tools
- Windows Performance Monitor
- Task Manager
- GPU-Z
- HWiNFO
- OBS Studio (comparison)

## Bug Reporting

When reporting bugs, include:
- Windows version (e.g., Windows 11 23H2)
- GPU model and driver version
- CPU model
- RAM amount
- MoonLit version
- Screenshot or video of the issue
- Log files from `%APPDATA%\com.souriscg.moonlit\logs\`
- Steps to reproduce

## Continuous Integration

### Automated Tests (GitHub Actions)
```yaml
name: CI

on: [push, pull_request]

jobs:
  test-linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Install Rust
        uses: actions-rs/toolchain@v1
      - name: Install Node.js
        uses: actions/setup-node@v3
      - name: Install dependencies
        run: npm install
      - name: Run checks
        run: |
          npm run check
          npm run build
          cargo fmt --manifest-path src-tauri/Cargo.toml -- --check
          cargo test --manifest-path src-tauri/Cargo.toml
          cargo clippy --manifest-path src-tauri/Cargo.toml --all-targets --all-features -- -D warnings

  test-windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v3
      - name: Install Rust
        uses: actions-rs/toolchain@v1
      - name: Install Node.js
        uses: actions/setup-node@v3
      - name: Install dependencies
        run: npm install
      - name: Build
        run: npm run tauri build
      - name: Run tests
        run: cargo test --manifest-path src-tauri/Cargo.toml
```

### Manual Testing Checklist
- [ ] All automated tests pass
- [ ] Capture works on target Windows versions
- [ ] Audio mixing works correctly
- [ ] GPU encoding works on all vendors
- [ ] Hotkey works globally
- [ ] Game detection (post-v1; excluded from v1 gate)
- [ ] Notifications work
- [ ] Library works with 1000+ clips
- [ ] Performance meets targets
- [ ] Stability test (24h) passes

## Testing Timeline

### Phase 0: Preparation (Linux - Current)
- ✅ Automated checks setup
- ⏳ Unit tests for portable code
- ⏳ Frontend tests

### Phase 1: Windows Backend (Windows Required)
- ⏳ Basic capture tests
- ⏳ Audio tests
- ⏳ Encoding tests
- ⏳ Hotkey tests

### Phase 2: Integration (Windows Required)
- ⏳ End-to-end tests
- ⏳ Performance tests
- ⏳ Stability tests
- ⏳ Compatibility tests

### Phase 3: Release (Windows Required)
- ⏳ Final validation
- ⏳ Edge case testing
- ⏳ User acceptance testing

## Notes

- Linux testing is limited to automated checks and FakeBackend
- Windows testing requires physical Windows machine
- GPU testing requires compatible hardware
- Multi-monitor testing requires multiple monitors
- Long stability tests (24h+) require dedicated machine

## Executable CI

The checked-in workflows are `.github/workflows/ci.yml` and
`.github/workflows/release.yml`. The CI workflow runs frontend checks, locked
portable crates, Windows clippy and the Windows test harness. The release
workflow remains intentionally gated by approved runtime/license manifests and
the real recorder self-test.
