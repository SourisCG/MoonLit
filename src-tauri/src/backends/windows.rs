//! Windows.Graphics.Capture and NVENC backend.
//!
//! The unsafe WinRT, D3D11 and NVENC boundary lives in the
//! `moonlit-windows-native` crate. This module owns the portable replay buffer
//! and maps native resources into the `ReplayBackend` contract.

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::mpsc::Receiver;
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use moonlit_windows_native as native;

use crate::replay::{EncodedPacket, ReplayBuffer, ReplayError};
use crate::traits::{
    BackendCapabilities, BackendDescriptor, BackendError, BackendErrorCode, BackendId,
    CaptureSource, CaptureSourceKind, ClipArtifact, ClipKind, EncoderCapability, EncoderPreference,
    ReplayBackend, ReplayConfig, VideoCodec, VideoResolution,
};

pub struct WindowsNativeBackend {
    descriptor: BackendDescriptor,
    capture: Option<native::CaptureHandle>,
    replay: Option<Arc<Mutex<ReplayBuffer>>>,
    collector: Option<JoinHandle<()>>,
    collector_error: Arc<Mutex<Option<BackendError>>>,
    output_dir: Option<PathBuf>,
    buffer_seconds: u32,
}

impl WindowsNativeBackend {
    pub fn new() -> Self {
        Self {
            descriptor: native_descriptor(),
            capture: None,
            replay: None,
            collector: None,
            collector_error: Arc::new(Mutex::new(None)),
            output_dir: None,
            buffer_seconds: 0,
        }
    }

    fn current_error(&self) -> Option<BackendError> {
        self.collector_error
            .lock()
            .ok()
            .and_then(|error| error.clone())
    }

    fn set_error(error_ref: &Arc<Mutex<Option<BackendError>>>, error: BackendError) {
        if let Ok(mut current) = error_ref.lock() {
            *current = Some(error);
        }
    }
}

impl ReplayBackend for WindowsNativeBackend {
    fn descriptor(&self) -> BackendDescriptor {
        self.descriptor.clone()
    }

    fn list_sources(&self) -> Result<Vec<CaptureSource>, BackendError> {
        native::list_sources()
            .map(|sources| {
                sources
                    .into_iter()
                    .map(|source| CaptureSource {
                        id: source.id,
                        kind: match source.kind {
                            native::SourceKind::Monitor => CaptureSourceKind::Monitor,
                            native::SourceKind::Window => CaptureSourceKind::Window,
                        },
                        label: source.label,
                        is_default: source.is_default,
                    })
                    .collect()
            })
            .map_err(map_native_error)
    }

    fn start(&mut self, config: &ReplayConfig, output_dir: &Path) -> Result<(), BackendError> {
        if self.capture.is_some() {
            return Err(BackendError::invalid_state(
                "El backend Windows ya esta capturando",
            ));
        }
        if !self.descriptor.available {
            return Err(BackendError::backend_unavailable(
                self.descriptor
                    .note
                    .clone()
                    .unwrap_or_else(|| "El backend Windows no esta disponible".to_string()),
            ));
        }
        if config.codec != VideoCodec::H264 {
            return Err(BackendError::new(
                BackendErrorCode::Unsupported,
                "El spike nativo solo admite H.264",
                false,
            ));
        }
        if !matches!(
            config.encoder,
            EncoderPreference::Auto | EncoderPreference::Nvenc
        ) {
            return Err(BackendError::new(
                BackendErrorCode::EncoderUnavailable,
                "El backend Windows solo admite NVENC en este spike",
                false,
            ));
        }

        let source = native::list_sources()
            .map_err(map_native_error)?
            .into_iter()
            .find(|source| source.id == config.source_id)
            .ok_or_else(|| BackendError::source_not_found(&config.source_id))?;
        if source.kind != native::SourceKind::Monitor {
            return Err(BackendError::new(
                BackendErrorCode::Unsupported,
                "El spike nativo comienza con captura de monitores",
                false,
            ));
        }

        let resolution = config.resolution.clone().unwrap_or(VideoResolution {
            width: source.width,
            height: source.height,
        });
        let fps = config.fps.unwrap_or(60);
        if fps > 60 {
            return Err(BackendError::invalid_config(
                "El spike nativo admite hasta 60 FPS",
            ));
        }
        let replay = ReplayBuffer::new(Duration::from_secs(config.buffer_seconds as u64))
            .map_err(map_replay_error)?;
        let (capture, packets) = native::start_capture(native::NativeConfig {
            source_id: config.source_id.clone(),
            width: resolution.width,
            height: resolution.height,
            fps,
        })
        .map_err(map_native_error)?;

        let replay = Arc::new(Mutex::new(replay));
        let error_ref = Arc::new(Mutex::new(None));
        let collector_replay = Arc::clone(&replay);
        let collector_error = Arc::clone(&error_ref);
        let collector = match thread::Builder::new()
            .name("moonlit-replay-collector".to_string())
            .spawn(move || collect_packets(packets, collector_replay, collector_error))
        {
            Ok(collector) => collector,
            Err(error) => {
                drop(capture);
                return Err(BackendError::io(format!(
                    "No se pudo iniciar el collector nativo: {error}"
                )));
            }
        };

        self.capture = Some(capture);
        self.replay = Some(replay);
        self.collector = Some(collector);
        self.collector_error = error_ref;
        self.output_dir = Some(output_dir.to_path_buf());
        self.buffer_seconds = config.buffer_seconds;
        Ok(())
    }

