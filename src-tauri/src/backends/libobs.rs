//! Process-isolated libobs backend.
//!
//! The host owns only the control session and completed clip metadata. libobs,
//! capture surfaces, encoded packets and muxing remain inside the sidecar.

use std::fs;
use std::path::{Component, Path, PathBuf};
use std::sync::Arc;

use moonlit_libobs_protocol as protocol;
use protocol::{EncoderInfo, ProbeResult, Request, Response, SourceInfo, StartRequest};

use crate::sidecar::{ProcessSidecarLauncher, SidecarError, SidecarLauncher, SidecarTransport};
use crate::traits::{
    BackendCapabilities, BackendDescriptor, BackendError, BackendErrorCode, BackendId,
    CaptureSource, CaptureSourceKind, ClipArtifact, ClipKind, EncoderCapability, EncoderPreference,
    ReplayBackend, ReplayConfig, VideoCodec, VideoResolution,
};

const RUNTIME_RELATIVE_PATH: &str = "runtime/obs";
const RECORDER_RELATIVE_PATH: &str = "bin/64bit/moonlit-recorder.exe";

pub struct LibobsSidecarBackend {
    descriptor: BackendDescriptor,
    runtime_root: PathBuf,
    launcher: Arc<dyn SidecarLauncher>,
    session: Option<Box<dyn SidecarTransport>>,
    sources: Vec<CaptureSource>,
    output_root: Option<PathBuf>,
    buffer_seconds: u32,
}

impl LibobsSidecarBackend {
    pub fn discover_with_resource_dir(resource_dir: Option<PathBuf>) -> Self {
        let Some(resource_dir) = resource_dir else {
            return Self::unavailable(PathBuf::new(), "No hay directorio de recursos Tauri");
        };
        let runtime_root = resource_dir.join(RUNTIME_RELATIVE_PATH);
        let executable = runtime_root.join(RECORDER_RELATIVE_PATH);
        let launcher = Arc::new(ProcessSidecarLauncher::new(executable.clone()));
        let mut backend = Self::new(runtime_root, launcher);
        if !executable.is_file() {
            backend.descriptor.note = Some(format!(
                "Falta el sidecar empaquetado: {}",
                executable.display()
            ));
            return backend;
        }
        backend.refresh_probe();
        backend
    }

    #[cfg(test)]
    pub fn new_with_launcher(runtime_root: PathBuf, launcher: Arc<dyn SidecarLauncher>) -> Self {
        Self::new(runtime_root, launcher)
    }

    fn new(runtime_root: PathBuf, launcher: Arc<dyn SidecarLauncher>) -> Self {
        Self {
            descriptor: unavailable_descriptor("El runtime libobs aun no ha sido validado"),
            runtime_root,
            launcher,
            session: None,
            sources: Vec::new(),
            output_root: None,
            buffer_seconds: 0,
        }
    }

    fn unavailable(runtime_root: PathBuf, note: &str) -> Self {
        let executable = runtime_root.join(RECORDER_RELATIVE_PATH);
        let launcher = Arc::new(ProcessSidecarLauncher::new(executable));
        let mut backend = Self::new(runtime_root, launcher);
        backend.descriptor.note = Some(note.to_string());
        backend
    }

    fn refresh_probe(&mut self) {
        let result = self.probe();
        match result {
            Ok(probe) => {
                self.sources = map_sources(probe.sources.clone());
                self.descriptor = descriptor_from_probe(&probe);
            }
            Err(error) => {
                self.descriptor = unavailable_descriptor(&error.to_string());
            }
        }
    }

    fn probe(&self) -> Result<ProbeResult, BackendError> {
        let mut session = self
            .launcher
            .launch(&self.runtime_root)
            .map_err(map_sidecar_error)?;
        let result = (|| {
            match session
                .request(Request::Hello {
                    parent_pid: Some(std::process::id()),
                })
                .map_err(map_sidecar_error)?
            {
                Response::Hello {
                    protocol_version, ..
                } if protocol_version == protocol::PROTOCOL_VERSION => {}
                Response::Hello {
                    protocol_version, ..
                } => {
                    return Err(BackendError::new(
                        BackendErrorCode::BackendUnavailable,
                        format!("Version de protocolo no compatible: {protocol_version}"),
                        false,
                    ));
                }
                Response::Error(error) => return Err(map_protocol_error(error)),
                response => {
                    return Err(BackendError::new(
                        BackendErrorCode::BackendExited,
                        format!("Respuesta Hello inesperada: {response:?}"),
                        true,
                    ));
                }
            }
            match session.request(Request::Probe).map_err(map_sidecar_error)? {
                Response::Probe(probe) => Ok(probe),
                Response::Error(error) => Err(map_protocol_error(error)),
                response => Err(BackendError::new(
                    BackendErrorCode::BackendExited,
                    format!("Respuesta Probe inesperada: {response:?}"),
                    true,
                )),
            }
        })();
        let _ = session.request(Request::Shutdown);
        session.terminate();
        result
    }

