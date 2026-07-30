//! Deterministic replay backend for development and contract testing.

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

use serde::Serialize;

use crate::traits::{
    AudioCapabilities, BackendCapabilities, BackendDescriptor, BackendError, BackendId,
    CaptureSource, CaptureSourceKind, ClipArtifact, ClipKind, ContainerFormat, EncoderCapability,
    EncoderPreference, ReplayBackend, ReplayConfig, VideoCodec, VideoResolution,
};

static NEXT_ID: AtomicU64 = AtomicU64::new(0);

#[derive(Default)]
pub struct FakeBackend {
    session: Option<FakeSession>,
}

struct FakeSession {
    config: ReplayConfig,
    output_dir: PathBuf,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct SimulationManifest<'a> {
    id: &'a str,
    created_at_ms: u64,
    duration_seconds: u32,
    backend: &'a str,
    source_id: &'a str,
    codec: &'a str,
    format: &'a str,
    audio: bool,
    note: &'static str,
}

impl FakeBackend {
    pub fn new() -> Self {
        Self::default()
    }

    fn sources() -> Vec<CaptureSource> {
        vec![
            CaptureSource {
                id: "fake-monitor-1".to_string(),
                kind: CaptureSourceKind::Monitor,
                label: "Fake Monitor 1".to_string(),
                is_default: true,
                width: Some(1920),
                height: Some(1080),
                process_name: None,
                available: true,
            },
            CaptureSource {
                id: "fake-monitor-2".to_string(),
                kind: CaptureSourceKind::Monitor,
                label: "Fake Monitor 2".to_string(),
                is_default: false,
                width: Some(2560),
                height: Some(1440),
                process_name: None,
                available: true,
            },
            CaptureSource {
                id: "fake-window-1".to_string(),
                kind: CaptureSourceKind::Window,
                label: "Fake Window 1".to_string(),
                is_default: false,
                width: Some(1280),
                height: Some(720),
                process_name: Some("fake-game.exe".to_string()),
                available: true,
            },
        ]
    }

    fn descriptor_value() -> BackendDescriptor {
        BackendDescriptor {
            id: BackendId::Fake,
            display_name: "Simulado".to_string(),
            available: true,
            simulated: true,
            capabilities: BackendCapabilities {
                source_kinds: vec![CaptureSourceKind::Monitor, CaptureSourceKind::Window],
                max_resolution: Some(VideoResolution {
                    width: 3840,
                    height: 2160,
                }),
                max_fps: Some(144),
                encoders: vec![
                    EncoderCapability {
                        id: EncoderPreference::Auto,
                        available: true,
                        reason: None,
                    },
                    EncoderCapability {
                        id: EncoderPreference::Software,
                        available: true,
                        reason: None,
                    },
                ],
                codecs: vec![VideoCodec::H264, VideoCodec::Hevc],
                formats: vec![ContainerFormat::Mp4, ContainerFormat::Mkv],
                audio: AudioCapabilities {
                    available: true,
                    system_audio: true,
                    microphone: true,
                    application_audio: false,
                    note: Some("Audio simulado para pruebas de UI".to_string()),
                },
            },
            note: Some(
                "No produce video real; escribe manifests para probar el flujo.".to_string(),
            ),
        }
    }
}

impl ReplayBackend for FakeBackend {
    fn descriptor(&self) -> BackendDescriptor {
        Self::descriptor_value()
    }

    fn list_sources(&self) -> Result<Vec<CaptureSource>, BackendError> {
        Ok(Self::sources())
    }

    fn start(&mut self, config: &ReplayConfig, output_dir: &Path) -> Result<(), BackendError> {
        if self.session.is_some() {
            return Err(BackendError::invalid_state(
                "El backend simulado ya esta capturando",
            ));
        }

        let sources = Self::sources();
        config.validate(&sources)?;
        if !matches!(
            config.encoder,
            EncoderPreference::Auto | EncoderPreference::Software
        ) {
            return Err(BackendError::new(
                crate::traits::BackendErrorCode::EncoderUnavailable,
                "El backend simulado solo admite Auto o Software",
                false,
            ));
        }
        self.session = Some(FakeSession {
            config: config.clone(),
            output_dir: output_dir.to_path_buf(),
        });
        Ok(())
    }

