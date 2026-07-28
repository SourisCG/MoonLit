//! Windows native backend boundary.
//!
//! WGC, D3D11 and NVENC will be implemented behind this contract. The
//! unavailable descriptor is intentional until the native crate is added.

use std::path::Path;

use crate::traits::{
    BackendCapabilities, BackendDescriptor, BackendError, BackendId, CaptureSource, ClipArtifact,
    EncoderCapability, EncoderPreference, ReplayBackend, ReplayConfig, VideoResolution,
};

pub struct WindowsNativeBackend;

impl WindowsNativeBackend {
    pub fn new() -> Self {
        Self
    }

    fn descriptor_value() -> BackendDescriptor {
        BackendDescriptor {
            id: BackendId::WindowsNative,
            display_name: "Windows Capture".to_string(),
            available: false,
            simulated: false,
            capabilities: BackendCapabilities {
                source_kinds: Vec::new(),
                max_resolution: Some(VideoResolution {
                    width: 3840,
                    height: 2160,
                }),
                max_fps: Some(60),
                encoders: vec![EncoderCapability {
                    id: EncoderPreference::Nvenc,
                    available: false,
                    reason: Some("WGC y NVENC aun no estan implementados".to_string()),
                }],
            },
            note: Some(
                "El backend nativo Windows se habilitara despues del spike WGC + NVENC."
                    .to_string(),
            ),
        }
    }
}

impl ReplayBackend for WindowsNativeBackend {
    fn descriptor(&self) -> BackendDescriptor {
        Self::descriptor_value()
    }

    fn list_sources(&self) -> Result<Vec<CaptureSource>, BackendError> {
        Err(BackendError::backend_unavailable(
            "Windows.Graphics.Capture aun no esta implementado",
        ))
    }

    fn start(&mut self, _config: &ReplayConfig, _output_dir: &Path) -> Result<(), BackendError> {
        Err(BackendError::backend_unavailable(
            "Windows.Graphics.Capture y NVENC aun no estan implementados",
        ))
    }

    fn save_replay(&mut self) -> Result<ClipArtifact, BackendError> {
        Err(BackendError::backend_unavailable(
            "El backend nativo Windows aun no esta disponible",
        ))
    }

    fn stop(&mut self) -> Result<(), BackendError> {
        Ok(())
    }
}