    fn ensure_available(&self) -> Result<(), BackendError> {
        if self.descriptor.available {
            Ok(())
        } else {
            Err(BackendError::backend_unavailable(
                self.descriptor
                    .note
                    .clone()
                    .unwrap_or_else(|| "El runtime libobs no esta disponible".to_string()),
            ))
        }
    }

    fn response_error(response: Response) -> BackendError {
        match response {
            Response::Error(error) => map_protocol_error(error),
            other => BackendError::new(
                BackendErrorCode::BackendExited,
                format!("Respuesta inesperada del sidecar: {other:?}"),
                true,
            ),
        }
    }
}

impl ReplayBackend for LibobsSidecarBackend {
    fn descriptor(&self) -> BackendDescriptor {
        self.descriptor.clone()
    }

    fn list_sources(&self) -> Result<Vec<CaptureSource>, BackendError> {
        self.ensure_available()?;
        Ok(self.sources.clone())
    }

    fn start(&mut self, config: &ReplayConfig, output_dir: &Path) -> Result<(), BackendError> {
        self.ensure_available()?;
        if self.session.is_some() {
            return Err(BackendError::invalid_state(
                "El backend libobs ya esta capturando",
            ));
        }
        if config.codec != VideoCodec::H264 {
            return Err(BackendError::new(
                BackendErrorCode::Unsupported,
                "El primer corte libobs solo admite H.264",
                false,
            ));
        }
        if !self
            .sources
            .iter()
            .any(|source| source.id == config.source_id)
        {
            return Err(BackendError::source_not_found(&config.source_id));
        }
        if !output_dir.is_absolute() {
            return Err(BackendError::invalid_config(
                "El directorio de salida debe ser absoluto",
            ));
        }
        let output_root = output_dir.join("libobs-clips");
        fs::create_dir_all(&output_root).map_err(|error| {
            BackendError::io(format!("No se pudo crear el directorio libobs: {error}"))
        })?;

        let mut session = self
            .launcher
            .launch(&self.runtime_root)
            .map_err(map_sidecar_error)?;
        match session
            .request(Request::Hello {
                parent_pid: Some(std::process::id()),
            })
            .map_err(map_sidecar_error)?
        {
            Response::Hello {
                protocol_version, ..
            } if protocol_version == protocol::PROTOCOL_VERSION => {}
            Response::Error(error) => {
                session.terminate();
                return Err(map_protocol_error(error));
            }
            response => {
                session.terminate();
                return Err(BackendError::new(
                    BackendErrorCode::BackendExited,
                    format!("Handshake libobs inesperado: {response:?}"),
                    true,
                ));
            }
        }
        let start = StartRequest {
            source_id: config.source_id.clone(),
            buffer_seconds: config.buffer_seconds,
            width: config.resolution.as_ref().map(|value| value.width),
            height: config.resolution.as_ref().map(|value| value.height),
            fps: config.fps,
            encoder: encoder_name(&config.encoder).to_string(),
            codec: "h264".to_string(),
            format: "mp4".to_string(),
            output_dir: output_root.to_string_lossy().into_owned(),
        };
        let response = session
            .request(Request::Start(start))
            .map_err(map_sidecar_error)?;
        if !matches!(response, Response::Started { .. }) {
            session.terminate();
            return Err(Self::response_error(response));
        }
        self.output_root = Some(output_root);
        self.buffer_seconds = config.buffer_seconds;
        self.session = Some(session);
        Ok(())
    }

    fn save_replay(&mut self) -> Result<ClipArtifact, BackendError> {
        let session = self
            .session
            .as_mut()
            .ok_or_else(|| BackendError::invalid_state("Inicia el buffer antes de guardar"))?;
        let response = session
            .request(Request::SaveReplay)
            .map_err(map_sidecar_error)?;
        let Response::ClipSaved {
            relative_path,
            duration_seconds,
        } = response
        else {
            return Err(Self::response_error(response));
        };
        let root = self.output_root.as_ref().ok_or_else(|| {
            BackendError::new(BackendErrorCode::Internal, "No hay salida libobs", true)
        })?;
        let path = safe_clip_path(root, &relative_path)?;
        if !path.is_file() {
            return Err(BackendError::io("El sidecar informo un clip inexistente"));
        }
        Ok(ClipArtifact {
            path,
            duration_seconds,
            kind: ClipKind::Media,
        })
    }

