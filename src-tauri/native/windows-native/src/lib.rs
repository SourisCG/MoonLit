#![allow(non_snake_case)]

mod capture;
mod d3d11;
mod encoder;
mod error;
mod sources;

pub use error::NativeError;

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum SourceKind {
    Monitor,
    Window,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NativeSource {
    pub id: String,
    pub kind: SourceKind,
    pub label: String,
    pub is_default: bool,
    pub width: u32,
    pub height: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NativeCapabilities {
    pub wgc_supported: bool,
    pub nvenc_h264: bool,
    pub max_width: Option<u32>,
    pub max_height: Option<u32>,
    pub max_fps: Option<u32>,
    pub note: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NativeConfig {
    pub source_id: String,
    pub width: u32,
    pub height: u32,
    pub fps: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct EncodedPacket {
    pub pts_100ns: u64,
    pub duration_100ns: u64,
    pub is_keyframe: bool,
    pub codec_config: Option<Vec<u8>>,
    pub data: Vec<u8>,
}

pub struct CaptureHandle {
    stop: Option<Box<dyn FnOnce() -> Result<(), NativeError> + Send + 'static>>,
}

impl CaptureHandle {
    pub(crate) fn new(stop: impl FnOnce() -> Result<(), NativeError> + Send + 'static) -> Self {
        Self {
            stop: Some(Box::new(stop)),
        }
    }

    pub fn stop(mut self) -> Result<(), NativeError> {
        self.stop.take().map(|stop| stop()).unwrap_or(Ok(()))
    }
}

impl Drop for CaptureHandle {
    fn drop(&mut self) {
        if let Some(stop) = self.stop.take() {
            let _ = stop();
        }
    }
}

pub fn capabilities() -> NativeCapabilities {
    let wgc_supported =
        windows::Graphics::Capture::GraphicsCaptureSession::IsSupported().unwrap_or(false);
    let monitors = sources::enumerate_monitors().unwrap_or_default();
    let (max_width, max_height) = monitors
        .first()
        .map(|(_, target)| (Some(target.width), Some(target.height)))
        .unwrap_or((None, None));
    let mut note = Vec::new();
    if !wgc_supported {
        note.push("Windows.Graphics.Capture is not supported".to_string());
    }
    if monitors.is_empty() {
        note.push("No monitor sources were enumerated".to_string());
    }

    let nvenc_h264 = match d3d11::D3d11Context::create() {
        Ok(context) => match encoder::NvencEncoder::probe(&context) {
            Ok(()) => true,
            Err(error) => {
                note.push(error.to_string());
                false
            }
        },
        Err(error) => {
            note.push(error.to_string());
            false
        }
    };

    NativeCapabilities {
        wgc_supported,
        nvenc_h264,
        max_width,
        max_height,
        max_fps: (wgc_supported && !monitors.is_empty()).then_some(60),
        note: (!note.is_empty()).then(|| note.join("; ")),
    }
}

pub fn list_sources() -> Result<Vec<NativeSource>, NativeError> {
    Ok(sources::enumerate_monitors()?
        .into_iter()
        .map(|(source, _)| source)
        .collect())
}

pub fn start_capture(
    config: NativeConfig,
) -> Result<
    (
        CaptureHandle,
        std::sync::mpsc::Receiver<Result<EncodedPacket, NativeError>>,
    ),
    NativeError,
> {
    capture::start(config)
}
