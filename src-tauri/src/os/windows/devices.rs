//! Capture device enumeration (Windows): `cpal` hosts, no sidecar.
//! Output (render) devices are game/desktop sources (WASAPI loopback reads
//! them); input (capture) devices are microphones. Same item shape as
//! `os/linux/devices`; argless on both backends so shared code never
//! branches (Linux resolves its GSR binary internally).

use super::super::AudioDevice;
use cpal::traits::{DeviceTrait, HostTrait};

/// Friendly device name, or `None` when the OS will not name it.
fn dev_name(d: &cpal::Device) -> Option<String> {
    d.name()
        .ok()
        .map(|n| n.trim().to_string())
        .filter(|n| !n.is_empty())
}

pub async fn list_audio_devices() -> Result<Vec<AudioDevice>, String> {
    // Device enumeration is quick; keep it sync-shaped inside async for
    // signature parity with the Linux backend.
    let host = cpal::default_host();
    let mut devices = Vec::new();
    let mut seen = std::collections::HashSet::new();
    let mut push = |id: String, kind: &str| {
        if seen.insert((id.clone(), kind.to_string())) {
            devices.push(AudioDevice {
                id: id.clone(),
                description: id,
                kind: kind.to_string(),
            });
        }
    };
    match host.output_devices() {
        Ok(list) => {
            for d in list {
                if let Some(n) = dev_name(&d) {
                    push(n, "desktop");
                }
            }
        }
        Err(e) => return Err(format!("cannot list output devices: {e}")),
    }
    match host.input_devices() {
        Ok(list) => {
            for d in list {
                if let Some(n) = dev_name(&d) {
                    push(n, "mic");
                }
            }
        }
        Err(e) => return Err(format!("cannot list input devices: {e}")),
    }
    if devices.is_empty() {
        return Err("no audio devices found".into());
    }
    Ok(devices)
}

/// Resolve a `cpal` device by the id `list_audio_devices` returned
/// (the OS friendly name). `render=true` searches outputs (loopback game
/// capture), `render=false` searches inputs (mic).
pub fn find_device(id: &str, render: bool) -> Option<cpal::Device> {
    let host = cpal::default_host();
    let list = if render {
        host.output_devices().ok()?
    } else {
        host.input_devices().ok()?
    };
    list.into_iter().find(|d| dev_name(d).as_deref() == Some(id))
}
