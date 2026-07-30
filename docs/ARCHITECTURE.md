# MoonLit Architecture

> **Current implementation note (2026-07-28):** The runtime now uses the
> `ReplayBackend` contract in `src-tauri/src/traits.rs`, a bounded recorder
> actor in `src-tauri/src/recorder.rs`, and a single connected `FakeBackend`.
> GSR is a Linux-only legacy adapter. The sections below describe the broader
> target architecture for audio, library and native capture work; examples
> mentioning the retired `CaptureService` contract are historical until those
> services are implemented.

> **V1 update (2026-07-29):** The host implementation now includes
> `ConfigStore`, `StorageManager`, `LibraryStore`, `MediaJobService`, global
> hotkey/tray services and system notifications. The recorder boundary is
> protocol v2 and carries H.264/H.265, MP4/MKV, quality and audio metadata;
> media samples remain inside the sidecar. The service examples below are
> target interfaces and must not move audio or video buffers across IPC.

## Current Runtime Contract

The current runtime has one backend factory and exposes only metadata over
Tauri IPC:

```text
Frontend capture client
        -> Tauri commands/events
RecorderRuntime actor
        -> ReplayBackend
           ├─ FakeBackend
           ├─ WindowsNativeBackend boundary
           └─ LegacyGsrBackend (Linux only)
```

The backend owns capture, encoding and replay resources. Frames, textures,
encoded packets and native handles never cross IPC. The portable core now
includes a GOP-aware replay packet buffer; the next addition is the Windows
WGC/NVENC spike.

## Overview

MoonLit uses a **portable, trait-based architecture** that enables platform-specific implementations while maintaining a unified codebase. This design allows the project to start on Windows and expand to Linux in the future without rewriting the entire application.

## Architecture Principles

