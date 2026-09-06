//! Windows backend assembly. Full WGC + WASAPI implementation happens
//! on the Windows test trip (see docs/PROGRESS.md cross-platform gate).
//! Same surface as os/linux so shared code never branches on OS.

pub mod audio;
pub mod caps;
pub mod devices;
mod wgc;

pub use wgc::WindowsCaptureEngine as Engine;

pub fn backend_name() -> &'static str {
    "windows-capture (stub)"
}
