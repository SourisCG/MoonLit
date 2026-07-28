# MoonLit

MoonLit is a Windows-first, local-first game clip recorder inspired by Medal and OBS. It features GPU-accelerated screen capture, advanced audio mixing, replay buffer, and a simple editor.

Windows with future Linux portability planned.

## Features

- **GPU-Accelerated Capture**: Windows.Graphics.Capture API with NVENC/AMF/QuickSync encoding
- **Replay Buffer**: 30 seconds default (configurable 10s - 5min)
- **Advanced Audio Mixing**: System audio, microphone, and specific apps (OBS-style)
- **Hotkey Save**: F8 by default (configurable)
- **Game Detection**: Automatic + manual detection
- **Local Library**: SQLite-based clip management with tags and search
- **Multiple Formats**: MP4 (default) and MKV
- **Quality Presets**: Low/Medium/High/Ultra + advanced configuration
- **Notifications**: Windows system notifications

## Requirements

### System Requirements
- **OS**: Windows 10 1903+ or Windows 11
- **Architecture**: x86_64 (64-bit)
- **GPU**: Optional (for hardware encoding)
  - NVIDIA: NVENC support
  - AMD: AMF support
  - Intel: QuickSync support
- **CPU Fallback**: x264/x265 software encoding

### Development Requirements
- Node.js 20 or newer
- npm 10 or newer
- Rust stable (1.88+)
- Tauri 2.x CLI
- Windows SDK (for Windows-specific features)

## Installation

### Windows (Release - Future)

1. Download the installer from GitHub Releases
2. Run the installer
3. Follow the installation wizard
4. Launch MoonLit from Start Menu or Desktop

**Note**: Microsoft Store version will be available in the future.

### Development Build

