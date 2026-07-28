//! Fake backend implementation for development and testing
//!
//! This backend simulates capture operations without requiring real hardware.
//! Useful for development on Linux and testing UI without actual capture.

#![allow(dead_code)]

use crate::traits::*;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};

static SESSION_COUNTER: AtomicU64 = AtomicU64::new(0);

/// Fake capture backend for development and testing
pub struct FakeBackend {
    is_capturing: AtomicBool,
    sources: Vec<CaptureSource>,
}

impl FakeBackend {
    pub fn new() -> Self {
        Self {
            is_capturing: AtomicBool::new(false),
            sources: vec![
                CaptureSource::Monitor("Fake Monitor 1".to_string()),
                CaptureSource::Monitor("Fake Monitor 2".to_string()),
                CaptureSource::Window("Fake Window 1".to_string()),
            ],
        }
    }
}

impl Default for FakeBackend {
    fn default() -> Self {
        Self::new()
    }
}

impl CaptureService for FakeBackend {
    fn start_replay(&mut self, config: CaptureConfig) -> Result<CaptureSession, CaptureError> {
        if self.is_capturing.load(Ordering::SeqCst) {
            return Err(CaptureError::CaptureFailed("Already capturing".to_string()));
        }

        self.is_capturing.store(true, Ordering::SeqCst);

        let session_id = format!(
            "fake-session-{}",
            SESSION_COUNTER.fetch_add(1, Ordering::SeqCst)
        );

        Ok(CaptureSession {
            id: session_id,
            source: config.source,
            start_time: std::time::Instant::now(),
            duration: config.duration,
        })
    }

    fn save_clip(&mut self, session: &mut CaptureSession) -> Result<PathBuf, CaptureError> {
        if !self.is_capturing.load(Ordering::SeqCst) {
            return Err(CaptureError::CaptureFailed("Not capturing".to_string()));
        }

        // Simulate saving a clip
        let timestamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs();

        let clip_name = format!("fake-clip-{}-{}.mp4", session.id, timestamp);

        let clip_path = std::env::temp_dir().join("moonlit").join(clip_name);

        // Create fake file (just a placeholder)
        std::fs::create_dir_all(clip_path.parent().unwrap())?;
        std::fs::write(&clip_path, b"FAKE_VIDEO_DATA")?;

        Ok(clip_path)
    }

    fn stop(&mut self, _session: &mut CaptureSession) -> Result<(), CaptureError> {
        self.is_capturing.store(false, Ordering::SeqCst);
        Ok(())
    }

    fn get_sources(&self) -> Result<Vec<CaptureSource>, CaptureError> {
        Ok(self.sources.clone())
    }

    fn is_capturing(&self) -> bool {
        self.is_capturing.load(Ordering::SeqCst)
    }

    fn backend_name(&self) -> &str {
        "FakeBackend"
    }

    fn capabilities(&self) -> BackendCapabilities {
        BackendCapabilities {
            supports_window_capture: true,
            supports_monitor_capture: true,
            supports_region_capture: true,
            max_resolution: Some((3840, 2160)),
            max_fps: Some(144),
            supported_codecs: vec![VideoCodec::H264, VideoCodec::H265],
        }
    }
}

/// Fake audio mixer for development and testing
pub struct FakeAudioMixer {
    sources: Vec<(String, AudioSource, f32, bool)>, // (id, source, volume, muted)
    is_capturing: bool,
    master_volume: f32,
}

impl FakeAudioMixer {
    pub fn new() -> Self {
        Self {
            sources: vec![
                (
                    "fake-system-audio".to_string(),
                    AudioSource::SystemAudio("System Audio".to_string()),
                    1.0,
                    false,
                ),
                (
                    "fake-microphone".to_string(),
                    AudioSource::Microphone("Microphone".to_string()),
                    1.0,
                    false,
                ),
            ],
            is_capturing: false,
            master_volume: 1.0,
        }
    }
}

impl Default for FakeAudioMixer {
    fn default() -> Self {
        Self::new()
    }
}

