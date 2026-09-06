//! Filesystem path helpers. The DB never stores absolute paths.

use std::path::PathBuf;
use tauri::{AppHandle, Manager};

/// Resolve the physical path of a clip from the configured base dir.
pub fn resolve_clip_path(base_dir: &PathBuf, file_name: &str) -> PathBuf {
    base_dir.join(file_name)
}

/// Default clips directory: ~/Videos/MoonLit (or data dir fallback).
pub fn default_clips_dir() -> PathBuf {
    if let Some(videos) = dirs::video_dir() {
        return videos.join("MoonLit");
    }
    PathBuf::from("MoonLit")
}

/// SQLite file location: <app_data>/moonlit.db
pub fn db_file_path(app: &AppHandle) -> Result<PathBuf, String> {
    let dir = app
        .path()
        .app_data_dir()
        .map_err(|e| format!("app data dir unavailable: {e}"))?;
    std::fs::create_dir_all(&dir).map_err(|e| format!("cannot create app data dir: {e}"))?;
    Ok(dir.join("moonlit.db"))
}
