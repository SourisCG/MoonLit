//! Platform-specific backend implementations
//!
//! This module contains implementations of the portable traits defined in `crate::traits`.
//! Each backend is conditionally compiled based on the target platform.

pub mod fake;

#[cfg(target_os = "windows")]
pub mod windows;

#[cfg(target_os = "linux")]
pub mod linux;

// Re-export the appropriate backend based on platform
#[cfg(target_os = "windows")]
pub use windows::WindowsCaptureBackend;

#[cfg(target_os = "linux")]
pub use linux::LinuxCaptureBackend;

pub use fake::FakeBackend;