1. **Portable Core**: Business logic, UI, and data models are platform-independent
2. **Platform Traits**: Define interfaces for platform-specific functionality
3. **Concrete Implementations**: Each platform provides its own implementation
4. **Fake Backends**: Development and testing without real hardware
5. **No Cross-Platform Data**: Frame data never crosses IPC boundaries
6. **Type-Safe IPC**: All communication between frontend and backend uses typed interfaces

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         Frontend Layer                           │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │                    Svelte 5 + TypeScript                    │ │
│  │  ┌─────────────────────────────────────────────────────┐ │ │
│  │  │ Views: Dashboard, Capture, AudioMixer, Library,      │ │ │
│  │  │        Settings                                      │ │ │
│  │  └─────────────────────────────────────────────────────┘ │ │
│  │  ┌─────────────────────────────────────────────────────┐ │ │
│  │  │ State Management: Svelte Stores                      │ │ │
│  │  └─────────────────────────────────────────────────────┘ │ │
│  │  ┌─────────────────────────────────────────────────────┐ │ │
│  │  │ UI Components: Fluent Design-inspired                │ │ │
│  │  └─────────────────────────────────────────────────────┘ │ │
│  └───────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                              ↓ Tauri IPC (Type-Safe)
┌─────────────────────────────────────────────────────────────────┐
│                      Backend Layer (Rust)                        │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │                   Tauri Application Core                   │ │
│  │  ┌─────────────────────────────────────────────────────┐ │ │
│  │  │ Command Handlers (Type-Safe IPC Endpoints)           │ │ │
│  │  └─────────────────────────────────────────────────────┘ │ │
│  └───────────────────────────────────────────────────────────┘ │
│                              ↓                                    │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │                    Service Layer (Traits)                   │ │
│  │  ┌─────────────────────────────────────────────────────┐ │ │
│  │  │ CaptureService                                       │ │ │
│  │  │ ├─ WindowsCaptureBackend (Windows.Graphics.Capture) │ │ │
│  │  │ ├─ LinuxCaptureBackend (Future: PipeWire + X11)     │ │ │
│  │  │ └─ FakeBackend (Development/Testing)                │ │ │
│  │  └─────────────────────────────────────────────────────┘ │ │
│  │  ┌─────────────────────────────────────────────────────┐ │ │
│  │  │ AudioMixerService                                    │ │ │
│  │  │ ├─ WindowsAudioMixer (WASAPI)                       │ │ │
│  │  │ ├─ LinuxAudioMixer (Future: PipeWire)               │ │ │
│  │  │ └─ FakeAudioMixer (Development/Testing)             │ │ │
│  │  └─────────────────────────────────────────────────────┘ │ │
│  │  ┌─────────────────────────────────────────────────────┐ │ │
│  │  │ EncodingService                                      │ │ │
│  │  │ ├─ NvencEncoder (NVIDIA GPU)                        │ │ │
│  │  │ ├─ AmfEncoder (AMD GPU)                             │ │ │
│  │  │ ├─ QuickSyncEncoder (Intel GPU)                     │ │ │
│  │  │ └─ SoftwareEncoder (x264/x265 CPU)                  │ │ │
│  │  └─────────────────────────────────────────────────────┘ │ │
│  │  ┌─────────────────────────────────────────────────────┐ │ │
│  │  │ GameDetector                                         │ │ │
│  │  │ ├─ ProcessScanner                                   │ │ │
│  │  │ └─ WindowMatcher                                    │ │ │
│  │  └─────────────────────────────────────────────────────┘ │ │
│  │  ┌─────────────────────────────────────────────────────┐ │ │
│  │  │ HotkeyService                                        │ │ │
│  │  │ ├─ WindowsHotkeyService (WinAPI)                    │ │ │
│  │  │ ├─ LinuxHotkeyService (Future: X11/Wayland)         │ │ │
│  │  │ └─ FakeHotkeyService (Development/Testing)          │ │ │
│  │  └─────────────────────────────────────────────────────┘ │ │
│  │  ┌─────────────────────────────────────────────────────┐ │ │
│  │  │ LibraryService (SQLite)                              │ │ │
│  │  │ ├─ ClipStorage                                      │ │ │
│  │  │ ├─ MetadataManagement                               │ │ │
│  │  │ └─ SearchIndex                                      │ │ │
│  │  └─────────────────────────────────────────────────────┘ │ │
│  │  ┌─────────────────────────────────────────────────────┐ │ │
│  │  │ ConfigService                                        │ │ │
│  │  │ ├─ SettingsPersistence                              │ │ │
│  │  │ ├─ MigrationManagement                              │ │ │
│  │  │ └─ Validation                                       │ │ │
│  │  └─────────────────────────────────────────────────────┘ │ │
│  └───────────────────────────────────────────────────────────┘ │
│                              ↓                                    │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │                   Platform APIs (OS-Specific)               │ │
│  │  ┌─────────────────────────────────────────────────────┐ │ │
│  │  │ Windows: WinRT, WASAPI, NVENC, AMF, QuickSync       │ │ │
│  │  │ Linux (Future): PipeWire, X11/Wayland, VAAPI        │ │ │
│  │  └─────────────────────────────────────────────────────┘ │ │
│  └───────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

## Service Layer Design

### CaptureService Trait

The `CaptureService` trait defines the interface for screen/window capture across all platforms.

```rust
pub trait CaptureService: Send + Sync {
    /// Start replay buffer capture
    fn start_replay(&mut self, config: CaptureConfig) -> Result<CaptureSession, CaptureError>;
    
    /// Save the last N seconds from replay buffer
    fn save_clip(&mut self, session: &mut CaptureSession) -> Result<CapturedClip, CaptureError>;
    
    /// Stop capture and clean up resources
    fn stop(&mut self, session: &mut CaptureSession) -> Result<(), CaptureError>;
    
    /// Get list of available capture sources (monitors, windows)
    fn get_sources(&self) -> Result<Vec<CaptureSource>, CaptureError>;
    
    /// Check if the service is currently capturing
    fn is_capturing(&self) -> bool;
    
    /// Get backend name (e.g., "Windows.Graphics.Capture", "Linux PipeWire")
    fn backend_name(&self) -> &str;
    
    /// Get backend capabilities
    fn capabilities(&self) -> BackendCapabilities;
}

pub struct CaptureConfig {
    pub source: CaptureSource,
    pub duration: Duration,
    pub resolution: Option<(u32, u32)>,
    pub fps: Option<u32>,
    pub encoder: EncoderType,
}

pub enum CaptureSource {
    Monitor(MonitorId),
    Window(WindowId),
    Region { x: i32, y: i32, width: u32, height: u32 },
}

pub enum EncoderType {
    Auto,
    Nvenc,
    Amf,
    QuickSync,
    Software,
}
```

