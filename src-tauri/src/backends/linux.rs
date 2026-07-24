//! Linux backend implementation (stub)
//!
//! This module will contain the real Linux implementation using:
//! - PipeWire + X11/Wayland for screen capture
//! - PipeWire for audio capture
//! - VAAPI/NVENC (Linux) for GPU encoding
//!
//! Currently a stub that returns errors. Implementation will be completed on Linux machine.

use crate::traits::*;
use std::path::PathBuf;

/// Linux capture backend using PipeWire + X11/Wayland
pub struct LinuxCaptureBackend {
    // Will hold Linux-specific state
    // e.g., PipeWire context, X11/Wayland display, etc.
}

impl LinuxCaptureBackend {
    pub fn new() -> Self {
        Self {
            // Initialize Linux-specific resources
        }
    }
}

impl CaptureService for LinuxCaptureBackend {
    fn start_replay(&mut self, _config: CaptureConfig) -> Result<CaptureSession, CaptureError> {
        Err(CaptureError::CaptureFailed(
            "Linux backend not yet implemented. This is a future feature.".to_string(),
        ))
    }

    fn save_clip(&mut self, _session: &mut CaptureSession) -> Result<PathBuf, CaptureError> {
        Err(CaptureError::CaptureFailed(
            "Linux backend not yet implemented. This is a future feature.".to_string(),
        ))
    }

    fn stop(&mut self, _session: &mut CaptureSession) -> Result<(), CaptureError> {
        Ok(())
    }

    fn get_sources(&self) -> Result<Vec<CaptureSource>, CaptureError> {
        Err(CaptureError::CaptureFailed(
            "Linux backend not yet implemented. This is a future feature.".to_string(),
        ))
    }

    fn is_capturing(&self) -> bool {
        false
    }

    fn backend_name(&self) -> &str {
        "PipeWire + X11/Wayland"
    }

    fn capabilities(&self) -> BackendCapabilities {
        BackendCapabilities {
            supports_window_capture: true,
            supports_monitor_capture: true,
            supports_region_capture: true,
            max_resolution: Some((7680, 4320)), // 8K
            max_fps: Some(240),
            supported_codecs: vec![VideoCodec::H264, VideoCodec::H265],
        }
    }
}

/// Linux audio mixer using PipeWire
pub struct LinuxAudioMixer {
    // Will hold PipeWire state
}

impl LinuxAudioMixer {
    pub fn new() -> Self {
        Self {
            // Initialize PipeWire resources
        }
    }
}

impl AudioMixerService for LinuxAudioMixer {
    fn add_source(&mut self, _source: AudioSource) -> Result<String, AudioError> {
        Err(AudioError::CaptureFailed(
            "Linux audio backend not yet implemented. This is a future feature.".to_string(),
        ))
    }

    fn remove_source(&mut self, _source_id: &str) -> Result<(), AudioError> {
        Err(AudioError::CaptureFailed(
            "Linux audio backend not yet implemented. This is a future feature.".to_string(),
        ))
    }

    fn set_volume(&mut self, _source_id: &str, _volume: f32) -> Result<(), AudioError> {
        Err(AudioError::CaptureFailed(
            "Linux audio backend not yet implemented. This is a future feature.".to_string(),
        ))
    }

    fn set_muted(&mut self, _source_id: &str, _muted: bool) -> Result<(), AudioError> {
        Err(AudioError::CaptureFailed(
            "Linux audio backend not yet implemented. This is a future feature.".to_string(),
        ))
    }

    fn get_devices(&self) -> Result<Vec<AudioDevice>, AudioError> {
        Err(AudioError::CaptureFailed(
            "Linux audio backend not yet implemented. This is a future feature.".to_string(),
        ))
    }

    fn get_state(&self) -> MixerState {
        MixerState {
            sources: Vec::new(),
            master_volume: 1.0,
            is_capturing: false,
        }
    }

    fn start_capture(&mut self) -> Result<(), AudioError> {
        Err(AudioError::CaptureFailed(
            "Linux audio backend not yet implemented. This is a future feature.".to_string(),
        ))
    }

    fn stop_capture(&mut self) -> Result<(), AudioError> {
        Ok(())
    }
}

/// Linux hotkey service using X11/Wayland
pub struct LinuxHotkeyService {
    // Will hold X11/Wayland hotkey state
}

impl LinuxHotkeyService {
    pub fn new() -> Self {
        Self {
            // Initialize X11/Wayland hotkey resources
        }
    }
}

impl HotkeyService for LinuxHotkeyService {
    fn register(&mut self, _hotkey: Hotkey) -> Result<String, HotkeyError> {
        Err(HotkeyError::RegistrationFailed(
            "Linux hotkey backend not yet implemented. This is a future feature.".to_string(),
        ))
    }

    fn unregister(&mut self, _hotkey_id: &str) -> Result<(), HotkeyError> {
        Err(HotkeyError::NotFound(
            "Linux hotkey backend not yet implemented. This is a future feature.".to_string(),
        ))
    }

    fn update(&mut self, _hotkey_id: &str, _hotkey: Hotkey) -> Result<(), HotkeyError> {
        Err(HotkeyError::NotFound(
            "Linux hotkey backend not yet implemented. This is a future feature.".to_string(),
        ))
    }

    fn get_registered(&self) -> Vec<HotkeyRegistration> {
        Vec::new()
    }
}
