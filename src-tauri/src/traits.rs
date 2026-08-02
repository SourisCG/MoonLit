//! Portable replay backend contract.
//!
//! Native resources and encoded frame data stay inside a backend. Only
//! descriptors, source metadata and completed clip metadata cross this layer.

use std::fmt;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum BackendId {
    Fake,
    #[serde(rename = "libobsSidecar")]
    LibobsSidecar,
    #[serde(rename = "windowsNative")]
    WindowsNative,
    #[serde(rename = "legacyGsr")]
    LegacyGsr,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum CaptureSourceKind {
    Monitor,
    Window,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CaptureSource {
    pub id: String,
    pub kind: CaptureSourceKind,
    pub label: String,
    pub is_default: bool,
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub process_name: Option<String>,
    pub available: bool,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct VideoResolution {
    pub width: u32,
    pub height: u32,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum VideoCodec {
    H264,
    Hevc,
}

#[derive(Clone, Debug, Default, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum ContainerFormat {
    #[default]
    Mp4,
    Mkv,
}

#[derive(Clone, Debug, Default, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum QualityPreset {
    Low,
    #[default]
    Medium,
    High,
    Ultra,
    Custom,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AudioConfig {
    pub system_enabled: bool,
    pub microphone_enabled: bool,
    pub system_device_id: Option<String>,
    pub microphone_device_id: Option<String>,
    pub system_gain: f32,
    pub microphone_gain: f32,
    pub system_muted: bool,
    pub microphone_muted: bool,
    pub bitrate_kbps: u32,
}

impl Default for AudioConfig {
    fn default() -> Self {
        Self {
            system_enabled: true,
            microphone_enabled: false,
            system_device_id: None,
            microphone_device_id: None,
            system_gain: 1.0,
            microphone_gain: 1.0,
            system_muted: false,
            microphone_muted: false,
            bitrate_kbps: 160,
        }
    }
}

#[derive(Clone, Debug, Default, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AudioCapabilities {
    pub available: bool,
    pub system_audio: bool,
    pub microphone: bool,
    pub application_audio: bool,
    pub note: Option<String>,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum EncoderPreference {
    Auto,
    Nvenc,
    Amf,
    QuickSync,
    Software,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct EncoderCapability {
    pub id: EncoderPreference,
    pub available: bool,
    pub reason: Option<String>,
}

#[derive(Clone, Debug, Default, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct BackendCapabilities {
    pub source_kinds: Vec<CaptureSourceKind>,
    pub max_resolution: Option<VideoResolution>,
    pub max_fps: Option<u32>,
    pub encoders: Vec<EncoderCapability>,
    pub codecs: Vec<VideoCodec>,
    pub formats: Vec<ContainerFormat>,
    pub audio: AudioCapabilities,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct BackendDescriptor {
    pub id: BackendId,
    pub display_name: String,
    pub available: bool,
    pub simulated: bool,
    pub capabilities: BackendCapabilities,
    pub note: Option<String>,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ReplayConfig {
    pub source_id: String,
    pub buffer_seconds: u32,
    pub resolution: Option<VideoResolution>,
    pub fps: Option<u32>,
    pub encoder: EncoderPreference,
    pub codec: VideoCodec,
    pub format: ContainerFormat,
    pub quality: QualityPreset,
    pub bitrate_kbps: Option<u32>,
    pub audio: AudioConfig,
}

impl Default for ReplayConfig {
    fn default() -> Self {
        Self {
            source_id: "fake-monitor-1".to_string(),
            buffer_seconds: 30,
            resolution: None,
            fps: None,
            encoder: EncoderPreference::Auto,
            codec: VideoCodec::H264,
            format: ContainerFormat::Mp4,
            quality: QualityPreset::Medium,
            bitrate_kbps: None,
            audio: AudioConfig::default(),
        }
    }
}

impl ReplayConfig {
    pub fn validate(&self, sources: &[CaptureSource]) -> Result<(), BackendError> {
        if !(10..=300).contains(&self.buffer_seconds) {
            return Err(BackendError::invalid_config(
                "El buffer debe estar entre 10 y 300 segundos",
            ));
        }
        if self.source_id.trim().is_empty() {
            return Err(BackendError::invalid_config(
                "La fuente de captura no puede estar vacia",
            ));
        }
        if !sources.iter().any(|source| source.id == self.source_id) {
            return Err(BackendError::source_not_found(&self.source_id));
        }
        if self.fps.is_some_and(|fps| fps == 0) {
            return Err(BackendError::invalid_config(
                "Los FPS deben ser mayores que cero",
            ));
        }
        if self
            .resolution
            .as_ref()
            .is_some_and(|resolution| resolution.width == 0 || resolution.height == 0)
        {
            return Err(BackendError::invalid_config(
                "La resolucion debe ser mayor que cero",
            ));
        }
        if self
            .bitrate_kbps
            .is_some_and(|bitrate| !(100..=200_000).contains(&bitrate))
        {
            return Err(BackendError::invalid_config(
                "El bitrate debe estar entre 100 y 200000 kbps",
            ));
        }
        if !(0.0..=4.0).contains(&self.audio.system_gain)
            || !(0.0..=4.0).contains(&self.audio.microphone_gain)
        {
            return Err(BackendError::invalid_config(
                "La ganancia de audio debe estar entre 0 y 4",
            ));
        }
        if !(32..=512).contains(&self.audio.bitrate_kbps) {
            return Err(BackendError::invalid_config(
                "El bitrate de audio debe estar entre 32 y 512 kbps",
            ));
        }
        Ok(())
    }
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct EffectiveReplaySettings {
    pub encoder: String,
    pub codec: VideoCodec,
    pub format: ContainerFormat,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum ClipKind {
    Simulation,
    #[allow(dead_code)]
    Media,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ClipArtifact {
    pub path: PathBuf,
    pub duration_seconds: u32,
    pub kind: ClipKind,
    pub codec: VideoCodec,
    pub format: ContainerFormat,
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub fps: Option<u32>,
    pub has_audio: bool,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum BackendErrorCode {
    InvalidConfig,
    InvalidState,
    BackendUnavailable,
    PermissionDenied,
    SourceNotFound,
    SourceEnded,
    EncoderUnavailable,
    Io,
    Timeout,
    BackendExited,
    Unsupported,
    Internal,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct BackendError {
    pub code: BackendErrorCode,
    pub message: String,
    pub retryable: bool,
}

impl BackendError {
    pub fn new(code: BackendErrorCode, message: impl Into<String>, retryable: bool) -> Self {
        Self {
            code,
            message: message.into(),
            retryable,
        }
    }

    pub fn invalid_config(message: impl Into<String>) -> Self {
        Self::new(BackendErrorCode::InvalidConfig, message, false)
    }

    pub fn invalid_state(message: impl Into<String>) -> Self {
        Self::new(BackendErrorCode::InvalidState, message, true)
    }

    pub fn backend_unavailable(message: impl Into<String>) -> Self {
        Self::new(BackendErrorCode::BackendUnavailable, message, true)
    }

    pub fn source_not_found(source_id: &str) -> Self {
        Self::new(
            BackendErrorCode::SourceNotFound,
            format!("No se encontro la fuente '{source_id}'"),
            true,
        )
    }

    pub fn io(message: impl Into<String>) -> Self {
        Self::new(BackendErrorCode::Io, message, true)
    }
}

impl fmt::Display for BackendError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.message)
    }
}

impl std::error::Error for BackendError {}

impl From<std::io::Error> for BackendError {
    fn from(error: std::io::Error) -> Self {
        Self::io(error.to_string())
    }
}

/// Owns the complete capture, encode and replay pipeline for one backend.
pub trait ReplayBackend: Send {
    fn descriptor(&self) -> BackendDescriptor;
    fn list_sources(&self) -> Result<Vec<CaptureSource>, BackendError>;
    fn start(&mut self, config: &ReplayConfig, output_dir: &Path) -> Result<(), BackendError>;

    fn effective_settings(&self) -> Option<EffectiveReplaySettings> {
        None
    }

    fn can_save(&self) -> bool {
        true
    }

    fn save_replay(&mut self) -> Result<ClipArtifact, BackendError>;
    fn stop(&mut self) -> Result<(), BackendError>;

    fn poll_health(&mut self) -> Result<(), BackendError> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::{BackendId, CaptureSource, CaptureSourceKind, ReplayConfig};

    fn sources() -> Vec<CaptureSource> {
        vec![CaptureSource {
            id: "monitor-1".to_string(),
            kind: CaptureSourceKind::Monitor,
            label: "Monitor 1".to_string(),
            is_default: true,
            width: Some(1920),
            height: Some(1080),
            process_name: None,
            available: true,
        }]
    }

    #[test]
    fn default_replay_config_is_valid_for_a_known_source() {
        let config = ReplayConfig {
            source_id: "monitor-1".to_string(),
            ..ReplayConfig::default()
        };
        assert!(config.validate(&sources()).is_ok());
    }

    #[test]
    fn replay_config_rejects_unknown_source() {
        let error = ReplayConfig::default()
            .validate(&sources())
            .expect_err("unknown source must be rejected");
        assert_eq!(error.code, super::BackendErrorCode::SourceNotFound);
    }

    #[test]
    fn sidecar_backend_id_has_a_stable_wire_name() {
        let json = serde_json::to_string(&BackendId::LibobsSidecar).expect("serialize backend id");
        assert_eq!(json, "\"libobsSidecar\"");
        assert_eq!(
            serde_json::from_str::<BackendId>(&json).expect("deserialize backend id"),
            BackendId::LibobsSidecar
        );
    }
}
