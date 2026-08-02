use serde::Serialize;

use crate::traits::{BackendDescriptor, BackendError, EffectiveReplaySettings, ReplayConfig};

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum CapturePhase {
    Idle,
    Starting,
    Buffering,
    Saving,
    Stopping,
    Faulted,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ClipRecord {
    pub id: String,
    pub path: String,
    pub created_at_ms: u64,
    pub duration_seconds: u32,
    pub kind: String,
    pub size_bytes: u64,
    pub codec: String,
    pub format: String,
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub fps: Option<u32>,
    pub has_audio: bool,
    pub proxy_path: Option<String>,
    pub proxy_status: String,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SessionSnapshot {
    pub id: String,
    pub source_id: String,
    pub source_label: String,
    pub started_at_ms: u64,
}

#[derive(Clone, Debug, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CaptureSnapshot {
    pub revision: u64,
    pub phase: CapturePhase,
    pub backend: BackendDescriptor,
    pub config: Option<ReplayConfig>,
    pub effective: Option<EffectiveReplaySettings>,
    pub can_save: bool,
    pub session: Option<SessionSnapshot>,
    pub saved_clips: u32,
    pub last_clip: Option<ClipRecord>,
    pub last_error: Option<BackendError>,
}

#[derive(Clone, Debug, Serialize)]
#[serde(tag = "type", rename_all = "camelCase")]
pub enum RecorderEvent {
    StateChanged {
        snapshot: CaptureSnapshot,
    },
    ClipSaved {
        snapshot: CaptureSnapshot,
        clip: ClipRecord,
    },
    ErrorOccurred {
        snapshot: CaptureSnapshot,
        error: BackendError,
    },
}