**Windows Implementation**: Uses `Windows.Graphics.Capture` API
**Linux Implementation (Future)**: Uses PipeWire + X11/Wayland APIs
**Fake Implementation**: Simulates capture for testing

### AudioMixerService Trait

The `AudioMixerService` trait defines the interface for audio capture and mixing.

```rust
pub trait AudioMixerService: Send + Sync {
    /// Add an audio source to the mixer
    fn add_source(&mut self, source: AudioSource) -> Result<SourceId, AudioError>;
    
    /// Remove an audio source
    fn remove_source(&mut self, source_id: &SourceId) -> Result<(), AudioError>;
    
    /// Set volume for a specific source (0.0 to 1.0)
    fn set_volume(&mut self, source_id: &SourceId, volume: f32) -> Result<(), AudioError>;
    
    /// Mute/unmute a source
    fn set_muted(&mut self, source_id: &SourceId, muted: bool) -> Result<(), AudioError>;
    
    /// Get list of available audio devices
    fn get_devices(&self) -> Result<Vec<AudioDevice>, AudioError>;
    
    /// Get current mixer state
    fn get_state(&self) -> MixerState;
    
    /// Start audio capture
    fn start_capture(&mut self) -> Result<(), AudioError>;
    
    /// Stop audio capture
    fn stop_capture(&mut self) -> Result<(), AudioError>;
    
    /// Get mixed audio buffer (for encoding)
    fn get_mixed_buffer(&mut self) -> Result<AudioBuffer, AudioError>;
}

pub enum AudioSource {
    SystemAudio(DeviceId),  // Loopback capture
    Microphone(DeviceId),
    Application(ProcessId),  // OBS-style app-specific capture
}

pub struct MixerState {
    pub sources: Vec<SourceState>,
    pub master_volume: f32,
    pub is_capturing: bool,
}
```

**Windows Implementation**: Uses WASAPI (Windows Audio Session API)
**Linux Implementation (Future)**: Uses PipeWire
**Fake Implementation**: Generates silence or test tones

### EncodingService Trait

The `EncodingService` trait defines the interface for video encoding.

```rust
pub trait EncodingService: Send + Sync {
    /// Initialize encoder with configuration
    fn initialize(&mut self, config: EncodingConfig) -> Result<(), EncodingError>;
    
    /// Encode a video frame
    fn encode_frame(&mut self, frame: &VideoFrame) -> Result<EncodedFrame, EncodingError>;
    
    /// Encode audio buffer
    fn encode_audio(&mut self, audio: &AudioBuffer) -> Result<EncodedAudio, EncodingError>;
    
    /// Finalize encoding and write to file
    fn finalize(&mut self, output_path: &Path) -> Result<OutputFile, EncodingError>;
    
    /// Get encoder type
    fn encoder_type(&self) -> EncoderType;
    
    /// Get encoder capabilities
    fn capabilities(&self) -> EncoderCapabilities;
}

pub struct EncodingConfig {
    pub codec: VideoCodec,  // H.264, H.265
    pub resolution: (u32, u32),
    pub fps: u32,
    pub bitrate: u32,  // kbps
    pub quality: QualityPreset,  // Low, Medium, High, Ultra
    pub audio_codec: AudioCodec,  // AAC, Opus
    pub audio_bitrate: u32,  // kbps
}
```

**NVENC Implementation**: NVIDIA GPU encoding
**AMF Implementation**: AMD GPU encoding
**QuickSync Implementation**: Intel GPU encoding
**Software Implementation**: x264/x265 CPU encoding

### HotkeyService Trait

The `HotkeyService` trait defines the interface for global hotkey registration.

```rust
pub trait HotkeyService: Send + Sync {
    /// Register a global hotkey
    fn register(&mut self, hotkey: Hotkey) -> Result<HotkeyId, HotkeyError>;
    
    /// Unregister a hotkey
    fn unregister(&mut self, hotkey_id: &HotkeyId) -> Result<(), HotkeyError>;
    
    /// Update hotkey configuration
    fn update(&mut self, hotkey_id: &HotkeyId, hotkey: Hotkey) -> Result<(), HotkeyError>;
    
    /// Get list of registered hotkeys
    fn get_registered(&self) -> Vec<HotkeyRegistration>;
    
    /// Set callback for hotkey events
    fn on_hotkey_pressed(&mut self, callback: Box<dyn Fn(HotkeyId) + Send>);
}

pub struct Hotkey {
    pub key: KeyCode,
    pub modifiers: Vec<Modifier>,  // Ctrl, Alt, Shift, Win
}

pub enum KeyCode {
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    // ... other keys
}

pub enum Modifier {
    Ctrl,
    Alt,
    Shift,
    Win,
}
```

