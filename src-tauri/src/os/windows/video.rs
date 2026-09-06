//! Windows video discovery stub. Real WGC capability queries land on the
//! Windows test trip. Same signatures as os/linux/video.

use super::super::TranscodeEncoder;
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

/// Save-time transcode encoder for (`vendor`, `codec`) — mirrors
/// os/linux/video. Real DXGI adapter detection lands on the Windows trip;
/// the mapping itself is pure data. AMD→Amf, Intel→Qsv, NVIDIA→Nvenc.
pub fn transcode_encoder(vendor: &str, _codec: &str) -> Option<TranscodeEncoder> {
    match vendor {
        "nvidia" => Some(TranscodeEncoder::Nvenc),
        "amd" => Some(TranscodeEncoder::Amf),
        "intel" => Some(TranscodeEncoder::Qsv),
        _ => None,
    }
}

/// Mirrors os/linux/video::offered_codecs signature.
pub async fn offered_codecs(_bin: &Path) -> Vec<String> {
    vec![]
}
