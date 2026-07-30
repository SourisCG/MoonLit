use std::mem::size_of;

use windows::core::BOOL;
use windows::Win32::Foundation::LPARAM;
use windows::Win32::Graphics::Gdi::{
    EnumDisplayMonitors, GetMonitorInfoW, HMONITOR, MONITORENUMPROC, MONITORINFOEXW,
};

use crate::{NativeError, NativeSource, SourceKind};

#[derive(Clone, Copy)]
pub(crate) struct MonitorTarget {
    pub handle: HMONITOR,
    pub width: u32,
    pub height: u32,
}

pub(crate) fn enumerate_monitors() -> Result<Vec<(NativeSource, MonitorTarget)>, NativeError> {
    let mut handles = Vec::new();
    let result = unsafe {
        EnumDisplayMonitors(
            None,
            None,
            Some(monitor_callback),
            LPARAM(&mut handles as *mut Vec<HMONITOR> as isize),
        )
    };
    if !result.as_bool() {
        return Err(NativeError::windows(
            "EnumDisplayMonitors",
            windows::core::Error::from_thread(),
        ));
    }

    handles
        .into_iter()
        .enumerate()
        .map(|(index, handle)| {
            let mut info = MONITORINFOEXW::default();
            info.monitorInfo.cbSize = size_of::<MONITORINFOEXW>() as u32;
            let result = unsafe { GetMonitorInfoW(handle, &mut info.monitorInfo) };
            if !result.as_bool() {
                return Err(NativeError::windows(
                    "GetMonitorInfoW",
                    windows::core::Error::from_thread(),
                ));
            }

            let width =
                (info.monitorInfo.rcMonitor.right - info.monitorInfo.rcMonitor.left).max(0) as u32;
            let height =
                (info.monitorInfo.rcMonitor.bottom - info.monitorInfo.rcMonitor.top).max(0) as u32;
            let device_name = utf16_string(&info.szDevice);
            let label = if device_name.is_empty() {
                format!("Monitor {}", index + 1)
            } else {
                format!("Monitor {} ({device_name})", index + 1)
            };

            Ok((
                NativeSource {
                    id: format!("monitor-{index}"),
                    kind: SourceKind::Monitor,
                    label,
                    is_default: index == 0,
                    width,
                    height,
                },
                MonitorTarget {
                    handle,
                    width,
                    height,
                },
            ))
        })
        .collect()
}

pub(crate) fn monitor_for_source(source_id: &str) -> Result<MonitorTarget, NativeError> {
    enumerate_monitors()?
        .into_iter()
        .find_map(|(source, target)| (source.id == source_id).then_some(target))
        .ok_or_else(|| NativeError::SourceNotFound(source_id.to_string()))
}

unsafe extern "system" fn monitor_callback(
    monitor: HMONITOR,
    _device_context: windows::Win32::Graphics::Gdi::HDC,
    _monitor_rect: *mut windows::Win32::Foundation::RECT,
    data: LPARAM,
) -> BOOL {
    let handles = unsafe { &mut *(data.0 as *mut Vec<HMONITOR>) };
    handles.push(monitor);
    BOOL(1)
}

fn utf16_string(value: &[u16]) -> String {
    let length = value
        .iter()
        .position(|character| *character == 0)
        .unwrap_or(value.len());
    String::from_utf16_lossy(&value[..length])
}

const _: MONITORENUMPROC = Some(monitor_callback);
