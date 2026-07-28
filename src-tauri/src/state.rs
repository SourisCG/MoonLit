use serde::Serialize;

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum CaptureStatus {
    Idle,
    Buffering,
    #[allow(dead_code)]
    Error,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ClipRecord {
    pub id: String,
    pub path: String,
    pub created_at: u64,
    pub duration_seconds: u32,
    pub kind: String,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RuntimeSnapshot {
    pub status: CaptureStatus,
    pub backend: String,
    pub session_id: Option<String>,
    pub game_label: Option<String>,
    pub started_at: Option<u64>,
    pub buffer_seconds: u32,
    pub saved_clips: u32,
    pub last_clip: Option<ClipRecord>,
    pub message: String,
}

impl Default for RuntimeSnapshot {
    fn default() -> Self {
        Self {
            status: CaptureStatus::Idle,
            backend: "fake".to_string(),
            session_id: None,
            game_label: None,
            started_at: None,
            buffer_seconds: 30,
            saved_clips: 0,
            last_clip: None,
            message: "Listo para iniciar una prueba.".to_string(),
        }
    }
}