    fn save_replay(&mut self) -> Result<ClipArtifact, BackendError> {
        if let Some(error) = self.current_error() {
            return Err(error);
        }
        let replay = self
            .replay
            .as_ref()
            .ok_or_else(|| BackendError::invalid_state("Inicia el buffer antes de guardar"))?;
        let clip = replay
            .lock()
            .map_err(|_| {
                BackendError::new(
                    BackendErrorCode::Internal,
                    "El replay buffer esta bloqueado",
                    true,
                )
            })?
            .save_last(Duration::from_secs(self.buffer_seconds as u64))
            .map_err(map_replay_error)?;
        let output_dir = self
            .output_dir
            .as_ref()
            .ok_or_else(|| {
                BackendError::new(
                    BackendErrorCode::Internal,
                    "No hay directorio de salida",
                    true,
                )
            })?
            .join("native-clips");
        fs::create_dir_all(&output_dir).map_err(|error| {
            BackendError::io(format!("No se pudo crear el directorio nativo: {error}"))
        })?;

        let contents: Vec<u8> = clip
            .packets
            .iter()
            .flat_map(|packet| packet.data.iter().copied())
            .collect();
        let id = unique_id("native");
        let path = output_dir.join(format!("{id}.h264"));
        write_atomic(&path, &contents)?;
        let duration_seconds = clip
            .duration_ms()
            .saturating_add(999)
            .checked_div(1000)
            .unwrap_or(0)
            .min(u32::MAX as u64) as u32;

        Ok(ClipArtifact {
            path,
            duration_seconds,
            kind: ClipKind::Media,
        })
    }

    fn stop(&mut self) -> Result<(), BackendError> {
        let mut first_error = None;
        if let Some(capture) = self.capture.take() {
            if let Err(error) = capture.stop() {
                first_error = Some(map_native_error(error));
            }
        }
        if let Some(collector) = self.collector.take() {
            if collector.join().is_err() && first_error.is_none() {
                first_error = Some(BackendError::new(
                    BackendErrorCode::BackendExited,
                    "El collector nativo termino abruptamente",
                    true,
                ));
            }
        }
        self.replay = None;
        self.output_dir = None;
        self.buffer_seconds = 0;
        first_error.map_or(Ok(()), Err)
    }

    fn poll_health(&mut self) -> Result<(), BackendError> {
        self.current_error().map_or(Ok(()), Err)
    }
}

impl Drop for WindowsNativeBackend {
    fn drop(&mut self) {
        let _ = self.stop();
    }
}

fn native_descriptor() -> BackendDescriptor {
    let capabilities = native::capabilities();
    let sources = native::list_sources().unwrap_or_default();
    let available = capabilities.wgc_supported && capabilities.nvenc_h264 && !sources.is_empty();
    let source_kinds = if sources.is_empty() {
        Vec::new()
    } else {
        vec![CaptureSourceKind::Monitor]
    };
    let note = if available {
        Some("Captura monitor-first con WGC, D3D11 y NVENC H.264.".to_string())
    } else {
        Some(capabilities.note.unwrap_or_else(|| {
            "WGC y NVENC H.264 no estan disponibles en este equipo.".to_string()
        }))
    };

    BackendDescriptor {
        id: BackendId::WindowsNative,
        display_name: "Windows Capture".to_string(),
        available,
        simulated: false,
        capabilities: BackendCapabilities {
            source_kinds,
            max_resolution: capabilities
                .max_width
                .zip(capabilities.max_height)
                .map(|(width, height)| VideoResolution { width, height }),
            max_fps: capabilities.max_fps,
            encoders: vec![EncoderCapability {
                id: EncoderPreference::Nvenc,
                available: capabilities.nvenc_h264,
                reason: if capabilities.nvenc_h264 {
                    None
                } else {
                    Some("NVENC H.264 no esta disponible".to_string())
                },
            }],
        },
        note,
    }
}