See [Development](#development) section below.

### Current Development Status

The Windows bootstrap is verified: the Tauri application builds and launches
with the simulated backend. Native Windows.Graphics.Capture, WASAPI, NVENC,
the persistent library and the installer are still under development.

## Usage

### Quick Start

1. Launch MoonLit
2. Select capture source (monitor, window, or application)
3. Configure audio sources and volumes
4. Start replay buffer
5. Press F8 (or configured hotkey) to save last 30 seconds
6. Find your clip in `%USERPROFILE%\Videos\MoonLit`

### Audio Mixing

MoonLit supports multiple audio sources:
- **System Audio**: Captures all system sounds (loopback)
- **Microphone**: Your microphone input
- **Application Audio**: Specific applications (OBS-style)

Each source has independent volume control and can be muted/unmuted.

### Capture Modes

- **Full Screen**: Capture entire monitor
- **Window**: Capture specific window
- **Application**: Capture specific application (when available)

### Quality Settings

**Presets**:
- Low: 720p30
- Medium: 1080p30
- High: 1080p60
- Ultra: 1440p60 or 4K60

**Advanced**: Custom bitrate, FPS, resolution, and codec settings.

## Development

### Project Structure

```
moonlit/
├── src/                    # Frontend (Svelte 5)
│   ├── App.svelte         # Main application component
│   ├── main.ts            # Application entry point
│   └── app.css            # Global styles
├── src-tauri/             # Backend (Rust)
│   ├── src/
│   │   ├── lib.rs         # Library entry point
│   │   ├── main.rs        # Application entry point
│   │   ├── capture.rs     # Capture service (portable)
│   │   ├── recorder.rs    # Recording service
│   │   ├── doctor.rs      # Diagnostic service
│   │   └── state.rs       # Application state
│   ├── Cargo.toml         # Rust dependencies
│   └── tauri.conf.json    # Tauri configuration
├── docs/                  # Documentation
│   ├── PLAN.md           # Implementation plan
│   ├── AI_CONTEXT.md     # AI context and decisions
│   ├── TESTING.md        # Testing strategy
│   ├── ARCHITECTURE.md   # Architecture documentation
│   └── WINDOWS_APIS.md   # Windows API documentation
└── packaging/            # Packaging configuration
    └── windows/          # Windows-specific packaging
```

### Backend Architecture

The backend uses a **portable trait-based architecture**:

```rust
pub trait CaptureService {
    fn start_replay(&mut self, config: CaptureConfig) -> Result<()>;
    fn save_clip(&mut self) -> Result<PathBuf>;
    fn stop(&mut self) -> Result<()>;
}

pub trait AudioMixerService {
    fn add_source(&mut self, source: AudioSource) -> Result<()>;
    fn set_volume(&mut self, source_id: &str, volume: f32) -> Result<()>;
    fn mix(&mut self, buffer: &mut AudioBuffer) -> Result<()>;
}
```

Platform-specific implementations:
- **Windows**: `WindowsCaptureBackend`, `WindowsAudioMixer`, `NvencEncoder`
- **Linux (Future)**: `LinuxCaptureBackend`, `LinuxAudioMixer`
- **Development**: `FakeBackend`, `FakeAudioMixer`

### Setup Development Environment

#### Windows (Primary)

1. Install prerequisites:
   ```powershell
   # Install Node.js (20+) from https://nodejs.org/
   # Install Rust from https://rustup.rs/
   # Install Visual Studio Build Tools with C++ workload
   ```

2. Install dependencies:
   ```bash
   npm install
   ```

3. Run development server:
   ```bash
   npm run tauri dev
   ```

#### Linux (Development Only - Current)

**Note**: Linux can develop frontend and portable backend, but cannot compile or test Windows-specific features.

1. Install prerequisites (Fedora):
   ```bash
   sudo dnf install webkit2gtk4.1-devel openssl-devel curl wget file \
     libappindicator-gtk3-devel librsvg2-devel libxdo-devel
   sudo dnf group install "c-development"
   ```

2. Install dependencies:
   ```bash
   npm install
   ```

3. Run development server (with FakeBackend):
   ```bash
   npm run tauri dev
   ```

### Build

#### Development Build
```bash
npm run tauri dev
```

#### Production Build (Windows)
```bash
npm run tauri build
```

Output: `src-tauri/target/release/bundle/nsis/MoonLit_0.1.0_x64-setup.exe`

### Testing

#### Automated Checks
```bash
# Frontend checks
npm run check
npm run build

# Backend checks
cargo fmt --manifest-path src-tauri/Cargo.toml -- --check
cargo test --manifest-path src-tauri/Cargo.toml
cargo clippy --manifest-path src-tauri/Cargo.toml --all-targets --all-features -- -D warnings
```

#### Manual Testing (Windows Required)
- Test capture with Windows.Graphics.Capture
- Test audio mixing with WASAPI
- Test GPU encoding (NVENC/AMF/QuickSync)
- Test hotkey registration
- Test on Windows 10 and Windows 11
- Test with different GPUs

See [TESTING.md](docs/TESTING.md) for complete testing strategy.

## Configuration

### Settings Location

**Windows**: `%APPDATA%\com.souriscg.moonlit\`

### Configurable Options

- Replay buffer duration (10s - 5min, default: 30s)
- Save hotkey (default: F8)
- Clip storage location (default: `%USERPROFILE%\Videos\MoonLit`)
- Video format (MP4 or MKV)
- Quality preset or advanced settings
- Audio source volumes
- Game detection settings
- Notification preferences

## Troubleshooting

### Capture Permission Popup
Windows.Graphics.Capture requires user permission. On first use, Windows will show a permission popup. Click "Allow" to enable capture.

### No GPU Detected
MoonLit will fall back to CPU encoding (x264/x265). This works but uses more CPU and may have lower performance.

### Audio Not Capturing
1. Check audio device settings in Windows
2. Ensure microphone permissions are granted
3. Verify WASAPI is available (built into Windows)
4. Check audio mixer settings in MoonLit

### Hotkey Not Working
1. Check if another application is using F8
2. Change hotkey in settings
3. Ensure MoonLit has focus or is running in background

### Performance Issues
1. Lower quality preset
2. Use GPU encoding if available
3. Reduce replay buffer duration
4. Close other resource-intensive applications

## Roadmap

### Phase 0: Preparation (Current)
- ✅ Project configuration and setup
- ✅ Documentation updated for Windows
- ⏳ Portable backend structure
- ⏳ Windows API documentation

### Phase 1: Windows Backend (Next)
- ⏳ Windows.Graphics.Capture integration
- ⏳ WASAPI audio capture
- ⏳ GPU encoding (NVENC/AMF/QuickSync)
- ⏳ Hotkey service
- ⏳ Game detector

### Phase 2: Frontend
- ⏳ Windows-inspired UI
- ⏳ All views implemented
- ⏳ Connected to backend

### Phase 3: Integration
- ⏳ Full integration
- ⏳ Testing and optimization
- ⏳ Bug fixes

### Phase 4: Release
- ⏳ NSIS installer
- ⏳ GitHub release
- ⏳ User documentation

### Phase 5: Linux Port (Future)
- ⏳ Linux backend implementations
- ⏳ DEB/RPM packages
- ⏳ Linux testing

## Contributing

This project is currently in active development. Contributions are welcome once the initial release is complete.

## License

MoonLit is licensed under GPL-3.0-only. See LICENSE file for details.

## Credits

- Inspired by Medal and OBS Studio
- Built with Tauri 2, Svelte 5, and Rust
- Windows.Graphics.Capture API for capture
- WASAPI for audio
- NVENC/AMF/QuickSync for GPU encoding

## References

- [Windows.Graphics.Capture](https://docs.microsoft.com/windows/win32/direct3d11/windows-graphics-capture)
- [WASAPI](https://docs.microsoft.com/windows/win32/coreaudio/wasapi)
- [NVENC SDK](https://developer.nvidia.com/nvidia-video-codec-sdk)
- [AMD AMF](https://gpuopen.com/advanced-media-framework/)
- [Tauri Documentation](https://v2.tauri.app/)
- [Svelte Documentation](https://svelte.dev/)

## Support

For issues and questions:
- GitHub Issues (when repository is public)
- Documentation in `docs/` folder
