//! Portable traits for MoonLit backend services
//!
//! These traits define platform-agnostic interfaces that can be implemented
//! for different operating systems (Windows, Linux, macOS).

#![allow(dead_code)]

use std::path::PathBuf;
use std::time::Duration;

/// Configuration for video encoding
#[derive(Debug, Clone)]
pub struct EncodingConfig {
    pub codec: VideoCodec,
    pub resolution: (u32, u32),
    pub fps: u32,
    pub bitrate: u32,
    pub quality: QualityPreset,
}

/// Video codec options
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum VideoCodec {
    H264,
    H265,
}

/// Quality presets for encoding
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum QualityPreset {
    Low,
    Medium,
    High,
    Ultra,
}

/// Type of encoder to use
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EncoderType {
    Auto,
    Nvenc,
    Amf,
    QuickSync,
    Software,
}

/// Errors that can occur during capture operations
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

    #[error("IO error: {0}")]
    IoError(#[from] std::io::Error),
}

/// Errors that can occur during audio operations
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

/// Errors that can occur during encoding operations
#[derive(Debug, thiserror::Error)]
pub enum EncodingError {
    #[error("Encoder initialization failed: {0}")]
    InitializationFailed(String),

    #[error("Encoding failed: {0}")]
    EncodingFailed(String),

    #[error("GPU not available: {0}")]
    GpuNotAvailable(String),

    #[error("Unsupported codec: {0}")]
    UnsupportedCodec(String),
}

/// Trait for platform-specific capture implementations
pub trait CaptureService: Send + Sync {
    /// Start replay buffer capture
    fn start_replay(&mut self, config: CaptureConfig) -> Result<CaptureSession, CaptureError>;

    /// Save the last N seconds from replay buffer
    fn save_clip(&mut self, session: &mut CaptureSession) -> Result<PathBuf, CaptureError>;

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

/// Configuration for capture
#[derive(Debug, Clone)]
pub struct CaptureConfig {
    pub source: CaptureSource,
    pub duration: Duration,
    pub resolution: Option<(u32, u32)>,
    pub fps: Option<u32>,
    pub encoder: EncoderType,
}

/// Type of capture source
#[derive(Debug, Clone)]
pub enum CaptureSource {
    Monitor(String),
    Window(String),
    Region {
        x: i32,
        y: i32,
        width: u32,
        height: u32,
    },
}

/// Active capture session
#[derive(Debug)]
pub struct CaptureSession {
    pub id: String,
    pub source: CaptureSource,
    pub start_time: std::time::Instant,
    pub duration: Duration,
}

/// Capabilities of a capture backend
#[derive(Debug, Clone)]
pub struct BackendCapabilities {
    pub supports_window_capture: bool,
    pub supports_monitor_capture: bool,
    pub supports_region_capture: bool,
    pub max_resolution: Option<(u32, u32)>,
    pub max_fps: Option<u32>,
    pub supported_codecs: Vec<VideoCodec>,
}

/// Trait for platform-specific audio mixing implementations
pub trait AudioMixerService: Send + Sync {
    /// Add an audio source to the mixer
    fn add_source(&mut self, source: AudioSource) -> Result<String, AudioError>;

    /// Remove an audio source
    fn remove_source(&mut self, source_id: &str) -> Result<(), AudioError>;

    /// Set volume for a specific source (0.0 to 1.0)
    fn set_volume(&mut self, source_id: &str, volume: f32) -> Result<(), AudioError>;

    /// Mute/unmute a source
    fn set_muted(&mut self, source_id: &str, muted: bool) -> Result<(), AudioError>;

    /// Get list of available audio devices
    fn get_devices(&self) -> Result<Vec<AudioDevice>, AudioError>;

    /// Get current mixer state
    fn get_state(&self) -> MixerState;

    /// Start audio capture
    fn start_capture(&mut self) -> Result<(), AudioError>;