**Windows Implementation**: Uses Win32 `RegisterHotKey` API
**Linux Implementation (Future)**: Uses X11/Wayland APIs
**Fake Implementation**: Simulates hotkey presses

### GameDetector Service

The `GameDetector` service detects running games for automatic capture suggestions.

```rust
pub trait GameDetector: Send + Sync {
    /// Scan for running games
    fn scan(&self) -> Result<Vec<DetectedGame>, GameDetectorError>;
    
    /// Check if a specific process is a known game
    fn is_game(&self, process_id: ProcessId) -> Result<bool, GameDetectorError>;
    
    /// Add a custom game to the database
    fn add_custom_game(&mut self, game: CustomGame) -> Result<GameId, GameDetectorError>;
    
    /// Get known games database
    fn get_database(&self) -> &GameDatabase;
}

pub struct DetectedGame {
    pub process_id: ProcessId,
    pub window_id: Option<WindowId>,
    pub name: String,
    pub executable: String,
    pub is_active: bool,  // Window is focused
}
```

**Implementation**: Process scanning + window title matching + known game database

### LibraryService

The `LibraryService` manages clip storage, metadata, and search.

```rust
pub trait LibraryService: Send + Sync {
    /// Save clip metadata to database
    fn save_clip(&mut self, clip: ClipMetadata) -> Result<ClipId, LibraryError>;
    
    /// Get clip by ID
    fn get_clip(&self, clip_id: &ClipId) -> Result<ClipMetadata, LibraryError>;
    
    /// Get all clips (with pagination)
    fn get_clips(&self, page: u32, per_page: u32) -> Result<Vec<ClipMetadata>, LibraryError>;
    
    /// Search clips
    fn search(&self, query: &str, filters: SearchFilters) -> Result<Vec<ClipMetadata>, LibraryError>;
    
    /// Delete clip (file + metadata)
    fn delete_clip(&mut self, clip_id: &ClipId) -> Result<(), LibraryError>;
    
    /// Update clip metadata (tags, favorite, etc.)
    fn update_clip(&mut self, clip_id: &ClipId, update: ClipUpdate) -> Result<(), LibraryError>;
    
    /// Get storage statistics
    fn get_storage_stats(&self) -> StorageStats;
}

pub struct ClipMetadata {
    pub id: ClipId,
    pub filename: String,
    pub path: PathBuf,
    pub duration: Duration,
    pub size: u64,  // bytes
    pub resolution: (u32, u32),
    pub fps: u32,
    pub codec: VideoCodec,
    pub created_at: DateTime<Utc>,
    pub game: Option<String>,
    pub tags: Vec<String>,
    pub is_favorite: bool,
}
```

**Implementation**: SQLite database with FTS (Full-Text Search)

### ConfigService

The `ConfigService` manages application settings persistence.

```rust
pub trait ConfigService: Send + Sync {
    /// Load configuration from disk
    fn load(&self) -> Result<AppConfig, ConfigError>;
    
    /// Save configuration to disk
    fn save(&self, config: &AppConfig) -> Result<(), ConfigError>;
    
    /// Get a specific setting
    fn get<T: DeserializeOwned>(&self, key: &str) -> Result<T, ConfigError>;
    
    /// Set a specific setting
    fn set<T: Serialize>(&mut self, key: &str, value: T) -> Result<(), ConfigError>;
    
    /// Reset to default configuration
    fn reset(&mut self) -> Result<(), ConfigError>;
    
    /// Import configuration from file
    fn import(&mut self, path: &Path) -> Result<(), ConfigError>;
    
    /// Export configuration to file
    fn export(&self, path: &Path) -> Result<(), ConfigError>;
}
```

**Implementation**: JSON configuration file with migration support

## Data Flow

### Capture Flow

