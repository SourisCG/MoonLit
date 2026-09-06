//! Capture engines (Phase 3). Platform backends behind one trait.
//! Linux: gpu-screen-recorder sidecar. Windows: native stub (full impl on Windows trip).

use std::path::PathBuf;

#[cfg(target_os = "linux")]
pub mod linux;
#[cfg(target_os = "linux")]
pub mod audio;
#[cfg(target_os = "windows")]
pub mod windows;

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct CaptureConfig {
    pub duration_seconds: u32,
    pub fps: u32,
    pub output_dir: PathBuf,
    /// Resolved GSR binary (bundled sidecar). None = resolve from env/PATH.
    pub gsr_bin: Option<PathBuf>,
}

/// Unified engine interface. Methods are async to allow signal waits / IPC.
#[allow(async_fn_in_trait)]
pub trait CaptureEngine: Send + Sync {
    async fn start_buffer(&mut self, config: CaptureConfig) -> Result<(), String>;
    async fn save_clip(&mut self) -> Result<PathBuf, String>;
    async fn stop_buffer(&mut self) -> Result<(), String>;
    fn backend_name(&self) -> &'static str;
}