    fn stop(&mut self) -> Result<(), BackendError> {
        let mut first_error = None;
        if let Some(mut session) = self.session.take() {
            match session.request(Request::Stop) {
                Ok(Response::Stopped) => {}
                Ok(response) => first_error = Some(Self::response_error(response)),
                Err(error) => first_error = Some(map_sidecar_error(error)),
            }
            session.terminate();
        }
        self.output_root = None;
        self.buffer_seconds = 0;
        first_error.map_or(Ok(()), Err)
    }

    fn poll_health(&mut self) -> Result<(), BackendError> {
        let Some(session) = self.session.as_mut() else {
            return Ok(());
        };
        match session.request(Request::Ping).map_err(map_sidecar_error)? {
            Response::Pong => Ok(()),
            response => Err(Self::response_error(response)),
        }
    }
}

impl Drop for LibobsSidecarBackend {
    fn drop(&mut self) {
        let _ = self.stop();
    }
}

fn descriptor_from_probe(probe: &ProbeResult) -> BackendDescriptor {
    let sources = map_sources(probe.sources.clone());
    let encoders = map_encoders(&probe.encoders);
    let encoder_available = encoders.iter().any(|encoder| encoder.available);
    BackendDescriptor {
        id: BackendId::LibobsSidecar,
        display_name: "MoonLit Capture".to_string(),
        available: probe.available && !sources.is_empty() && encoder_available,
        simulated: false,
        capabilities: BackendCapabilities {
            source_kinds: sources
                .iter()
                .map(|source| source.kind.clone())
                .collect::<Vec<_>>(),
            max_resolution: probe
                .max_width
                .zip(probe.max_height)
                .map(|(width, height)| VideoResolution { width, height }),
            max_fps: probe.max_fps,
            encoders,
        },
        note: probe.note.clone(),
    }
}

fn unavailable_descriptor(note: &str) -> BackendDescriptor {
    BackendDescriptor {
        id: BackendId::LibobsSidecar,
        display_name: "MoonLit Capture".to_string(),
        available: false,
        simulated: false,
        capabilities: BackendCapabilities::default(),
        note: Some(note.to_string()),
    }
}

fn map_sources(sources: Vec<SourceInfo>) -> Vec<CaptureSource> {
    sources
        .into_iter()
        .filter_map(|source| {
            let kind = match source.kind.as_str() {
                "monitor" => CaptureSourceKind::Monitor,
                "window" => CaptureSourceKind::Window,
                _ => return None,
            };
            Some(CaptureSource {
                id: source.id,
                kind,
                label: source.label,
                is_default: source.is_default,
            })
        })
        .collect()
}

fn map_encoders(encoders: &[EncoderInfo]) -> Vec<EncoderCapability> {
    encoders
        .iter()
        .filter_map(|encoder| {
            let id = match encoder.id.as_str() {
                "auto" => EncoderPreference::Auto,
                "nvenc" => EncoderPreference::Nvenc,
                "amf" => EncoderPreference::Amf,
                "quickSync" | "quick-sync" => EncoderPreference::QuickSync,
                "software" | "x264" => EncoderPreference::Software,
                _ => return None,
            };
            Some(EncoderCapability {
                id,
                available: encoder.available,
                reason: encoder.reason.clone(),
            })
        })
        .collect()
}

fn encoder_name(encoder: &EncoderPreference) -> &'static str {
    match encoder {
        EncoderPreference::Auto => "auto",
        EncoderPreference::Nvenc => "nvenc",
        EncoderPreference::Amf => "amf",
        EncoderPreference::QuickSync => "quickSync",
        EncoderPreference::Software => "software",
    }
}

fn safe_clip_path(root: &Path, relative_path: &str) -> Result<PathBuf, BackendError> {
    let relative = Path::new(relative_path);
    if relative.as_os_str().is_empty() || relative.is_absolute() {
        return Err(BackendError::io(
            "El sidecar devolvio una ruta de clip invalida",
        ));
    }
    if relative.components().any(|component| {
        matches!(
            component,
            Component::ParentDir | Component::RootDir | Component::Prefix(_)
        )
    }) {
        return Err(BackendError::io(
            "El sidecar devolvio una ruta fuera del directorio",
        ));
    }
    Ok(root.join(relative))
}

fn map_sidecar_error(error: SidecarError) -> BackendError {
    match error {
        SidecarError::Timeout => BackendError::new(
            BackendErrorCode::Timeout,
            "El sidecar no respondio a tiempo",
            true,
        ),
        SidecarError::Exited => BackendError::new(
            BackendErrorCode::BackendExited,
            "El sidecar termino inesperadamente",
            true,
        ),
        SidecarError::Io(message) => BackendError::backend_unavailable(message),
        SidecarError::Protocol(message) | SidecarError::InvalidResponse(message) => {
            BackendError::new(BackendErrorCode::BackendExited, message, true)
        }
    }
}