    /// Stop audio capture
    fn stop_capture(&mut self) -> Result<(), AudioError>;
}

/// Type of audio source
#[derive(Debug, Clone)]
pub enum AudioSource {
    SystemAudio(String),
    Microphone(String),
    Application(String),
}

/// Audio device information
#[derive(Debug, Clone)]
pub struct AudioDevice {
    pub id: String,
    pub name: String,
    pub device_type: AudioDeviceType,
    pub is_default: bool,
}

/// Type of audio device
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AudioDeviceType {
    Input,
    Output,
}

/// Current state of the audio mixer
#[derive(Debug, Clone)]
pub struct MixerState {
    pub sources: Vec<SourceState>,
    pub master_volume: f32,
    pub is_capturing: bool,
}

/// State of an individual audio source
#[derive(Debug, Clone)]
pub struct SourceState {
    pub id: String,
    pub name: String,
    pub volume: f32,
    pub muted: bool,
}

/// Trait for platform-specific hotkey implementations
pub trait HotkeyService: Send + Sync {
    /// Register a global hotkey
    fn register(&mut self, hotkey: Hotkey) -> Result<String, HotkeyError>;

    /// Unregister a hotkey
    fn unregister(&mut self, hotkey_id: &str) -> Result<(), HotkeyError>;

    /// Update hotkey configuration
    fn update(&mut self, hotkey_id: &str, hotkey: Hotkey) -> Result<(), HotkeyError>;

    /// Get list of registered hotkeys
    fn get_registered(&self) -> Vec<HotkeyRegistration>;
}

/// Hotkey configuration
#[derive(Debug, Clone)]
pub struct Hotkey {
    pub key: KeyCode,
    pub modifiers: Vec<Modifier>,
    pub action: HotkeyAction,
}

/// Keyboard key codes
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum KeyCode {
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Num0,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,
}

/// Keyboard modifiers
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Modifier {
    Ctrl,
    Alt,
    Shift,
    Win,
}

/// Action to perform when hotkey is pressed
#[derive(Debug, Clone)]
pub enum HotkeyAction {
    SaveClip,
    StartCapture,
    StopCapture,
    ToggleCapture,
    MuteMic,
}

/// Registered hotkey information
#[derive(Debug, Clone)]
pub struct HotkeyRegistration {
    pub id: String,
    pub hotkey: Hotkey,
}

/// Errors that can occur during hotkey operations
#[derive(Debug, thiserror::Error)]
pub enum HotkeyError {
    #[error("Hotkey registration failed: {0}")]
    RegistrationFailed(String),

    #[error("Hotkey not found: {0}")]
    NotFound(String),

    #[error("Hotkey conflict: {0}")]
    Conflict(String),
}

/// Trait for game detection
pub trait GameDetector: Send + Sync {
    /// Scan for running games
    fn scan(&self) -> Result<Vec<DetectedGame>, GameDetectorError>;

    /// Check if a specific process is a known game
    fn is_game(&self, process_name: &str) -> bool;

    /// Get known games database
    fn get_database(&self) -> &GameDatabase;
}

/// Information about a detected game
#[derive(Debug, Clone)]
pub struct DetectedGame {
    pub process_id: u32,
    pub process_name: String,
    pub window_title: Option<String>,
    pub game_name: String,
    pub is_active: bool,
}

/// Database of known games
#[derive(Debug, Clone)]
pub struct GameDatabase {
    pub games: Vec<KnownGame>,
}

/// Information about a known game
#[derive(Debug, Clone)]
pub struct KnownGame {
    pub name: String,
    pub process_names: Vec<String>,
    pub window_titles: Vec<String>,
}

/// Errors that can occur during game detection
#[derive(Debug, thiserror::Error)]
pub enum GameDetectorError {
    #[error("Detection failed: {0}")]
    DetectionFailed(String),

    #[error("Database error: {0}")]
    DatabaseError(String),
}
