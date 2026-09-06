//! Windows video discovery stub. Real WGC capability queries land on the
//! Windows test trip. Same signatures as os/linux/video.

use std::path::Path;

/// Mirrors os/linux/video::vendor signature.
pub async fn vendor(_bin: &Path) -> String {
    "unknown".into()
}

/// Mirrors os/linux/video::max_source_height signature.
pub async fn max_source_height(_bin: &Path) -> u32 {
    0
}

/// Mirrors os/linux/video::offered_codecs signature.
pub async fn offered_codecs(_bin: &Path) -> Vec<String> {
    vec![]
}
