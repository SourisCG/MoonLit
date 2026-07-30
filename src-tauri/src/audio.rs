use std::sync::Mutex;

use serde::{Deserialize, Serialize};
use tauri::State;

use crate::config::ConfigState;
use crate::traits::AudioConfig;

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum AudioDeviceKind {
    System,
    Microphone,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AudioDevice {
    pub id: String,
    pub kind: AudioDeviceKind,
    pub label: String,
    pub is_default: bool,
    pub available: bool,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AudioMixerSnapshot {
    pub revision: u64,
    pub devices: Vec<AudioDevice>,
    pub config: AudioConfig,
    pub system_level: f32,
    pub microphone_level: f32,
    pub sync_drift_ms: Option<i32>,
    pub status: String,
}

pub struct AudioMixer {
    snapshot: AudioMixerSnapshot,
}

impl AudioMixer {
    pub fn new(config: AudioConfig) -> Self {
        Self {
            snapshot: AudioMixerSnapshot {
                revision: 0,
                devices: vec![
                    AudioDevice {
                        id: "default-system".to_string(),
                        kind: AudioDeviceKind::System,
                        label: "Salida predeterminada".to_string(),
                        is_default: true,
                        available: true,
                    },
                    AudioDevice {
                        id: "default-microphone".to_string(),
                        kind: AudioDeviceKind::Microphone,
                        label: "Micrófono predeterminado".to_string(),
                        is_default: true,
                        available: true,
                    },
                ],
                config,
                system_level: 0.0,
                microphone_level: 0.0,
                sync_drift_ms: Some(0),
                status: "simulated".to_string(),
            },
        }
    }

    fn update(&mut self, config: AudioConfig) -> AudioMixerSnapshot {
        self.snapshot.revision = self.snapshot.revision.saturating_add(1);
        self.snapshot.config = config;
        self.snapshot.clone()
    }
}

pub struct AudioState(pub Mutex<AudioMixer>);

#[tauri::command]
pub fn get_audio_snapshot(state: State<'_, AudioState>) -> Result<AudioMixerSnapshot, String> {
    Ok(state
        .0
        .lock()
        .map_err(|_| "El mezclador esta bloqueado".to_string())?
        .snapshot
        .clone())
}

#[tauri::command]
pub fn set_audio_config(
    state: State<'_, AudioState>,
    config_state: State<'_, ConfigState>,
    config: AudioConfig,
) -> Result<AudioMixerSnapshot, String> {
    let snapshot = state
        .0
        .lock()
        .map_err(|_| "El mezclador esta bloqueado".to_string())?
        .update(config.clone());
    let store = config_state
        .0
        .lock()
        .map_err(|_| "La configuracion esta bloqueada".to_string())?;
    let mut app_config = store.load()?;
    app_config.replay.audio = config;
    store.save(&app_config)?;
    Ok(snapshot)
}
