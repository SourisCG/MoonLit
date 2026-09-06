//! Shared app state (Phase 3): the capture engine behind an async mutex.

use tokio::sync::Mutex;

#[cfg(target_os = "linux")]
pub type Engine = crate::capture::linux::LinuxGsrEngine;
#[cfg(target_os = "windows")]
pub type Engine = crate::capture::windows::WindowsCaptureEngine;
#[cfg(not(any(target_os = "linux", target_os = "windows")))]
pub type Engine = crate::capture::linux::LinuxGsrEngine;

#[derive(Default)]
pub struct AppState {
    pub recorder: Mutex<Option<Engine>>,
}
