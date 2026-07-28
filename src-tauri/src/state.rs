use serde::Serialize;

use crate::traits::{BackendDescriptor, BackendError, ReplayConfig};

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
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SessionSnapshot {
    pub id: String,
    pub source_id: String,
    pub source_label: String,
    pub started_at_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CaptureSnapshot {
    pub revision: u64,
    pub phase: CapturePhase,
    pub backend: BackendDescriptor,
    pub config: Option<ReplayConfig>,
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
