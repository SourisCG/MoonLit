//! Sidecar path resolution (OS-free): bundled binaries first, system fallback.
//! Layout: <res|exe>/binaries/<triple>/gpu-screen-recorder (+ gsr-kms-server, ffmpeg).
//! Dev layout: src-tauri/binaries/<triple>/ (walked up from target/debug/<bin>).
//! OS-specific bits (caps, device lists) live under os/, never here.

use std::path::PathBuf;
use tauri::{AppHandle, Manager};

pub fn host_triple() -> &'static str {
    match std::env::consts::ARCH {
        "x86_64" => "x86_64-unknown-linux-gnu",
        "aarch64" => "aarch64-unknown-linux-gnu",
        _ => "unknown",
    }
}

fn find_in(dir: &std::path::Path, triple: &str, name: &str) -> Option<PathBuf> {
    let p = dir.join("binaries").join(triple).join(name);
    p.exists().then(|| p)
}

/// Search resource dir, exe dir (+ ancestors for dev layout: target/debug/<bin>).
fn search(name: &str, app: &AppHandle) -> Option<(PathBuf, &'static str)> {
    let triple = host_triple();
    if let Ok(res) = app.path().resource_dir() {
        if let Some(p) = find_in(&res, triple, name) {
            return Some((p, "bundled"));
        }
    }
    if let Ok(exe) = std::env::current_exe() {
        let mut dir = exe.parent().map(|p| p.to_path_buf());
        for _ in 0..5 {
            let Some(d) = dir else { break };
            if let Some(p) = find_in(&d, triple, name) {
                return Some((p, "bundled"));
            }
            // Flat dev layout: src-tauri/binaries/<triple> next to Cargo project.
            if d.ends_with("target/debug") || d.ends_with("target/release") {
                if let Some(root) = d.parent().and_then(|p| p.parent()) {
                    let p = root.join("binaries").join(triple).join(name);
                    if p.exists() {
                        return Some((p, "bundled"));
                    }
                }
            }
            dir = d.parent().map(|p| p.to_path_buf());
        }
    }
    None
}

/// Resolve GSR: MOONLIT_GSR_BIN -> bundled sidecar -> system PATH.
pub fn gsr_binary(app: &AppHandle) -> Result<(PathBuf, &'static str), String> {
    if let Ok(path) = std::env::var("MOONLIT_GSR_BIN") {
        let p = PathBuf::from(&path);
        if p.exists() {
            return Ok((p, "env"));
        }
        return Err(format!("MOONLIT_GSR_BIN points nowhere: {path}"));
    }
    if let Some(found) = search("gpu-screen-recorder", app) {
        return Ok(found);
    }
    let out = std::process::Command::new("sh")
        .args(["-c", "command -v gpu-screen-recorder"])
        .output()
        .map_err(|e| e.to_string())?;
    if out.status.success() {
        let p = String::from_utf8_lossy(&out.stdout).trim().to_string();
        if !p.is_empty() {
            return Ok((PathBuf::from(p), "system"));
        }
    }
    Err("gpu-screen-recorder not bundled and not installed. Rebuild with build-aux/build-gsr.sh.".into())
}
