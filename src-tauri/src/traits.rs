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

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ReplayConfig {
    pub source_id: String,
    pub buffer_seconds: u32,
    pub resolution: Option<VideoResolution>,
    pub fps: Option<u32>,
    pub encoder: EncoderPreference,
    pub codec: VideoCodec,
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
        Ok(())
    }
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
    fn save_replay(&mut self) -> Result<ClipArtifact, BackendError>;
    fn stop(&mut self) -> Result<(), BackendError>;
}

#[cfg(test)]
mod tests {
    use super::{CaptureSource, CaptureSourceKind, ReplayConfig};

    fn sources() -> Vec<CaptureSource> {
        vec![CaptureSource {
            id: "monitor-1".to_string(),
            kind: CaptureSourceKind::Monitor,
            label: "Monitor 1".to_string(),
            is_default: true,
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
}