```
1. User clicks "Start Capture" in UI
   ↓
2. Frontend sends IPC command: start_capture(config)
   ↓
3. Backend CaptureService.start_replay(config)
   ├─ Initialize platform-specific capture API
   ├─ Set up replay buffer (circular buffer in RAM)
   ├─ Start frame capture loop
   └─ Return CaptureSession
   ↓
4. User presses hotkey (F8)
   ↓
5. Backend HotkeyService triggers callback
   ↓
6. Backend CaptureService.save_clip(session)
   ├─ Extract last N seconds from buffer
   ├─ Encode video with EncodingService
   ├─ Mix audio with AudioMixerService
   ├─ Write to file
   └─ Save metadata with LibraryService
   ↓
7. Backend sends IPC response: clip saved
   ↓
8. Frontend shows notification
```

### Audio Mixing Flow

```
1. User adds audio sources in AudioMixer view
   ↓
2. Frontend sends IPC command: add_audio_source(source)
   ↓
3. Backend AudioMixerService.add_source(source)
   ├─ Enumerate available devices (WASAPI on Windows)
   ├─ Initialize audio capture for each source
   ├─ Set up audio buffers
   └─ Return SourceId
   ↓
4. User adjusts volumes
   ↓
5. Frontend sends IPC command: set_volume(source_id, volume)
   ↓
6. Backend AudioMixerService.set_volume(source_id, volume)
   ├─ Update volume for specific source
   └─ Return success
   ↓
7. During capture, audio frames are captured continuously
   ↓
8. AudioMixerService.get_mixed_buffer()
   ├─ Get buffers from all sources
   ├─ Apply volume adjustments
   ├─ Mix all sources into single buffer
   └─ Return mixed AudioBuffer
   ↓
9. EncodingService.encode_audio(mixed_buffer)
   ├─ Encode to AAC or Opus
   └─ Return EncodedAudio
```

### IPC Communication

All IPC communication uses Tauri's type-safe command system:

```rust
// Backend command
#[tauri::command]
async fn start_capture(
    config: CaptureConfig,
    state: State<'_, AppState>,
) -> Result<CaptureSession, String> {
    let mut capture_service = state.capture_service.lock().unwrap();
    capture_service.start_replay(config).map_err(|e| e.to_string())
}

// Frontend call
const session = await invoke('start_capture', { config });
```

**Benefits**:
- Type safety (compile-time checks)
- No manual serialization/deserialization
- Error handling with Result types
- Async/await support

## State Management

### Application State

```rust
pub struct AppState {
    pub capture_service: Arc<Mutex<Box<dyn CaptureService>>>,
    pub audio_mixer: Arc<Mutex<Box<dyn AudioMixerService>>>,
    pub encoding_service: Arc<Mutex<Box<dyn EncodingService>>>,
    pub hotkey_service: Arc<Mutex<Box<dyn HotkeyService>>>,
    pub game_detector: Arc<Mutex<Box<dyn GameDetector>>>,
    pub library_service: Arc<Mutex<Box<dyn LibraryService>>>,
    pub config_service: Arc<Mutex<Box<dyn ConfigService>>>,
}
```

**Thread Safety**: All services are wrapped in `Arc<Mutex<...>>` for safe concurrent access.

**Initialization**:
```rust
pub fn create_app_state() -> AppState {
    AppState {
        capture_service: Arc::new(Mutex::new(Box::new(WindowsCaptureBackend::new()))),
        audio_mixer: Arc::new(Mutex::new(Box::new(WindowsAudioMixer::new()))),
        encoding_service: Arc::new(Mutex::new(Box::new(NvencEncoder::new()))),
        hotkey_service: Arc::new(Mutex::new(Box::new(WindowsHotkeyService::new()))),
        game_detector: Arc::new(Mutex::new(Box::new(GameDetectorImpl::new()))),
        library_service: Arc::new(Mutex::new(Box::new(LibraryServiceImpl::new()))),
        config_service: Arc::new(Mutex::new(Box::new(ConfigServiceImpl::new()))),
    }
}
```

## Error Handling

### Error Types

Each service defines its own error type:

```rust
#[derive(Debug, thiserror::Error)]
pub enum CaptureError {
    #[error("Permission denied: {0}")]
    PermissionDenied(String),
    #[error("Source not found: {0}")]
    SourceNotFound(String),
    #[error("Capture failed: {0}")]
    CaptureFailed(String),
    #[error("Buffer overflow")]
    BufferOverflow,
    #[error("Encoder error: {0}")]
    EncoderError(String),
}

#[derive(Debug, thiserror::Error)]
pub enum AudioError {
    #[error("Device not found: {0}")]
    DeviceNotFound(String),
    #[error("Device busy: {0}")]
    DeviceBusy(String),
    #[error("Audio capture failed: {0}")]
    CaptureFailed(String),
    #[error("Mixing error: {0}")]
    MixingError(String),
}
```

