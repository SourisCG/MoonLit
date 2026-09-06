//! Windows device enumeration stub. Real cpal enumeration lands on the
//! Windows test trip. Same item shape as os/linux/devices.

use super::super::AudioDevice;
use std::path::Path;

/// Mirrors os/linux/devices::list_audio_devices signature.
pub async fn list_audio_devices(_bin: &Path) -> Result<Vec<AudioDevice>, String> {
    Err("device list lands on the Windows trip".into())
}
