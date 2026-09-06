//! FFmpeg helpers (Phase 3: thumbnails only; trim presets land in Phase 5).
//! Binary resolution: MOONLIT_FFMPEG override -> app-bundled sidecar -> PATH.

use std::path::{Path, PathBuf};
use tauri::{AppHandle, Manager};

pub fn resolve_ffmpeg(app: &AppHandle) -> Result<PathBuf, String> {
    if let Ok(path) = std::env::var("MOONLIT_FFMPEG") {
        let p = PathBuf::from(&path);
        if p.exists() {
            return Ok(p);
        }
    }
    // Bundled sidecar: <res>/binaries/ffmpeg-<target> (Tauri externalBin layout).
    if let Ok(res) = app.path().resource_dir() {
        let dir = res.join("binaries");
        if let Ok(entries) = std::fs::read_dir(&dir) {
            let mut cands: Vec<PathBuf> = entries
                .filter_map(|e| e.ok())
                .map(|e| e.path())
                .filter(|p| {
                    p.file_name()
                        .and_then(|n| n.to_str())
                        .map(|n| n.starts_with("ffmpeg"))
                        .unwrap_or(false)
                })
                .collect();
            cands.sort();
            if let Some(p) = cands.into_iter().next() {
                return Ok(p);
            }
        }
    }
    // Dev fallback: system ffmpeg.
    Ok(PathBuf::from("ffmpeg"))
}

/// Extract one JPEG thumbnail at 1 s. Fast (no re-encode of the clip).
pub async fn make_thumbnail(
    ffmpeg: &Path,
    input: &Path,
    output: &Path,
) -> Result<(), String> {
    let status = tokio::process::Command::new(ffmpeg)
        .args([
            "-y", "-hide_banner", "-loglevel", "error",
            "-ss", "00:00:01",
            "-i", &input.to_string_lossy(),
            "-vframes", "1",
            "-q:v", "2",
        ])
        .arg(output)
        .status()
        .await
        .map_err(|e| format!("ffmpeg thumbnail failed: {e}"))?;
    if !status.success() {
        return Err("ffmpeg thumbnail failed".into());
    }
    Ok(())
}
