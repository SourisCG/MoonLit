//! Default library locations (Linux): XDG user media dirs.
//! Unchanged by the Windows AV-safe relocation (Linux has no Controlled
//! Folder Access and users expect media under ~/Videos).

use std::path::PathBuf;

/// Default clips directory: ~/Videos/MoonLit (or data dir fallback).
pub fn default_clips_dir() -> PathBuf {
    if let Some(videos) = dirs::video_dir() {
        return videos.join("MoonLit");
    }
    PathBuf::from("MoonLit")
}

/// No legacy relocation on Linux: the default never moved.
pub fn legacy_default_clips_dir() -> Option<PathBuf> {
    None
}
