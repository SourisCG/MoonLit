//! Windows engine STUB (Phase 3). Full WGC + WASAPI implementation happens
//! on the Windows test trip (see docs/PROGRESS.md cross-platform gate).
//! This stub keeps the trait unified and the app building on Windows.

use std::path::PathBuf;

use super::super::{CaptureConfig, CaptureEngine};

pub struct WindowsCaptureEngine;

impl WindowsCaptureEngine {
    pub fn new() -> Self {
        Self
    }
}

impl CaptureEngine for WindowsCaptureEngine {
    async fn start_buffer(&mut self, _config: CaptureConfig) -> Result<(), String> {
        Err("Windows capture engine lands on the Windows test trip (Phase 3 gate)".into())
    }

    async fn save_clip(&mut self) -> Result<PathBuf, String> {
        Err("Windows capture engine lands on the Windows test trip (Phase 3 gate)".into())
    }

    async fn stop_buffer(&mut self) -> Result<(), String> {
        Ok(())
    }

    fn backend_name(&self) -> &'static str {
        "windows-capture (stub)"
    }
}