    fn save_replay(&mut self) -> Result<ClipArtifact, BackendError> {
        let session = self
            .session
            .as_ref()
            .ok_or_else(|| BackendError::invalid_state("Inicia el buffer antes de guardar"))?;
        let id = unique_id("sim");
        let clips_dir = session.output_dir.join("simulated-clips");
        fs::create_dir_all(&clips_dir).map_err(|error| {
            BackendError::io(format!("No se pudo crear el directorio: {error}"))
        })?;

        let extension = match session.config.format {
            ContainerFormat::Mp4 => "mp4",
            ContainerFormat::Mkv => "mkv",
        };
        let path = clips_dir.join(format!("{id}.{extension}.json"));
        let codec = match session.config.codec {
            VideoCodec::H264 => "h264",
            VideoCodec::Hevc => "hevc",
        };
        let format = match session.config.format {
            ContainerFormat::Mp4 => "mp4",
            ContainerFormat::Mkv => "mkv",
        };
        let manifest = SimulationManifest {
            id: &id,
            created_at_ms: now_millis(),
            duration_seconds: session.config.buffer_seconds,
            backend: "fake",
            source_id: &session.config.source_id,
            codec,
            format,
            audio: session.config.audio.system_enabled || session.config.audio.microphone_enabled,
            note: "Manifest generado por FakeBackend; no contiene video real.",
        };
        let contents = serde_json::to_vec_pretty(&manifest).map_err(|error| {
            BackendError::io(format!("No se pudo serializar el manifest: {error}"))
        })?;
        write_atomic(&path, &contents)?;

        Ok(ClipArtifact {
            path,
            duration_seconds: session.config.buffer_seconds,
            kind: ClipKind::Simulation,
            codec: session.config.codec.clone(),
            format: session.config.format.clone(),
            width: session.config.resolution.as_ref().map(|value| value.width),
            height: session.config.resolution.as_ref().map(|value| value.height),
            fps: session.config.fps,
            has_audio: session.config.audio.system_enabled
                || session.config.audio.microphone_enabled,
        })
    }

    fn stop(&mut self) -> Result<(), BackendError> {
        self.session = None;
        Ok(())
    }
}

fn write_atomic(path: &Path, contents: &[u8]) -> Result<(), BackendError> {
    let temporary_path = path.with_extension("json.tmp");
    fs::write(&temporary_path, contents).map_err(|error| {
        BackendError::io(format!("No se pudo escribir el manifest temporal: {error}"))
    })?;
    if let Err(error) = fs::rename(&temporary_path, path) {
        let _ = fs::remove_file(&temporary_path);
        return Err(BackendError::io(format!(
            "No se pudo finalizar el manifest: {error}"
        )));
    }
    Ok(())
}

fn now_millis() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
}

fn unique_id(prefix: &str) -> String {
    format!(
        "{prefix}-{}-{}",
        now_millis(),
        NEXT_ID.fetch_add(1, Ordering::Relaxed)
    )
}

#[cfg(test)]
mod tests {
    use std::fs;

    use super::{FakeBackend, ReplayBackend};
    use crate::traits::{BackendErrorCode, BackendId, ReplayConfig};

    fn temporary_directory() -> std::path::PathBuf {
        let path =
            std::env::temp_dir().join(format!("moonlit-contract-{}", super::unique_id("dir")));
        fs::create_dir_all(&path).expect("temporary directory");
        path
    }

    #[test]
    fn fake_backend_exposes_stable_sources_and_capabilities() {
        let backend = FakeBackend::new();
        assert_eq!(backend.descriptor().id, BackendId::Fake);
        assert_eq!(backend.list_sources().expect("sources").len(), 3);
    }

    #[test]
    fn fake_backend_saves_an_atomic_simulation_manifest() {
        let directory = temporary_directory();
        let mut backend = FakeBackend::new();
        let config = ReplayConfig::default();

        backend.start(&config, &directory).expect("start fake");
        let clip = backend.save_replay().expect("save fake");
        assert!(clip.path.exists());
        assert_eq!(clip.kind, crate::traits::ClipKind::Simulation);
        assert!(!clip.path.with_extension("json.tmp").exists());
        backend.stop().expect("stop fake");
        fs::remove_dir_all(directory).expect("remove temporary directory");
    }

    #[test]
    fn fake_backend_rejects_unknown_source() {
        let directory = temporary_directory();
        let mut backend = FakeBackend::new();
        let config = ReplayConfig {
            source_id: "missing".to_string(),
            ..ReplayConfig::default()
        };
        let error = backend
            .start(&config, &directory)
            .expect_err("missing source");
        assert_eq!(error.code, BackendErrorCode::SourceNotFound);
        fs::remove_dir_all(directory).expect("remove temporary directory");
    }
}
