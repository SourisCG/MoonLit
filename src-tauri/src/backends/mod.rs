//! Backend implementations and platform factory.

use std::path::PathBuf;

use crate::traits::{BackendDescriptor, BackendError, BackendId, ReplayBackend};

pub mod fake;

// The host services are deliberately included in a test-only namespace. This
// keeps their unit tests out of the Tauri entrypoint test binary while still
// compiling the real service implementations and fixtures. Production keeps
// the normal root modules from lib.rs.
#[cfg(test)]
#[path = "../config.rs"]
pub(crate) mod host_config;
#[cfg(test)]
#[path = "../library.rs"]
pub(crate) mod host_library;
#[cfg(test)]
#[path = "../media.rs"]
pub(crate) mod host_media;
#[cfg(test)]
#[path = "../recorder.rs"]
pub(crate) mod host_recorder;
#[cfg(test)]
#[path = "../state.rs"]
pub(crate) mod host_state;
#[cfg(test)]
#[path = "../storage.rs"]
pub(crate) mod host_storage;

#[cfg(target_os = "linux")]
pub mod gsr;

#[cfg(target_os = "windows")]
pub mod libobs;

#[cfg(target_os = "windows")]
pub mod windows;

pub fn descriptors(resource_dir: Option<PathBuf>) -> Vec<BackendDescriptor> {
    let mut descriptors = vec![fake::FakeBackend::new().descriptor()];

    #[cfg(not(any(target_os = "windows", target_os = "linux")))]
    let _ = resource_dir;
    #[cfg(target_os = "windows")]
    let _ = &resource_dir;

    #[cfg(target_os = "windows")]
    {
        descriptors.push(
            libobs::LibobsSidecarBackend::discover_with_resource_dir(resource_dir.clone())
                .descriptor(),
        );
        descriptors.push(windows::WindowsNativeBackend::new().descriptor());
    }

    #[cfg(target_os = "linux")]
    {
        descriptors
            .push(gsr::LegacyGsrBackend::discover_with_resource_dir(resource_dir).descriptor());
    }

    descriptors
}

pub fn create(
    id: BackendId,
    resource_dir: Option<PathBuf>,
) -> Result<Box<dyn ReplayBackend>, BackendError> {
    match id {
        BackendId::Fake => Ok(Box::new(fake::FakeBackend::new())),
        BackendId::LibobsSidecar => {
            #[cfg(target_os = "windows")]
            {
                Ok(Box::new(
                    libobs::LibobsSidecarBackend::discover_with_resource_dir(resource_dir),
                ))
            }
            #[cfg(not(target_os = "windows"))]
            {
                let _ = resource_dir;
                Err(BackendError::new(
                    crate::traits::BackendErrorCode::Unsupported,
                    "El backend libobs sidecar solo esta disponible en Windows por ahora",
                    false,
                ))
            }
        }
        BackendId::WindowsNative => {
            #[cfg(target_os = "windows")]
            {
                Ok(Box::new(windows::WindowsNativeBackend::new()))
            }
            #[cfg(not(target_os = "windows"))]
            {
                let _ = resource_dir;
                Err(BackendError::new(
                    crate::traits::BackendErrorCode::Unsupported,
                    "El backend Windows no esta disponible en esta plataforma",
                    false,
                ))
            }
        }
        BackendId::LegacyGsr => {
            #[cfg(target_os = "linux")]
            {
                Ok(Box::new(gsr::LegacyGsrBackend::discover_with_resource_dir(
                    resource_dir,
                )))
            }
            #[cfg(not(target_os = "linux"))]
            {
                let _ = resource_dir;
                Err(BackendError::new(
                    crate::traits::BackendErrorCode::Unsupported,
                    "GSR legacy solo esta disponible en Linux",
                    false,
                ))
            }
        }
    }
}
