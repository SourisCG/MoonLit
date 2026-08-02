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
    AudioCapabilities, AudioConfig, BackendCapabilities, BackendDescriptor, BackendError,
    BackendErrorCode, BackendId, CaptureSource, CaptureSourceKind, ClipArtifact, ClipKind,
    ContainerFormat, EffectiveReplaySettings, EncoderCapability, EncoderPreference, QualityPreset,
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
    effective_start: Option<EffectiveStartMetadata>,
    can_save: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct EffectiveStartMetadata {
    encoder: String,
    codec: VideoCodec,
    format: ContainerFormat,
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
            effective_start: None,
            can_save: false,
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
        self.effective_start = None;

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
            codec: codec_name(&config.codec).to_string(),
            format: format_name(&config.format).to_string(),
            quality: quality_name(&config.quality).to_string(),
            bitrate_kbps: config.bitrate_kbps,
            audio: audio_start(&config.audio),
            output_dir: output_root.to_string_lossy().into_owned(),
        };
        let response = session
            .request(Request::Start(start))
            .map_err(map_sidecar_error)?;
        let Response::Started {
            encoder,
            codec,
            format,
        } = response
        else {
            session.terminate();
            return Err(Self::response_error(response));
        };
        let effective_start = match effective_start_metadata(&encoder, &codec, &format) {
            Ok(metadata) => metadata,
            Err(error) => {
                session.terminate();
                return Err(error);
            }
        };
        if !requested_encoder_matches(&config.encoder, &effective_start.encoder)
            || effective_start.codec != config.codec
            || effective_start.format != config.format
        {
            session.terminate();
            return Err(BackendError::new(
                BackendErrorCode::Unsupported,
                format!(
                    "El sidecar devolvio una combinacion efectiva distinta: {} / {} / {}",
                    effective_start.encoder,
                    codec_name(&effective_start.codec),
                    format_name(&effective_start.format)
                ),
                false,
            ));
        }
        self.effective_start = Some(effective_start);
        self.output_root = Some(output_root);
        self.buffer_seconds = config.buffer_seconds;
        self.session = Some(session);
        self.can_save = false;
        Ok(())
    }

    fn effective_settings(&self) -> Option<EffectiveReplaySettings> {
        self.effective_start
            .as_ref()
            .map(|settings| EffectiveReplaySettings {
                encoder: settings.encoder.clone(),
                codec: settings.codec.clone(),
                format: settings.format.clone(),
            })
    }

    fn can_save(&self) -> bool {
        self.can_save
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
            codec,
            format,
            width,
            height,
            fps,
            has_audio,
        } = response
        else {
            return Err(Self::response_error(response));
        };
        let clip_codec = parse_codec(&codec)?;
        let clip_format = parse_format(&format)?;
        if let Some(started) = &self.effective_start {
            if started.codec != clip_codec {
                return Err(BackendError::new(
                    BackendErrorCode::Unsupported,
                    format!(
                        "El sidecar cambio el codec efectivo de {} a {}",
                        codec_name(&started.codec),
                        codec
                    ),
                    false,
                ));
            }
            if started.format != clip_format {
                return Err(BackendError::new(
                    BackendErrorCode::Unsupported,
                    format!(
                        "El sidecar cambio el contenedor efectivo de {} a {} (encoder: {})",
                        format_name(&started.format),
                        format_name(&clip_format),
                        started.encoder
                    ),
                    false,
                ));
            }
        }
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
            codec: clip_codec,
            format: clip_format,
            width,
            height,
            fps,
            has_audio,
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
        self.effective_start = None;
        self.can_save = false;
        first_error.map_or(Ok(()), Err)
    }

    fn poll_health(&mut self) -> Result<(), BackendError> {
        let Some(session) = self.session.as_mut() else {
            return Ok(());
        };
        for event in session.drain_events() {
            match event {
                protocol::Event::Fatal(error) => return Err(map_protocol_error(error)),
                protocol::Event::SourceEnded { source_id } => {
                    return Err(BackendError::new(
                        BackendErrorCode::SourceEnded,
                        format!("La fuente termino: {source_id}"),
                        true,
                    ));
                }
                protocol::Event::AudioDeviceChanged { .. } | protocol::Event::Heartbeat => {}
                protocol::Event::BufferStatus { can_save, .. } => {
                    self.can_save = can_save;
                }
            }
        }
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
    let codecs = map_codecs(&probe.codecs);
    let formats = map_formats(&probe.formats);
    BackendDescriptor {
        id: BackendId::LibobsSidecar,
        display_name: "MoonLit Capture".to_string(),
        available: probe.available
            && !sources.is_empty()
            && encoder_available
            && codecs.contains(&VideoCodec::H264)
            && codecs.contains(&VideoCodec::Hevc)
            && formats.contains(&ContainerFormat::Mp4)
            && formats.contains(&ContainerFormat::Mkv),
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
            codecs,
            formats,
            audio: AudioCapabilities {
                available: probe.audio.available,
                system_audio: probe.audio.system_audio,
                microphone: probe.audio.microphone,
                application_audio: probe.audio.application_audio,
                note: probe.audio.note.clone(),
            },
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
                width: None,
                height: None,
                process_name: None,
                available: true,
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

fn map_codecs(codecs: &[String]) -> Vec<VideoCodec> {
    codecs
        .iter()
        .filter_map(|codec| match codec.as_str() {
            "h264" | "avc" => Some(VideoCodec::H264),
            "hevc" | "h265" => Some(VideoCodec::Hevc),
            _ => None,
        })
        .collect()
}

fn map_formats(formats: &[String]) -> Vec<ContainerFormat> {
    formats
        .iter()
        .filter_map(|format| match format.as_str() {
            "mp4" => Some(ContainerFormat::Mp4),
            "mkv" => Some(ContainerFormat::Mkv),
            _ => None,
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

fn codec_name(codec: &VideoCodec) -> &'static str {
    match codec {
        VideoCodec::H264 => "h264",
        VideoCodec::Hevc => "hevc",
    }
}

fn format_name(format: &ContainerFormat) -> &'static str {
    match format {
        ContainerFormat::Mp4 => "mp4",
        ContainerFormat::Mkv => "mkv",
    }
}

fn quality_name(quality: &QualityPreset) -> &'static str {
    match quality {
        QualityPreset::Low => "low",
        QualityPreset::Medium => "medium",
        QualityPreset::High => "high",
        QualityPreset::Ultra => "ultra",
        QualityPreset::Custom => "custom",
    }
}

fn audio_start(audio: &AudioConfig) -> protocol::AudioStart {
    protocol::AudioStart {
        system_enabled: audio.system_enabled,
        microphone_enabled: audio.microphone_enabled,
        system_device_id: audio.system_device_id.clone(),
        microphone_device_id: audio.microphone_device_id.clone(),
        system_gain_milli: (audio.system_gain * 1000.0).round() as u32,
        microphone_gain_milli: (audio.microphone_gain * 1000.0).round() as u32,
        system_muted: audio.system_muted,
        microphone_muted: audio.microphone_muted,
        bitrate_kbps: audio.bitrate_kbps,
    }
}

fn effective_start_metadata(
    encoder: &str,
    codec: &str,
    format: &str,
) -> Result<EffectiveStartMetadata, BackendError> {
    let encoder = parse_effective_encoder(encoder)?;
    let codec = parse_codec(codec)?;
    let format = parse_format(format)?;
    Ok(EffectiveStartMetadata {
        encoder,
        codec,
        format,
    })
}

fn parse_effective_encoder(encoder: &str) -> Result<String, BackendError> {
    let value = encoder.trim();
    let known = [
        "auto",
        "nvenc",
        "amf",
        "quicksync",
        "quick-sync",
        "software",
        "x264",
        "x265",
    ];
    if value.is_empty()
        || !known
            .iter()
            .any(|candidate| value.eq_ignore_ascii_case(candidate))
    {
        return Err(unknown_metadata("encoder", encoder));
    }
    Ok(value.to_string())
}

fn requested_encoder_matches(requested: &EncoderPreference, effective: &str) -> bool {
    let effective = effective.to_ascii_lowercase();
    match requested {
        EncoderPreference::Auto => true,
        EncoderPreference::Nvenc => effective == "nvenc",
        EncoderPreference::Amf => effective == "amf",
        EncoderPreference::QuickSync => effective == "quicksync" || effective == "quick-sync",
        EncoderPreference::Software => matches!(effective.as_str(), "software" | "x264" | "x265"),
    }
}

fn parse_codec(codec: &str) -> Result<VideoCodec, BackendError> {
    if codec.eq_ignore_ascii_case("h264") || codec.eq_ignore_ascii_case("avc") {
        Ok(VideoCodec::H264)
    } else if codec.eq_ignore_ascii_case("hevc") || codec.eq_ignore_ascii_case("h265") {
        Ok(VideoCodec::Hevc)
    } else {
        Err(unknown_metadata("codec", codec))
    }
}

fn parse_format(format: &str) -> Result<ContainerFormat, BackendError> {
    if format.eq_ignore_ascii_case("mp4") {
        Ok(ContainerFormat::Mp4)
    } else if format.eq_ignore_ascii_case("mkv") {
        Ok(ContainerFormat::Mkv)
    } else {
        Err(unknown_metadata("container", format))
    }
}

fn unknown_metadata(kind: &str, value: &str) -> BackendError {
    BackendError::new(
        BackendErrorCode::Unsupported,
        format!("El sidecar devolvio {kind} desconocido: '{value}'"),
        false,
    )
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

    use super::{
        descriptor_from_probe, effective_start_metadata, parse_codec, parse_format, safe_clip_path,
        LibobsSidecarBackend,
    };
    use crate::sidecar::{SidecarError, SidecarLauncher, SidecarTransport};
    use crate::traits::{
        BackendErrorCode, BackendId, ContainerFormat, EncoderPreference, ReplayBackend,
        ReplayConfig, VideoCodec,
    };

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
    fn unknown_clip_metadata_is_rejected_instead_of_defaulting() {
        assert!(matches!(
            parse_codec("vp9"),
            Err(error) if error.code == BackendErrorCode::Unsupported
        ));
        assert!(matches!(
            parse_format("webm"),
            Err(error) if error.code == BackendErrorCode::Unsupported
        ));
        assert_eq!(parse_codec("h264").expect("h264"), VideoCodec::H264);
        assert_eq!(parse_format("mkv").expect("mkv"), ContainerFormat::Mkv);
    }

    #[test]
    fn started_metadata_is_preserved_and_validated() {
        let metadata =
            effective_start_metadata("nvenc", "hevc", "MKV").expect("effective metadata");
        assert_eq!(metadata.encoder, "nvenc");
        assert_eq!(metadata.codec, VideoCodec::Hevc);
        assert_eq!(metadata.format, ContainerFormat::Mkv);
        assert!(effective_start_metadata("unknown", "h264", "mp4").is_err());
        assert!(effective_start_metadata("software", "h264", "webm").is_err());
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
                    codec: "h264".to_string(),
                    format: "mp4".to_string(),
                }),
                Ok(protocol::Response::ClipSaved {
                    relative_path: "clip.mp4".to_string(),
                    duration_seconds: 30,
                    codec: "h264".to_string(),
                    format: "mp4".to_string(),
                    width: Some(1920),
                    height: Some(1080),
                    fps: Some(60),
                    has_audio: false,
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
            codecs: vec!["h264".to_string(), "hevc".to_string()],
            formats: vec!["mp4".to_string(), "mkv".to_string()],
            audio: protocol::AudioInfo::default(),
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
        assert_eq!(
            backend.effective_start,
            Some(super::EffectiveStartMetadata {
                encoder: "software".to_string(),
                codec: VideoCodec::H264,
                format: ContainerFormat::Mp4,
            })
        );
        let clip = backend.save_replay().expect("save");
        assert_eq!(clip.path, output_root.join("clip.mp4"));
        backend.stop().expect("stop");
        assert!(backend.effective_start.is_none());
        fs::remove_dir_all(directory).expect("cleanup");
    }
}
