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

/// Measure real duration in ms via `ffmpeg -i` stderr (no ffprobe needed —
/// the static sidecar does not ship ffprobe). Returns None on parse failure.
pub async fn probe_duration_ms(ffmpeg: &Path, input: &Path) -> Option<i64> {
    let out = tokio::process::Command::new(ffmpeg)
        .args(["-hide_banner", "-i", &input.to_string_lossy()])
        .output()
        .await
        .ok()?;
    let stderr = String::from_utf8_lossy(&out.stderr);
    // Line looks like: Duration: 00:01:20.65, start: 0.000000, bitrate: ...
    let line = stderr.lines().find(|l| l.trim_start().starts_with("Duration:"))?;
    let time = line.split(',').next()?.split("Duration:").nth(1)?.trim();
    let mut parts = time.split(':');
    let h: i64 = parts.next()?.parse().ok()?;
    let m: i64 = parts.next()?.parse().ok()?;
    let s: f64 = parts.next()?.parse().ok()?;
    Some(((h * 3600 + m * 60) as f64 * 1000.0 + s * 1000.0) as i64)
}

/// Downscale to `height` (aspect kept, width auto-even) with lanczos + NVENC.
///
/// Used at save time instead of the backend's live scaler, which proved soft
/// on text at non-integer ratios (1080p→720p). Same CBR ladder bitrate as a
/// direct capture would use. Returns false on any failure (caller keeps source).
pub async fn scale_to_height(
    ffmpeg: &Path,
    input: &Path,
    output: &Path,
    height: u32,
    bitrate_kbps: u32,
    codec: &str,
    fps: u32,
) -> bool {
    let encoder = match codec {
        "hevc" => "hevc_nvenc",
        "av1" => "av1_nvenc",
        _ => "h264_nvenc",
    };
    let mut cmd = tokio::process::Command::new(ffmpeg);
    cmd.args([
        "-y", "-hide_banner", "-loglevel", "error",
        "-i", &input.to_string_lossy(),
        "-vf", &format!("scale=-2:{height}:flags=lanczos"),
        "-c:v", encoder,
        "-preset", "p7",
        "-tune", "hq",
    ]);
    // `high` is an H.264 profile; HEVC uses `main`.
    if codec == "hevc" {
        cmd.args(["-profile:v", "main"]);
    } else if codec == "h264" {
        cmd.args(["-profile:v", "high"]);
    }
    let gop = (fps.max(1) * 2).to_string();
    let out = cmd
        .args([
            "-bf", "2",
            "-b:v", &format!("{bitrate_kbps}k"),
            "-maxrate", &format!("{bitrate_kbps}k"),
            "-bufsize", &format!("{bitrate_kbps}k"),
            "-g", &gop,
            "-c:a", "copy",
        ])
        .arg(output)
        .output()
        .await;
    matches!(out, Ok(o) if o.status.success())
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
