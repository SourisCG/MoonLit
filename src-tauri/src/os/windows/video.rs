//! Windows video discovery stub. Real WGC capability queries land on the
//! Windows test trip. Same signatures as os/linux/video.

use std::path::Path;

/// Mirrors os/linux/video::vendor signature.
pub async fn vendor(_bin: &Path) -> String {
    "unknown".into()
}

/// Monitor descriptor (mirrors os/linux/video::Monitor).
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct Monitor {
    pub name: String,
    pub width: u32,
    pub height: u32,
}

/// Mirrors os/linux/video::list_monitors signature.
pub async fn list_monitors(_bin: &Path) -> Vec<Monitor> {
    vec![]
}

/// Mirrors os/linux/video::offered_codecs signature.
pub async fn offered_codecs(_bin: &Path) -> Vec<String> {
    vec![]
}
