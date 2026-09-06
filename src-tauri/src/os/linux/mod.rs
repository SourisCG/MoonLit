//! Linux backend assembly. Everything Linux-only is reachable via this module.

pub mod audio;
pub mod caps;
pub mod devices;
pub mod video;
mod gsr;

pub use gsr::LinuxGsrEngine as Engine;

pub fn backend_name() -> &'static str {
    "gpu-screen-recorder"
}
