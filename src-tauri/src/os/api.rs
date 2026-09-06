//! Shared capture contract (OS-independent).
//! Every backend under os/linux and os/windows implements this trait.
//! Mirrors the OBS model: uniform interface, per-OS files behind it.

use std::path::PathBuf;

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct CaptureConfig {
    pub duration_seconds: u32,
    pub fps: u32,
    pub output_dir: PathBuf,
    /// Resolved GSR binary (bundled sidecar). None = resolve from env/PATH.
    pub gsr_bin: Option<PathBuf>,
    /// Audio sources for `-a` (game first, mic second).
    pub desktop_device: String,
    pub mic_device: String,
    /// Video codec id for `-k` (h264/hevc/av1, as listed by the backend).
    pub codec: String,
    /// Output height for `-s` (0 = source resolution).
    pub out_height: u32,
    /// CBR bitrate in kbps for `-q`.
    pub bitrate_kbps: u32,
    /// Extra `-ffmpeg-video-opts` (NVENC HQ recipe, NVIDIA only). None = backend defaults.
    pub nvenc_opts: Option<String>,
}

/// Unified engine interface. Methods are async to allow signal waits / IPC.
#[allow(async_fn_in_trait)]
pub trait CaptureEngine: Send + Sync {
    async fn start_buffer(&mut self, config: CaptureConfig) -> Result<(), String>;
    async fn save_clip(&mut self) -> Result<PathBuf, String>;
    async fn stop_buffer(&mut self) -> Result<(), String>;
    fn backend_name(&self) -> &'static str;
    /// The exact `-a` audio args the running engine spawned with (for stream matching).
    fn audio_args(&self) -> Vec<String> {
        vec![]
    }
}

/// One capture device from `--list-audio-devices` (or OS enumeration).
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct AudioDevice {
    pub id: String,
    pub description: String,
    /// "mic" or "desktop"
    pub kind: String,
}