fn collect_packets(
    packets: Receiver<Result<native::EncodedPacket, native::NativeError>>,
    replay: Arc<Mutex<ReplayBuffer>>,
    error_ref: Arc<Mutex<Option<BackendError>>>,
) {
    while let Ok(result) = packets.recv() {
        match result {
            Ok(packet) => {
                let packet = EncodedPacket::new(
                    packet.pts_100ns,
                    packet.duration_100ns,
                    packet.is_keyframe,
                    packet.data,
                );
                let result = replay
                    .lock()
                    .map_err(|_| {
                        BackendError::new(
                            BackendErrorCode::Internal,
                            "El replay buffer esta bloqueado",
                            true,
                        )
                    })
                    .and_then(|mut replay| replay.push(packet).map_err(map_replay_error));
                if let Err(error) = result {
                    WindowsNativeBackend::set_error(&error_ref, error);
                    break;
                }
            }
            Err(error) => {
                WindowsNativeBackend::set_error(&error_ref, map_native_error(error));
                break;
            }
        }
    }
}

fn write_atomic(path: &Path, contents: &[u8]) -> Result<(), BackendError> {
    let temporary_path = path.with_extension("h264.tmp");
    fs::write(&temporary_path, contents).map_err(|error| {
        BackendError::io(format!("No se pudo escribir el clip temporal: {error}"))
    })?;
    if let Err(error) = fs::rename(&temporary_path, path) {
        let _ = fs::remove_file(&temporary_path);
        return Err(BackendError::io(format!(
            "No se pudo finalizar el clip nativo: {error}"
        )));
    }
    Ok(())
}

fn map_native_error(error: native::NativeError) -> BackendError {
    match error {
        native::NativeError::PermissionDenied => BackendError::new(
            BackendErrorCode::PermissionDenied,
            "Windows rechazo el permiso de captura",
            true,
        ),
        native::NativeError::SourceNotFound(source) => BackendError::source_not_found(&source),
        native::NativeError::SourceEnded => BackendError::new(
            BackendErrorCode::SourceEnded,
            "La fuente de captura termino",
            true,
        ),
        native::NativeError::EncoderUnavailable(message) => {
            BackendError::new(BackendErrorCode::EncoderUnavailable, message, true)
        }
        native::NativeError::DriverUnavailable(message) => {
            BackendError::new(BackendErrorCode::EncoderUnavailable, message, true)
        }
        native::NativeError::InvalidConfig(message) => BackendError::invalid_config(message),
        native::NativeError::Unsupported(message) => {
            BackendError::new(BackendErrorCode::Unsupported, message, false)
        }
        native::NativeError::Io(message) => BackendError::io(message),
        native::NativeError::Windows { operation, code } => BackendError::new(
            BackendErrorCode::Internal,
            format!("{operation} fallo con HRESULT 0x{code:08x}"),
            true,
        ),
        native::NativeError::ChannelClosed => BackendError::new(
            BackendErrorCode::BackendExited,
            "El worker nativo termino",
            true,
        ),
        native::NativeError::WorkerPanicked => BackendError::new(
            BackendErrorCode::BackendExited,
            "El worker nativo termino abruptamente",
            true,
        ),
    }
}

fn map_replay_error(error: ReplayError) -> BackendError {
    let message = error.to_string();
    match error {
        ReplayError::InvalidWindow | ReplayError::InvalidPacket(_) => {
            BackendError::invalid_config(message)
        }
        ReplayError::InvalidBufferLimit => {
            BackendError::new(BackendErrorCode::Internal, message, false)
        }
        ReplayError::OutOfOrderPacket => {
            BackendError::new(BackendErrorCode::Internal, message, true)
        }
        ReplayError::NoDecodableKeyframe => BackendError::new(
            BackendErrorCode::InvalidState,
            "Todavia no hay un keyframe completo para guardar",
            true,
        ),
    }
}

fn now_millis() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
}

fn unique_id(prefix: &str) -> String {
    format!("{prefix}-{}", now_millis())
}
