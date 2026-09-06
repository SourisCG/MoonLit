//! Backend binary resolution (Windows): there is nothing to resolve —
//! capture uses native WGC/DXGI + WASAPI APIs, no sidecar binary.
//! Same signature as `os/linux/binary.rs` so shared code never branches.

use std::path::PathBuf;
use tauri::AppHandle;

/// Always errors on Windows (by design): the engine is native.
pub fn backend_binary(_app: &AppHandle) -> Result<(PathBuf, &'static str), String> {
    Err("capture backend is native on Windows (WGC) — no sidecar binary".into())
}