impl AudioMixerService for FakeAudioMixer {
    fn add_source(&mut self, source: AudioSource) -> Result<String, AudioError> {
        let id = format!("fake-source-{}", self.sources.len());
        self.sources.push((id.clone(), source, 1.0, false));
        Ok(id)
    }

    fn remove_source(&mut self, source_id: &str) -> Result<(), AudioError> {
        self.sources.retain(|(id, _, _, _)| id != source_id);
        Ok(())
    }

    fn set_volume(&mut self, source_id: &str, volume: f32) -> Result<(), AudioError> {
        if let Some((_, _, vol, _)) = self
            .sources
            .iter_mut()
            .find(|(id, _, _, _)| id == source_id)
        {
            *vol = volume.clamp(0.0, 1.0);
            Ok(())
        } else {
            Err(AudioError::DeviceNotFound(source_id.to_string()))
        }
    }

    fn set_muted(&mut self, source_id: &str, muted: bool) -> Result<(), AudioError> {
        if let Some((_, _, _, is_muted)) = self
            .sources
            .iter_mut()
            .find(|(id, _, _, _)| id == source_id)
        {
            *is_muted = muted;
            Ok(())
        } else {
            Err(AudioError::DeviceNotFound(source_id.to_string()))
        }
    }

    fn get_devices(&self) -> Result<Vec<AudioDevice>, AudioError> {
        Ok(vec![
            AudioDevice {
                id: "fake-speakers".to_string(),
                name: "Fake Speakers".to_string(),
                device_type: AudioDeviceType::Output,
                is_default: true,
            },
            AudioDevice {
                id: "fake-mic".to_string(),
                name: "Fake Microphone".to_string(),
                device_type: AudioDeviceType::Input,
                is_default: true,
            },
        ])
    }

    fn get_state(&self) -> MixerState {
        MixerState {
            sources: self
                .sources
                .iter()
                .map(|(id, source, volume, muted)| {
                    let name = match source {
                        AudioSource::SystemAudio(n) => n.clone(),
                        AudioSource::Microphone(n) => n.clone(),
                        AudioSource::Application(n) => n.clone(),
                    };
                    SourceState {
                        id: id.clone(),
                        name,
                        volume: *volume,
                        muted: *muted,
                    }
                })
                .collect(),
            master_volume: self.master_volume,
            is_capturing: self.is_capturing,
        }
    }

    fn start_capture(&mut self) -> Result<(), AudioError> {
        self.is_capturing = true;
        Ok(())
    }

    fn stop_capture(&mut self) -> Result<(), AudioError> {
        self.is_capturing = false;
        Ok(())
    }
}

/// Fake hotkey service for development and testing
pub struct FakeHotkeyService {
    hotkeys: Vec<(String, Hotkey)>,
}

impl FakeHotkeyService {
    pub fn new() -> Self {
        Self {
            hotkeys: Vec::new(),
        }
    }
}

impl Default for FakeHotkeyService {
    fn default() -> Self {
        Self::new()
    }
}

impl HotkeyService for FakeHotkeyService {
    fn register(&mut self, hotkey: Hotkey) -> Result<String, HotkeyError> {
        let id = format!("fake-hotkey-{}", self.hotkeys.len());
        self.hotkeys.push((id.clone(), hotkey));
        Ok(id)
    }

    fn unregister(&mut self, hotkey_id: &str) -> Result<(), HotkeyError> {
        self.hotkeys.retain(|(id, _)| id != hotkey_id);
        Ok(())
    }

    fn update(&mut self, hotkey_id: &str, hotkey: Hotkey) -> Result<(), HotkeyError> {
        if let Some((_, hk)) = self.hotkeys.iter_mut().find(|(id, _)| id == hotkey_id) {
            *hk = hotkey;
            Ok(())
        } else {
            Err(HotkeyError::NotFound(hotkey_id.to_string()))
        }
    }

    fn get_registered(&self) -> Vec<HotkeyRegistration> {
        self.hotkeys
            .iter()
            .map(|(id, hotkey)| HotkeyRegistration {
                id: id.clone(),
                hotkey: hotkey.clone(),
            })
            .collect()
    }
}