### Error Propagation

Errors are propagated to the frontend via IPC:

```rust
#[tauri::command]
async fn start_capture(config: CaptureConfig) -> Result<CaptureSession, String> {
    // ... implementation
    match capture_service.start_replay(config) {
        Ok(session) => Ok(session),
        Err(CaptureError::PermissionDenied(msg)) => Err(format!("Permission denied: {}", msg)),
        Err(e) => Err(e.to_string()),
    }
}
```

### User-Friendly Messages

Backend errors are converted to user-friendly messages in the frontend:

```typescript
try {
  await invoke('start_capture', { config });
} catch (error) {
  if (error.includes('Permission denied')) {
    showPermissionDialog();
  } else {
    showErrorMessage(error);
  }
}
```

## Performance Considerations

### Frame Capture
- **Zero-copy**: Windows.Graphics.Capture provides zero-copy frame delivery
- **Circular buffer**: Replay buffer uses circular buffer in RAM
- **Async encoding**: Encoding happens in separate thread to avoid blocking capture

### Audio Capture
- **Low latency**: WASAPI provides < 10ms latency
- **Real-time mixing**: Audio mixing happens in real-time
- **Buffer management**: Ring buffers prevent audio glitches

### GPU Encoding
- **Hardware acceleration**: NVENC/AMF/QuickSync use dedicated GPU hardware
- **Low CPU usage**: GPU encoding uses < 5% CPU
- **High throughput**: Can encode 4K60 in real-time

### Memory Management
- **Capture buffer**: 30 seconds of 1080p60 ≈ 500 MB RAM
- **Audio buffer**: 30 seconds of audio ≈ 10 MB RAM
- **Encoding buffer**: Temporary buffers ≈ 100 MB RAM
- **Total**: < 1 GB RAM for typical usage

## Security Considerations

### No Code Injection
- Never execute user-controlled strings
- All inputs are validated and sanitized
- No shell command execution

### No Game Injection
- Never inject into game processes
- No anti-cheat bypass attempts
- No memory manipulation

### File System Access
- Clips stored in user's Videos folder
- Config stored in AppData
- No system-wide modifications

### Network Access
- No telemetry or analytics
- No automatic updates (manual only)
- No cloud storage

### Permissions
- No administrator privileges required
- Current user installation only
- Capture permission requested from Windows

## Testing Strategy

### Unit Tests
- Test each service implementation independently
- Test with FakeBackend implementations
- Mock platform APIs

### Integration Tests
- Test IPC communication
- Test service interactions
- Test data flow

### End-to-End Tests
- Test full capture workflow
- Test with real Windows APIs
- Test performance and stability

### Platform Testing
- Test on Windows 10 and Windows 11
- Test with different GPUs (NVIDIA, AMD, Intel)
- Test with different audio devices

## Future Enhancements

### Linux Support
- Implement `LinuxCaptureBackend` using PipeWire
- Implement `LinuxAudioMixer` using PipeWire
- Implement `LinuxHotkeyService` using X11/Wayland
- Create DEB and RPM packages

### Additional Features
- Streaming support (RTMP, HLS)
- Overlay graphics (webcam, alerts)
- Advanced editing (trim, cut, merge)
- Cloud storage integration
- Social media sharing
- Plugin system

### Performance Optimizations
- GPU-accelerated audio processing
- Multi-threaded encoding
- Hardware-accelerated scaling
- Zero-copy frame processing

## References

- [Tauri Architecture](https://v2.tauri.app/concept/architecture/)
- [Windows.Graphics.Capture](https://docs.microsoft.com/windows/win32/direct3d11/windows-graphics-capture)
- [WASAPI](https://docs.microsoft.com/windows/win32/coreaudio/wasapi)
- [NVENC SDK](https://developer.nvidia.com/nvidia-video-codec-sdk)
- [AMD AMF](https://gpuopen.com/advanced-media-framework/)
- [Intel Quick Sync](https://software.intel.com/content/www/us/en/develop/documentation/video-tutorial/getting-started-with-intel-quick-sync-video.html)