fn map_protocol_error(error: protocol::SidecarError) -> BackendError {
    let code = match error.code.as_str() {
        "backendUnavailable" => BackendErrorCode::BackendUnavailable,
        "sourceNotFound" => BackendErrorCode::SourceNotFound,
        "sourceEnded" => BackendErrorCode::SourceEnded,
        "encoderUnavailable" => BackendErrorCode::EncoderUnavailable,
        "io" => BackendErrorCode::Io,
        "timeout" => BackendErrorCode::Timeout,
        "unsupported" => BackendErrorCode::Unsupported,
        "backendExited" => BackendErrorCode::BackendExited,
        _ => BackendErrorCode::Internal,
    };
    BackendError::new(code, error.message, error.retryable)
}

#[cfg(test)]
mod tests {
    use std::collections::VecDeque;
    use std::fs;
    use std::path::Path;
    use std::sync::{Arc, Mutex};

    use moonlit_libobs_protocol as protocol;

    use super::{descriptor_from_probe, safe_clip_path, LibobsSidecarBackend};
    use crate::sidecar::{SidecarError, SidecarLauncher, SidecarTransport};
    use crate::traits::{BackendId, EncoderPreference, ReplayBackend, ReplayConfig};

    struct MockTransport {
        responses: VecDeque<Result<protocol::Response, SidecarError>>,
    }

    impl SidecarTransport for MockTransport {
        fn request(
            &mut self,
            _request: protocol::Request,
        ) -> Result<protocol::Response, SidecarError> {
            self.responses
                .pop_front()
                .unwrap_or(Err(SidecarError::Exited))
        }

        fn terminate(&mut self) {}
    }

    struct MockLauncher {
        responses: Mutex<VecDeque<Vec<Result<protocol::Response, SidecarError>>>>,
    }

    impl SidecarLauncher for MockLauncher {
        fn launch(&self, _runtime_root: &Path) -> Result<Box<dyn SidecarTransport>, SidecarError> {
            let responses = self
                .responses
                .lock()
                .expect("mock responses")
                .pop_front()
                .ok_or(SidecarError::Exited)?;
            Ok(Box::new(MockTransport {
                responses: responses.into_iter().collect(),
            }))
        }
    }

    #[test]
    fn clip_paths_cannot_escape_the_output_root() {
        let root = Path::new("C:/MoonLit/clips");
        assert!(safe_clip_path(root, "clip.mp4").is_ok());
        assert!(safe_clip_path(root, "../clip.mp4").is_err());
        assert!(safe_clip_path(root, "C:/other/clip.mp4").is_err());
    }

    #[test]
    fn sidecar_backend_saves_only_a_validated_relative_clip() {
        let directory = std::env::temp_dir().join(format!("moonlit-libobs-{}", std::process::id()));
        let output_root = directory.join("libobs-clips");
        fs::create_dir_all(&output_root).expect("output root");
        fs::write(output_root.join("clip.mp4"), b"test clip").expect("clip");
        let launcher = Arc::new(MockLauncher {
            responses: Mutex::new(VecDeque::from([vec![
                Ok(protocol::Response::Hello {
                    sidecar_version: "test".to_string(),
                    protocol_version: protocol::PROTOCOL_VERSION,
                }),
                Ok(protocol::Response::Started {
                    encoder: "software".to_string(),
                    format: "mp4".to_string(),
                }),
                Ok(protocol::Response::ClipSaved {
                    relative_path: "clip.mp4".to_string(),
                    duration_seconds: 30,
                }),
                Ok(protocol::Response::Stopped),
            ]])),
        });
        let mut backend = LibobsSidecarBackend::new_with_launcher(directory.clone(), launcher);
        backend.descriptor = descriptor_from_probe(&protocol::ProbeResult {
            available: true,
            sources: vec![protocol::SourceInfo {
                id: "monitor-1".to_string(),
                kind: "monitor".to_string(),
                label: "Monitor 1".to_string(),
                is_default: true,
            }],
            encoders: vec![protocol::EncoderInfo {
                id: "software".to_string(),
                available: true,
                reason: None,
            }],
            max_width: Some(1920),
            max_height: Some(1080),
            max_fps: Some(60),
            note: None,
        });
        backend.sources = super::map_sources(vec![protocol::SourceInfo {
            id: "monitor-1".to_string(),
            kind: "monitor".to_string(),
            label: "Monitor 1".to_string(),
            is_default: true,
        }]);
        assert_eq!(backend.descriptor().id, BackendId::LibobsSidecar);
        backend
            .start(
                &ReplayConfig {
                    source_id: "monitor-1".to_string(),
                    encoder: EncoderPreference::Software,
                    ..ReplayConfig::default()
                },
                &directory,
            )
            .expect("start");
        let clip = backend.save_replay().expect("save");
        assert_eq!(clip.path, output_root.join("clip.mp4"));
        backend.stop().expect("stop");
        fs::remove_dir_all(directory).expect("cleanup");
    }
}
