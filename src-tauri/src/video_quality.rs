//! Video quality ladder (OS-free data + math).
//! Bitrates: Medal's official recommended table (CBR). NVENC HQ recipe:
//! old-MoonLit advanced table (CBR + P7 + HQ + AQ + BF2 + keyint 2s),
//! validated live against our bundled GSR (all keys accepted, bitrate on target).

/// Ladder heights offered in UI. 0 = source resolution (no -s flag).
pub const HEIGHTS: [u32; 4] = [360, 720, 1080, 1440];

/// CBR kbps per (output height, codec). Medal parity (1080p h264 = 20M,
/// same figure as the old-MoonLit advanced table).
pub fn bitrate_kbps(height: u32, codec: &str) -> u32 {
    match (height, codec) {
        (360, _) => 3000,
        (720, "h264") => 10000,
        (720, _) => 7000,
        (1080, "h264") => 20000,
        (1080, "hevc") => 12000,
        (1080, _) => 8000,
        (1440, "h264") => 25000,
        (1440, "hevc") => 20000,
        (1440, _) => 15000,
        (_, "h264") => 20000,
        (_, "hevc") => 12000,
        (_, _) => 8000,
    }
}

/// GSR `-s` value for ladder heights (16:9 box, kept aspect). None = original.
pub fn scale_arg(height: u32) -> Option<String> {
    match height {
        360 => Some("640x360".to_string()),
        720 => Some("1280x720".to_string()),
        1080 => Some("1920x1080".to_string()),
        1440 => Some("2560x1440".to_string()),
        _ => None,
    }
}

/// Exact NVENC HQ `-ffmpeg-video-opts` (old-MoonLit table). Dashes verified
/// live: GSR accepts every key, saves clean, bitrate lands on target.
/// Apply ONLY on NVIDIA + h264/hevc (meaningless/invalid elsewhere).
pub fn nvenc_hq_opts() -> &'static str {
    "preset=p7;tune=hq;profile=high;bf=2;spatial-aq=1;multipass=disabled"
}

/// Exact RAM/VRAM-ring megabytes for N seconds at a CBR bitrate.
pub fn ring_mb(bitrate_kbps: u32, seconds: u32) -> u32 {
    ((bitrate_kbps as u64 * seconds as u64) / 8 / 1000) as u32
}
