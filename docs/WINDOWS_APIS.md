# Windows APIs Documentation

This document provides detailed information about the Windows APIs used in MoonLit for screen capture, audio capture, GPU encoding, and other Windows-specific features.

## Table of Contents

1. [Windows.Graphics.Capture](#windowsgraphicscapture)
2. [WASAPI (Windows Audio Session API)](#wasapi-windows-audio-session-api)
3. [NVENC (NVIDIA Encoder)](#nvenc-nvidia-encoder)
4. [AMF (Advanced Media Framework)](#amf-advanced-media-framework)
5. [Intel Quick Sync Video](#intel-quick-sync-video)
6. [Win32 Hotkey API](#win32-hotkey-api)
7. [Windows Toast Notifications](#windows-toast-notifications)
8. [Process and Window Enumeration](#process-and-window-enumeration)

---

## Windows.Graphics.Capture

### Overview

Windows.Graphics.Capture is a WinRT API introduced in Windows 10 version 1803 (April 2018 Update) that allows applications to capture screen content, including monitors, windows, and applications. It provides high-performance, low-latency capture with minimal impact on system performance.

### Requirements

- **Minimum Windows Version**: Windows 10 1803 (build 17134)
- **Recommended**: Windows 10 1903+ (build 18362) for additional features
- **Runtime**: Windows SDK 10.0.17763.0 or later

### Key Features

- **Zero-copy capture**: Direct access to GPU textures without CPU copying
- **Multi-monitor support**: Capture any monitor independently
- **Window capture**: Capture specific application windows
- **High frame rates**: Up to 144 FPS or monitor refresh rate
- **HDR support**: Capture HDR content with proper color space
- **Permission system**: User must grant permission (one-time popup)

### Rust Integration

Use the `windows` crate with the following features:

```toml
[dependencies]
windows = { version = "0.62", features = [
    "Graphics_Capture",
    "Graphics_DirectX",
    "Graphics_DirectX_Direct3D11",
    "Graphics_Imaging",
    "Foundation",
    "Foundation_Numerics",
] }
```

### Core Types

```rust
use windows::Graphics::Capture::{
    GraphicsCaptureItem,
    GraphicsCaptureSession,
    Direct3D11CaptureFramePool,
    Direct3D11CaptureFrame,
    GraphicsCaptureSession2,
};

use windows::Graphics::DirectX::Direct3D11::{
    IDirect3DDevice,
    IDirect3DSurface,
};
```

### Capture Flow

```rust
// 1. Create GraphicsCaptureItem from monitor or window
let item = GraphicsCaptureItem::CreateForMonitor(monitor_handle)?;
// or
let item = GraphicsCaptureItem::CreateForWindow(window_handle)?;

// 2. Create Direct3D11 device
let device = create_direct3d_device()?;

// 3. Create frame pool
let frame_pool = Direct3D11CaptureFramePool::Create(
    device.clone(),
    DirectXPixelFormat::B8G8R8A8UIntNormalized,
    2, // number of buffers
    item.Size()?,
)?;

// 4. Create capture session
let session = frame_pool.CreateCaptureSession(item)?;

// 5. Start capture
session.StartCapture()?;

// 6. Get frames (async)
let frame = frame_pool.TryGetNextFrame()?;

// 7. Access frame surface
let surface = frame.Surface()?;

// 8. Stop capture when done
session.Close()?;
```

### Permission Handling

Windows requires explicit user permission for screen capture:

```rust
use windows::Graphics::Capture::GraphicsCaptureAccess;

// Check if permission is granted
let can_capture = GraphicsCaptureAccess::CanCapture()?;

if !can_capture {
    // Request permission (shows system dialog)
    GraphicsCaptureAccess::RequestAccessAsync()?.await?;
}
```

**Note**: On Windows 10 1903+, the permission dialog appears automatically on first capture. On Windows 10 1803, you must explicitly request access.

### Monitor Enumeration

```rust
use windows::Win32::Graphics::Gdi::{
    EnumDisplayMonitors,
    GetMonitorInfoW,
    MONITORINFOEXW,
    HMONITOR,
    HDC,
    RECT,
};

use windows::Win32::Foundation::{BOOL, LPARAM, LPARAM, HWND};

unsafe {
    EnumDisplayMonitors(
        None,
        None,
        Some(monitor_enum_callback),
        LPARAM(&mut monitors as *mut _ as _),
    )?;
}

extern "system" fn monitor_enum_callback(
    hmonitor: HMONITOR,
    _hdc: HDC,
    _rect: *mut RECT,
    lparam: LPARAM,
) -> BOOL {
    let monitors = unsafe { &mut *(lparam.0 as *mut Vec<HMONITOR>) };
    monitors.push(hmonitor);
    BOOL(1) // Continue enumeration
}
```

### Window Enumeration

```rust
use windows::Win32::UI::WindowsAndMessaging::{
    EnumWindows,
    GetWindowTextW,
    GetWindowThreadProcessId,
    IsWindowVisible,
    HWND,
};

unsafe {
    EnumWindows(Some(window_enum_callback), LPARAM(&mut windows as _))?;
}

extern "system" fn window_enum_callback(hwnd: HWND, lparam: LPARAM) -> BOOL {
    unsafe {
        if IsWindowVisible(hwnd).as_bool() {
            let mut title = [0u16; 256];
            let len = GetWindowTextW(hwnd, &mut title);
            if len > 0 {
                let windows = &mut *(lparam.0 as *mut Vec<HWND>);
                windows.push(hwnd);
            }
        }
        BOOL(1) // Continue enumeration
    }
}
```

### Performance Considerations

- **Frame pool size**: Use 2-3 buffers for optimal performance
- **Pixel format**: B8G8R8A8UIntNormalized is most efficient
- **Zero-copy**: Access GPU textures directly when possible
- **Async processing**: Process frames asynchronously to avoid blocking
- **Resource cleanup**: Always close capture session and frame pool

### Limitations

- **Permission required**: User must grant permission (cannot bypass)
- **No background capture**: Application must be visible or have focus
- **DRM protection**: Cannot capture DRM-protected content
- **Multi-GPU**: May not work correctly with multiple GPUs (laptop iGPU + dGPU)
- **Remote Desktop**: Limited support for RDP sessions

### Error Handling

```rust
use windows::core::Error as WinRTError;

fn capture_error_handling() -> Result<(), String> {
    match capture_operation() {
        Ok(_) => Ok(()),
        Err(e) => {
            let error = e.downcast::<WinRTError>().unwrap();
            match error.code() {
                windows::core::HRESULT(0x80070005) => {
                    Err("Permission denied".to_string())
                }
                windows::core::HRESULT(0x887A0001) => {
                    Err("Device removed or reset".to_string())
                }
                _ => Err(format!("Capture failed: {}", error))
            }
        }
    }
}
```

---

## WASAPI (Windows Audio Session API)

### Overview

WASAPI (Windows Audio Session API) is a low-level audio API introduced in Windows Vista that provides high-performance, low-latency audio capture and playback. It is the recommended API for professional audio applications on Windows.

### Requirements

- **Minimum Windows Version**: Windows Vista
- **Recommended**: Windows 10+ for best performance
- **Runtime**: No additional runtime required (built into Windows)

### Key Features

- **Loopback capture**: Capture system audio (all applications)
- **Microphone capture**: Capture microphone input
- **Low latency**: < 10ms latency in exclusive mode
- **High quality**: 24-bit, 192kHz support
- **Multiple devices**: Enumerate and select audio devices
- **Volume control**: Control volume per device

### Rust Integration

Use the `windows` crate with the following features:

```toml
[dependencies]
windows = { version = "0.62", features = [
    "Media_Audio",
    "Media_Capture",
    "Media_Devices",
    "Win32_Media_Audio",
    "Win32_System_Com",
] }

# Alternative: Use dedicated WASAPI crate
wasapi = "0.11"
```

### Core Types

```rust
use windows::Win32::Media::Audio::{
    IMMDeviceEnumerator,
    IMMDevice,
    IMMDeviceCollection,
    IAudioClient,
    IAudioCaptureClient,
    AUDCLNT_SHAREMODE,
    WAVEFORMATEX,
};

use windows::Win32::System::Com::{
    CoInitializeEx,
    CoCreateInstance,
    CLSCTX_ALL,
};
```

### Device Enumeration

```rust
use windows::Win32::Media::Audio::{
    MMDeviceEnumerator,
    eCapture,
    eRender,
    DEVICE_STATE_ACTIVE,
};

// Initialize COM
unsafe { CoInitializeEx(None, windows::Win32::System::Com::COINIT_MULTITHREADED)? };

// Create device enumerator
let enumerator: IMMDeviceEnumerator = unsafe {
    CoCreateInstance(&MMDeviceEnumerator, None, CLSCTX_ALL)?
};

// Enumerate capture devices (microphones)
let collection = unsafe {
    enumerator.EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE)?
};

// Enumerate render devices (speakers for loopback)
let render_collection = unsafe {
    enumerator.EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE)?
};
```

### Loopback Capture (System Audio)

```rust
use windows::Win32::Media::Audio::{
    AUDCLNT_STREAMFLAGS_LOOPBACK,
    AUDCLNT_SHAREMODE_SHARED,
};

// Get default render device
let render_device = unsafe {
    enumerator.GetDefaultAudioEndpoint(eRender, eConsole)?
};

// Get audio client
let audio_client: IAudioClient = unsafe {
    render_device.Activate(CLSCTX_ALL, None)?
};

// Initialize for loopback capture
unsafe {
    audio_client.Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        0, // buffer duration (0 for shared mode)
        0, // periodicity
        &wave_format,
        None,
    )?;
}

// Get capture client
let capture_client: IAudioCaptureClient = unsafe {
    audio_client.GetService()?
};

// Start capture
unsafe {
    audio_client.Start()?;
}

// Read audio data
let mut buffer_data = std::ptr::null_mut();
let mut num_frames_available = 0u32;

unsafe {
    capture_client.GetBuffer(
        &mut buffer_data,
        &mut num_frames_available,
        &mut flags,
        None,
        None,
    )?;
}

// Process audio data
// ...

// Release buffer
unsafe {
    capture_client.ReleaseBuffer(num_frames_available)?;
}
```

### Microphone Capture

```rust
// Get default capture device (microphone)
let capture_device = unsafe {
    enumerator.GetDefaultAudioEndpoint(eCapture, eConsole)?
};

// Get audio client
let audio_client: IAudioClient = unsafe {
    capture_device.Activate(CLSCTX_ALL, None)?
};

// Initialize for capture
unsafe {
    audio_client.Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0, // no special flags
        0,
        0,
        &wave_format,
        None,
    )?;
}

// Get capture client
let capture_client: IAudioCaptureClient = unsafe {
    audio_client.GetService()?
};

// Start capture
unsafe {
    audio_client.Start()?;
}

// Read audio data (same as loopback)
```

### Audio Mixing

```rust
struct AudioMixer {
    system_audio: AudioCapture,
    microphone: AudioCapture,
    app_captures: Vec<AudioCapture>,
    master_volume: f32,
}

impl AudioMixer {
    fn mix_audio(&mut self, output_buffer: &mut [f32]) {
        // Clear output buffer
        output_buffer.fill(0.0);
        
        // Mix system audio
        if let Some(system_buffer) = self.system_audio.get_buffer() {
            for (out, in) in output_buffer.iter_mut().zip(system_buffer.iter()) {
                *out += in * self.system_volume;
            }
        }
        
        // Mix microphone
        if let Some(mic_buffer) = self.microphone.get_buffer() {
            for (out, in) in output_buffer.iter_mut().zip(mic_buffer.iter()) {
                *out += in * self.mic_volume;
            }
        }
        
        // Mix application audio
        for app in &self.app_captures {
            if let Some(app_buffer) = app.get_buffer() {
                for (out, in) in output_buffer.iter_mut().zip(app_buffer.iter()) {
                    *out += in * app.volume;
                }
            }
        }
        
        // Apply master volume
        for sample in output_buffer.iter_mut() {
            *sample *= self.master_volume;
        }
    }
}
```

### Performance Considerations

- **Buffer size**: Use 10-20ms buffers for low latency
- **Thread priority**: Set capture thread to high priority
- **Avoid blocking**: Process audio in separate thread
- **Sample rate**: Match system sample rate (usually 48kHz)
- **Bit depth**: 16-bit or 32-bit float (32-bit float preferred)

### Limitations

- **Loopback only**: Cannot capture specific application audio without WASAPI loopback
- **Exclusive mode**: Requires exclusive access (blocks other applications)
- **Latency**: Shared mode has higher latency than exclusive mode
- **Device changes**: Must handle device disconnect/reconnect

---

## NVENC (NVIDIA Encoder)

### Overview

NVENC is NVIDIA's hardware-accelerated video encoding technology built into NVIDIA GeForce, Quadro, and Tesla GPUs. It provides high-quality H.264 and H.265 encoding with minimal CPU usage.

### Requirements

- **GPU**: NVIDIA GeForce (GTX 600+), Quadro, or Tesla with NVENC support
- **Driver**: NVIDIA driver 334.89 or later
- **SDK**: NVIDIA Video Codec SDK runtime exposed by the installed driver
- **Runtime**: `nvEncodeAPI64.dll` is loaded dynamically; CUDA headers and the CUDA toolkit are not required by the current spike

### Key Features

- **H.264 encoding**: AVC (Advanced Video Coding)
- **H.265 encoding**: HEVC (High Efficiency Video Coding)
- **Low latency**: < 10ms encoding latency
- **High throughput**: Up to 8K60 encoding (RTX 4090)
- **Quality presets**: P1 (fastest) to P7 (highest quality)
- **Rate control**: CBR, VBR, CQP modes
- **B-frames**: Support for B-frames (better compression)

### Rust Integration

```toml
[dependencies]
# Current MoonLit spike
nvenc = "0.1.0"
windows = { version = "0.62", features = [
    "Win32_Graphics_Direct3D11",
    "Win32_Graphics_Dxgi",
    "Graphics_DirectX_Direct3D11",
] }

# Alternative: Use FFmpeg bindings for final container output
ffmpeg-next = "6.0"
```

The current implementation is in `src-tauri/native/windows-native`. It opens a
D3D11 NVENC session, uses the low-latency P4 preset and emits synchronous H.264
Annex B packets. The generic initialization example below describes the NVENC
concepts; it is not the exact safe wrapper API used by the spike.

### Encoder Initialization

```rust
use nvenc_sys::*;

// Initialize NVENC
let mut nvenc = NVENC::new()?;

// Set encoding parameters
let config = NVENCConfig {
    codec: NVCodec::H264,
    preset: NVPreset::P4, // Medium quality
    rate_control: NVRateControl::VBR,
    bitrate: 8_000_000, // 8 Mbps
    max_bitrate: 12_000_000, // 12 Mbps
    width: 1920,
    height: 1080,
    framerate: 60,
    gop_size: 120, // 2 seconds at 60 FPS
    b_frames: 2,
};

nvenc.initialize(config)?;
```

### Encoding Flow

```rust
// 1. Create input surface (GPU texture)
let input_surface = nvenc.create_surface(1920, 1080)?;

// 2. Copy frame data to input surface
input_surface.copy_from_cpu(frame_data)?;

// 3. Encode frame
let encoded_packet = nvenc.encode_frame(input_surface)?;

// 4. Get encoded data
if let Some(packet) = encoded_packet {
    let data = packet.data();
    let timestamp = packet.timestamp();
    
    // Write to file or stream
    output_file.write(data)?;
}

// 5. Flush encoder at end
let remaining_packets = nvenc.flush()?;
for packet in remaining_packets {
    output_file.write(packet.data())?;
}
```

### Quality Presets

| Preset | Speed | Quality | Use Case |
|--------|-------|---------|----------|
| P1 | Fastest | Lowest | Streaming, low-end GPU |
| P2 | Fast | Low | Streaming |
| P3 | Medium | Medium | General recording |
| P4 | Medium | Medium | General recording |
| P5 | Slow | High | High-quality recording |
| P6 | Slow | High | High-quality recording |
| P7 | Slowest | Highest | Archival quality |

### Rate Control Modes

- **CQP (Constant QP)**: Fixed quality, variable bitrate
- **VBR (Variable Bitrate)**: Variable quality, target bitrate
- **CBR (Constant Bitrate)**: Fixed bitrate, variable quality

### Performance

- **RTX 3060**: Up to 4K60 H.265 encoding
- **RTX 4090**: Up to 8K60 H.265 encoding
- **CPU usage**: < 5% (encoding offloaded to GPU)
- **Latency**: < 10ms per frame

### Limitations

- **GPU-specific**: Only works on NVIDIA GPUs
- **Driver dependency**: Requires NVIDIA driver
- **Session limit**: Limited concurrent encoding sessions (varies by GPU)
- **Quality**: Not as good as software encoding at same bitrate

---

## AMF (Advanced Media Framework)

### Overview

AMF is AMD's hardware-accelerated media framework for encoding and decoding video on AMD Radeon GPUs. It provides H.264 and H.265 encoding with low CPU usage.

### Requirements

- **GPU**: AMD Radeon (GCN 1.0+, RDNA, RDNA2, RDNA3)
- **Driver**: AMD Radeon Software 15.7 or later
- **SDK**: AMD AMF SDK (download from AMD developer portal)
- **Runtime**: No additional runtime required

### Key Features

- **H.264 encoding**: AVC (Advanced Video Coding)
- **H.265 encoding**: HEVC (High Efficiency Video Coding)
- **Low latency**: < 15ms encoding latency
- **High throughput**: Up to 4K60 encoding
- **Quality presets**: Speed, Balanced, Quality
- **Rate control**: CBR, VBR, CQP modes

### Rust Integration

```toml
[dependencies]
# Use AMF bindings (if available)
# Otherwise, use raw FFI

windows = { version = "0.62", features = [
    "Win32_Graphics_Direct3D11",
    "Win32_Graphics_Dxgi",
] }
```

### Encoder Initialization

```rust
use amf_sys::*;

// Initialize AMF
let context = AMFContext::new()?;
context.init_direct3d11(device)?;

// Create encoder
let encoder = context.create_component("AMFVideoEncoderVCE_AVC")?;

// Set properties
encoder.set_property("Usage", AMF_USAGE_TRANSCODING)?;
encoder.set_property("QualityPreset", AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY)?;
encoder.set_property("RateControlMethod", AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_VARIABLE_BITRATE)?;
encoder.set_property("TargetBitrate", 8_000_000)?;
encoder.set_property("MaxBitrate", 12_000_000)?;
encoder.set_property("FrameSize", AMFSize::new(1920, 1080))?;
encoder.set_property("FrameRate", AMFRate::new(60, 1))?;

// Initialize encoder
encoder.init(AMF_SURFACE_BGRA, 1920, 1080)?;
```

### Encoding Flow

```rust
// 1. Create input surface
let surface = context.create_surface(1920, 1080, AMF_SURFACE_BGRA)?;

// 2. Copy frame data
surface.copy_from_cpu(frame_data)?;

// 3. Submit frame for encoding
encoder.submit_input(surface)?;

// 4. Get encoded data
let packet = encoder.query_output()?;

if let Some(packet) = packet {
    let data = packet.data();
    output_file.write(data)?;
}
```

### Performance

- **Radeon RX 6000**: Up to 4K60 H.265 encoding
- **Radeon RX 7000**: Up to 8K60 H.265 encoding
- **CPU usage**: < 5%
- **Latency**: < 15ms per frame

---

## Intel Quick Sync Video

### Overview

Intel Quick Sync Video is Intel's hardware-accelerated video encoding/decoding technology built into Intel HD Graphics, Intel UHD Graphics, and Intel Iris Xe Graphics.

### Requirements

- **CPU**: Intel Core (2nd Gen+) with integrated graphics
- **GPU**: Intel HD Graphics, UHD Graphics, or Iris Xe
- **Driver**: Intel Graphics Driver 15.33 or later
- **SDK**: Intel Media SDK (included with Intel oneAPI)
- **Runtime**: No additional runtime required

### Key Features

- **H.264 encoding**: AVC (Advanced Video Coding)
- **H.265 encoding**: HEVC (High Efficiency Video Coding) - 6th Gen+
- **Low latency**: < 20ms encoding latency
- **High throughput**: Up to 4K60 encoding (11th Gen+)
- **Quality presets**: Speed, Balanced, Quality
- **Rate control**: CBR, VBR, CQP modes

### Rust Integration

```toml
[dependencies]
# Use Intel Media SDK bindings (if available)
# Otherwise, use MediaFoundation with Intel encoder

windows = { version = "0.62", features = [
    "Media_MediaProperties",
    "Media_Transcoding",
] }
```

### Performance

- **Intel UHD 630**: Up to 4K30 H.264 encoding
- **Intel Iris Xe**: Up to 4K60 H.265 encoding
- **CPU usage**: < 5%
- **Latency**: < 20ms per frame

---

## Win32 Hotkey API

### Overview

The Win32 Hotkey API allows applications to register global hotkeys that work system-wide, even when the application is not in focus.

### Requirements

- **Minimum Windows Version**: Windows XP
- **Runtime**: No additional runtime required

### Key Features

- **Global hotkeys**: Work in all applications
- **Modifier support**: Ctrl, Alt, Shift, Win modifiers
- **Unique IDs**: Each hotkey has a unique identifier
- **Message-based**: Hotkey events sent to application window

### Rust Integration

```toml
[dependencies]
windows = { version = "0.62", features = [
    "Win32_UI_WindowsAndMessaging",
    "Win32_UI_Input_KeyboardAndMouse",
] }
```

### Hotkey Registration

```rust
use windows::Win32::UI::WindowsAndMessaging::{
    RegisterHotKey,
    UnregisterHotKey,
    MOD_CONTROL,
    MOD_ALT,
    MOD_SHIFT,
    MOD_WIN,
};

use windows::Win32::UI::Input::KeyboardAndMouse::{
    VIRTUAL_KEY,
    VK_F8,
};

// Register hotkey: Ctrl+Shift+F8
let hotkey_id = 1;
let success = unsafe {
    RegisterHotKey(
        hwnd,
        hotkey_id,
        MOD_CONTROL | MOD_SHIFT,
        VK_F8.0 as u32,
    )
};

if !success.as_bool() {
    let error = windows::core::Error::from_win32();
    return Err(format!("Failed to register hotkey: {}", error));
}
```

### Hotkey Message Handling

```rust
use windows::Win32::UI::WindowsAndMessaging::{
    GetMessageW,
    WM_HOTKEY,
    MSG,
};

// Message loop
let mut msg = MSG::default();
while unsafe { GetMessageW(&mut msg, None, 0, 0) }.as_bool() {
    if msg.message == WM_HOTKEY {
        let hotkey_id = msg.wParam.0 as i32;
        
        match hotkey_id {
            1 => {
                // Handle Ctrl+Shift+F8
                save_clip()?;
            }
            2 => {
                // Handle another hotkey
                toggle_recording()?;
            }
            _ => {}
        }
    }
    
    unsafe {
        windows::Win32::UI::WindowsAndMessaging::TranslateMessage(&msg);
        windows::Win32::UI::WindowsAndMessaging::DispatchMessageW(&msg);
    }
}
```

### Unregister Hotkey

```rust
// Unregister hotkey when done
unsafe {
    UnregisterHotKey(hwnd, hotkey_id)?;
}
```

### Common Virtual Key Codes

| Key | Code | Key | Code |
|-----|------|-----|------|
| F1 | 0x70 | F2 | 0x71 |
| F3 | 0x72 | F4 | 0x73 |
| F5 | 0x74 | F6 | 0x75 |
| F7 | 0x76 | F8 | 0x77 |
| F9 | 0x78 | F10 | 0x79 |
| F11 | 0x7A | F12 | 0x7B |

### Limitations

- **System-wide**: Hotkeys work globally (can conflict with other apps)
- **No detection**: Cannot detect if hotkey is already registered
- **Message loop**: Requires message loop to receive hotkey events
- **Modifier limit**: Limited to Ctrl, Alt, Shift, Win modifiers

---

## Windows Toast Notifications

### Overview

Windows Toast Notifications are modern notifications introduced in Windows 8 that provide rich, interactive notifications with support for images, buttons, and actions.

### Requirements

- **Minimum Windows Version**: Windows 8
- **Recommended**: Windows 10+ for best features
- **Runtime**: No additional runtime required

### Key Features

- **Rich content**: Text, images, buttons
- **Actions**: Click actions, button actions
- **Persistence**: Notifications persist in Action Center
- **Scheduling**: Schedule notifications for future
- **Customization**: Custom sounds, durations

### Rust Integration

```toml
[dependencies]
windows = { version = "0.62", features = [
    "UI_Notifications",
    "Data_Xml_Dom",
    "Foundation",
] }
```

### Creating Toast Notification

```rust
use windows::UI::Notifications::{
    ToastNotificationManager,
    ToastNotification,
};

use windows::Data::Xml::Dom::XmlDocument;

// Create XML template
let xml = r#"
<toast>
    <visual>
        <binding template="ToastGeneric">
            <text>MoonLit</text>
            <text>Clip saved successfully!</text>
        </binding>
    </visual>
</toast>
"#;

let doc = XmlDocument::new()?;
doc.LoadXml(xml)?;

// Create toast notification
let toast = ToastNotification::CreateToastNotification(&doc)?;

// Show notification
let notifier = ToastNotificationManager::CreateToastNotifierWithId("com.souriscg.moonlit")?;
notifier.Show(&toast)?;
```

### Notification with Buttons

```rust
let xml = r#"
<toast>
    <visual>
        <binding template="ToastGeneric">
            <text>MoonLit</text>
            <text>Clip saved: clip_2026-07-23_14-30-45.mp4</text>
        </binding>
    </visual>
    <actions>
        <action content="Open" arguments="open_clip" />
        <action content="Share" arguments="share_clip" />
        <action content="Delete" arguments="delete_clip" />
    </actions>
</toast>
"#;
```

### Limitations

- **UWP restrictions**: Some features require UWP app
- **Action Center**: Notifications persist in Action Center
- **User settings**: User can disable notifications
- **Do Not Disturb**: Respects system "Do Not Disturb" mode

---

## Process and Window Enumeration

### Overview

Windows provides APIs to enumerate running processes and windows, which is useful for game detection and window capture.

### Process Enumeration

```rust
use windows::Win32::System::Threading::{
    CreateToolhelp32Snapshot,
    Process32First,
    Process32Next,
    PROCESSENTRY32,
    TH32CS_SNAPPROCESS,
};

let snapshot = unsafe { CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)? };

let mut entry = PROCESSENTRY32 {
    dwSize: std::mem::size_of::<PROCESSENTRY32>() as u32,
    ..Default::default()
};

if unsafe { Process32First(snapshot, &mut entry) }.as_bool() {
    loop {
        let process_name = String::from_utf16_lossy(&entry.szExeFile);
        let process_id = entry.th32ProcessID;
        
        // Check if this is a game
        if is_known_game(&process_name) {
            println!("Found game: {} (PID: {})", process_name, process_id);
        }
        
        if !unsafe { Process32Next(snapshot, &mut entry) }.as_bool() {
            break;
        }
    }
}

unsafe { windows::Win32::Foundation::CloseHandle(snapshot)? };
```

### Window Enumeration

```rust
use windows::Win32::UI::WindowsAndMessaging::{
    EnumWindows,
    GetWindowTextW,
    GetWindowThreadProcessId,
    IsWindowVisible,
    GetWindowRect,
    HWND,
    RECT,
};

unsafe {
    EnumWindows(Some(enum_windows_callback), LPARAM(&mut windows as _))?;
}

extern "system" fn enum_windows_callback(hwnd: HWND, lparam: LPARAM) -> BOOL {
    unsafe {
        if IsWindowVisible(hwnd).as_bool() {
            let mut title = [0u16; 256];
            let len = GetWindowTextW(hwnd, &mut title);
            
            if len > 0 {
                let title = String::from_utf16_lossy(&title[..len as usize]);
                
                let mut process_id = 0u32;
                GetWindowThreadProcessId(hwnd, Some(&mut process_id));
                
                let mut rect = RECT::default();
                GetWindowRect(hwnd, &mut rect);
                
                let windows = &mut *(lparam.0 as *mut Vec<WindowInfo>);
                windows.push(WindowInfo {
                    hwnd,
                    title,
                    process_id,
                    rect,
                });
            }
        }
        BOOL(1)
    }
}
```

---

## References

- [Windows.Graphics.Capture Documentation](https://docs.microsoft.com/windows/win32/direct3d11/windows-graphics-capture)
- [WASAPI Documentation](https://docs.microsoft.com/windows/win32/coreaudio/wasapi)
- [NVENC SDK](https://developer.nvidia.com/nvidia-video-codec-sdk)
- [AMD AMF](https://gpuopen.com/advanced-media-framework/)
- [Intel Quick Sync](https://software.intel.com/content/www/us/en/develop/documentation/video-tutorial/getting-started-with-intel-quick-sync-video.html)
- [Win32 API Documentation](https://docs.microsoft.com/windows/win32/api/)
- [Rust Windows Crate](https://github.com/microsoft/windows-rs)

---

## Notes

- All code examples are for reference and may require adjustments for production use
- Error handling should be comprehensive in production code
- Always check for API availability before using features
- Test on multiple Windows versions and hardware configurations
- Consider fallbacks when hardware acceleration is not available
