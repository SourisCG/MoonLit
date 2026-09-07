//! Windows engine: WGC frame acquisition → ffmpeg encode → RAM rings.
//!
//! Replay semantics mirror Linux GSR (`-r` ring + remux on hotkey): encoded
//! video packets (MPEG-TS) and PCM audio stems live in RAM only — zero disk
//! writes while idle. `save_clip` cuts the window, muxes 3×AAC and delivers
//! the `.mp4`. No DLL injection anywhere (WGC is the same OS API Xbox Game
//! Bar uses — anti-cheat safe). OS floor: Windows 10 1903+.

use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::sync::{
    atomic::{AtomicBool, Ordering},
    mpsc, Arc, Mutex,
};
use std::time::Duration;

use tokio::process::{Child, Command};
use windows_capture::capture::{CaptureControl, Context, GraphicsCaptureApiHandler};
use windows_capture::frame::Frame;
use windows_capture::graphics_capture_api::InternalCaptureControl;
use windows_capture::settings::{
    ColorFormat, CursorCaptureSettings, DirtyRegionSettings, DrawBorderSettings,
    MinimumUpdateIntervalSettings, SecondaryWindowSettings, Settings,
};

use super::super::{CaptureConfig, CaptureEngine, SavePlan};
use super::audio::AudioCapture;
use super::video;

/// Frames in flight between the WGC callback and the ffmpeg writer.
/// Small: backpressure means the encoder is behind, and a replay buffer
/// prefers dropping a frame over growing latency.
const FRAME_QUEUE: usize = 4;
/// Extra seconds kept beyond the configured buffer (cut margin + headroom).
const RING_MARGIN_SECS: u64 = 8;
/// How long `start_buffer` waits for the encoder to prove it is alive.
const ENCODER_PROOF_MS: u64 = 800;

// ---------------------------------------------------------------------------
// Pure helpers (unit-tested)
// ---------------------------------------------------------------------------

/// ffmpeg args for the live encoder **after** `-c:v <enc>`. CBR ladder +
/// 2 s GOP everywhere (Medal parity); HQ knobs only where valid
/// (NVIDIA + h264/hevc, same rule as GSR `nvenc_opts`).
pub fn live_encoder_args(
    enc_name: &str,
    codec: &str,
    bitrate_kbps: u32,
    fps: u32,
    nvenc_hq: bool,
) -> Vec<String> {
    let mut out: Vec<String> = Vec::new();
    let mut flag = |k: &str, v: &str| {
        out.push(k.to_string());
        out.push(v.to_string());
    };
    if enc_name.ends_with("_nvenc") {
        if nvenc_hq {
            flag("-preset", "p7");
            flag("-tune", "hq");
            flag("-profile:v", if codec == "hevc" { "main" } else { "high" });
            flag("-bf", "2");
        }
        flag("-rc", "cbr");
    } else if enc_name.ends_with("_amf") {
        flag("-rc", "cbr");
        flag("-quality", "quality");
    } else if enc_name.ends_with("_qsv") {
        // Live constraint (unlike offline save-scale): stay `fast` so a
        // 60 fps game never stalls the encoder.
        flag("-preset", "fast");
    } else if enc_name == "libx264" {
        flag("-preset", "veryfast");
        flag("-tune", "zerolatency");
        flag("-profile:v", "high");
        flag("-bf", "2");
    }
    let gop = (fps.max(1) * 2).to_string();
    flag("-b:v", &format!("{bitrate_kbps}k"));
    flag("-maxrate", &format!("{bitrate_kbps}k"));
    flag("-bufsize", &format!("{bitrate_kbps}k"));
    flag("-g", &gop);
    out
}

/// Clip file name in the GSR style (`replay_YYYY-MM-DD_HH-MM-SS.mp4`) so
/// Windows clips sort and read exactly like Linux ones.
fn replay_filename() -> String {
    use windows::Win32::System::SystemInformation::GetLocalTime;
    let t = unsafe { GetLocalTime() };
    format!(
        "replay_{:04}-{:02}-{:02}_{:02}-{:02}-{:02}.mp4",
        t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond
    )
}

/// Resync a TS window: first offset where `0x47` starts an aligned pair.
/// Returns the whole window when no sync is found (caller still delivers).
fn cut_ts_window(ring: &[u8], keep_bytes: usize) -> &[u8] {
    let start = ring.len().saturating_sub(keep_bytes);
    let win = &ring[start..];
    let scan = win.len().min(188 * 8);
    for i in 0..scan {
        if win[i] == 0x47 && (i + 188 >= win.len() || win[i + 188] == 0x47) {
            return &win[i..];
        }
    }
    win
}

/// Minimal PCM-16 WAV writer (stereo 48 kHz). No extra crate needed.
fn write_wav(path: &Path, samples: &[f32]) -> std::io::Result<()> {
    let mut f = std::fs::File::create(path)?;
    let data_bytes = (samples.len() * 2) as u32;
    let mut hdr = [0u8; 44];
    hdr[0..4].copy_from_slice(b"RIFF");
    hdr[4..8].copy_from_slice(&(36 + data_bytes).to_le_bytes());
    hdr[8..12].copy_from_slice(b"WAVE");
    hdr[12..16].copy_from_slice(b"fmt ");
    hdr[16..20].copy_from_slice(&16u32.to_le_bytes());
    hdr[20..22].copy_from_slice(&1u16.to_le_bytes()); // PCM
    hdr[22..24].copy_from_slice(&2u16.to_le_bytes()); // stereo
    hdr[24..28].copy_from_slice(&48_000u32.to_le_bytes());
    hdr[28..32].copy_from_slice(&(48_000u32 * 2 * 2).to_le_bytes());
    hdr[32..34].copy_from_slice(&4u16.to_le_bytes());
    hdr[34..36].copy_from_slice(&16u16.to_le_bytes());
    hdr[36..40].copy_from_slice(b"data");
    hdr[40..44].copy_from_slice(&data_bytes.to_le_bytes());
    f.write_all(&hdr)?;
    for &s in samples {
        let v = ((s.clamp(-1.0, 1.0) * 32767.0) as i16).to_le_bytes();
        f.write_all(&v)?;
    }
    Ok(())
}

// ---------------------------------------------------------------------------
// WGC plumbing
// ---------------------------------------------------------------------------

struct EngineShared {
    tx: Mutex<mpsc::SyncSender<Vec<u8>>>,
    halt: AtomicBool,
    width: u32,
    height: u32,
    size_warned: AtomicBool,
}

struct FrameHandler {
    shared: Arc<EngineShared>,
    scratch: Vec<u8>,
}

impl GraphicsCaptureApiHandler for FrameHandler {
    type Flags = Arc<EngineShared>;
    type Error = String;

    fn new(ctx: Context<Self::Flags>) -> Result<Self, Self::Error> {
        Ok(Self {
            shared: ctx.flags,
            scratch: Vec::new(),
        })
    }

    fn on_frame_arrived(
        &mut self,
        frame: &mut Frame,
        _control: InternalCaptureControl,
    ) -> Result<(), Self::Error> {
        if self.shared.halt.load(Ordering::Relaxed) {
            return Ok(());
        }
        // A mid-run resize would corrupt the rawvideo stream (fixed `-s`);
        // drop those frames loudly instead of poisoning the ring.
        if frame.width() != self.shared.width || frame.height() != self.shared.height {
            if !self.shared.size_warned.swap(true, Ordering::Relaxed) {
                eprintln!(
                    "[moonlit] monitor size changed mid-buffer ({}x{}), dropping frames until restart",
                    frame.width(),
                    frame.height()
                );
            }
            return Ok(());
        }
        match frame.buffer() {
            Ok(buf) => {
                let bytes = buf.as_nopadding_buffer(&mut self.scratch);
                if let Ok(tx) = self.shared.tx.lock() {
                    // Full queue = encoder behind: drop, never block capture.
                    let _ = tx.try_send(bytes.to_vec());
                }
            }
            Err(e) => eprintln!("[moonlit] frame buffer failed: {e}"),
        }
        Ok(())
    }

    fn on_closed(&mut self) -> Result<(), Self::Error> {
        eprintln!("[moonlit] WGC session closed by the OS");
        Ok(())
    }
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

pub struct WindowsCaptureEngine {
    control: Option<CaptureControl<FrameHandler, String>>,
    video_child: Option<Child>,
    video_ring: Arc<Mutex<Vec<u8>>>,
    video_dead: Arc<AtomicBool>,
    ring_cap: usize,
    audio: Option<AudioCapture>,
    output_dir: PathBuf,
    audio_args: Vec<String>,
    save_plan: Option<SavePlan>,
    duration_secs: u32,
    bitrate_kbps: u32,
    fps: u32,
}

impl WindowsCaptureEngine {
    pub fn new() -> Self {
        Self {
            control: None,
            video_child: None,
            video_ring: Arc::new(Mutex::new(Vec::new())),
            video_dead: Arc::new(AtomicBool::new(false)),
            ring_cap: 0,
            audio: None,
            output_dir: PathBuf::new(),
            audio_args: Vec::new(),
            save_plan: None,
            duration_secs: 0,
            bitrate_kbps: 0,
            fps: 60,
        }
    }

    fn spawn_video_encoder(
        &self,
        ffmpeg: &Path,
        enc_name: &str,
        codec: &str,
        mw: u32,
        mh: u32,
        out_height: u32,
        bitrate_kbps: u32,
        fps: u32,
        nvenc_hq: bool,
    ) -> Result<(Child, mpsc::SyncSender<Vec<u8>>), String> {
        let vf = if out_height > 0 && out_height != mh {
            format!("scale=-2:{out_height},fps={fps},format=yuv420p")
        } else {
            format!("fps={fps},format=yuv420p")
        };
        let mut cmd = Command::new(ffmpeg);
        cmd.args([
            "-hide_banner",
            "-loglevel",
            "error",
            "-thread_queue_size",
            "32",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "bgra",
            "-s",
            &format!("{mw}x{mh}"),
            "-framerate",
            &fps.to_string(),
            "-i",
            "pipe:0",
            "-an",
            "-vf",
            &vf,
            "-c:v",
            enc_name,
        ]);
        cmd.args(live_encoder_args(enc_name, codec, bitrate_kbps, fps, nvenc_hq));
        cmd.args(["-f", "mpegts", "pipe:1"])
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .kill_on_drop(true);
        let mut child = cmd
            .spawn()
            .map_err(|e| format!("cannot launch video encoder ({}): {e}", ffmpeg.display()))?;
        // Hand the pipes to blocking std threads (the pump and the drain
        // must never stall the async runtime on 8 MB frame writes).
        let mut stdin: std::process::ChildStdin = child
            .stdin
            .take()
            .ok_or_else(|| "encoder stdin unavailable".to_string())?
            .into_owned_handle()
            .map_err(|e| format!("encoder stdin handoff failed: {e}"))?
            .into();
        let stdout: std::process::ChildStdout = child
            .stdout
            .take()
            .ok_or_else(|| "encoder stdout unavailable".to_string())?
            .into_owned_handle()
            .map_err(|e| format!("encoder stdout handoff failed: {e}"))?
            .into();
        // Frame pump: WGC callback -> ffmpeg stdin.
        let (tx, rx) = mpsc::sync_channel::<Vec<u8>>(FRAME_QUEUE);
        let dead = self.video_dead.clone();
        let halt_tx = dead.clone();
        std::thread::Builder::new()
            .name("moonlit-wgc-pump".into())
            .spawn(move || {
                for frame in rx {
                    if halt_tx.load(Ordering::Relaxed) {
                        break;
                    }
                    if stdin.write_all(&frame).is_err() {
                        dead.store(true, Ordering::Relaxed);
                        break;
                    }
                }
                let _ = stdin.flush();
            })
            .map_err(|e| format!("cannot spawn frame pump: {e}"))?;
        // TS drain: encoder stdout -> RAM ring.
        let ring = self.video_ring.clone();
        let cap = self.ring_cap;
        let dead2 = self.video_dead.clone();
        std::thread::Builder::new()
            .name("moonlit-ts-drain".into())
            .spawn(move || {
                let mut out = stdout;
                let mut buf = [0u8; 64 * 1024];
                loop {
                    match out.read(&mut buf) {
                        Ok(0) => break,
                        Ok(n) => {
                            if let Ok(mut guard) = ring.lock() {
                                guard.extend_from_slice(&buf[..n]);
                                if guard.len() > cap + 1024 * 1024 {
                                    let excess = guard.len() - cap;
                                    guard.drain(..excess);
                                }
                            }
                        }
                        Err(_) => break,
                    }
                }
                dead2.store(true, Ordering::Relaxed);
            })
            .map_err(|e| format!("cannot spawn TS drain: {e}"))?;
        Ok((child, tx))
    }

    /// Mux the cut TS window + 3 stems into the final clip. Track order =
    /// MIX, game, mic (Linux parity). Missing/empty stems become silence so
    /// the file always carries 3×AAC.
    async fn mux_clip(
        ffmpeg: &Path,
        cut_ts: &Path,
        mix: &[f32],
        game: &[f32],
        mic: &[f32],
        dest: &Path,
    ) -> Result<(), String> {
        let tmp = dest.with_extension("mux.tmp");
        let _ = tokio::fs::remove_file(&tmp).await;
        let stems = [("mix", mix), ("game", game), ("mic", mic)];
        let mut wav_paths = Vec::new();
        for (i, (name, samples)) in stems.iter().enumerate() {
            let p = tmp.with_extension(format!("{name}{i}.wav"));
            let owned = if samples.is_empty() {
                vec![0.0f32; 48_000 * 2 / 2] // 0.5 s silence placeholder
            } else {
                samples.to_vec()
            };
            tokio::task::spawn_blocking({
                let p = p.clone();
                move || write_wav(&p, &owned)
            })
            .await
            .map_err(|e| format!("wav task failed: {e}"))?
            .map_err(|e| format!("cannot write {name} wav: {e}"))?;
            wav_paths.push(p);
        }
        let status = Command::new(ffmpeg)
            .args(["-y", "-hide_banner", "-loglevel", "error"])
            .arg("-i")
            .arg(cut_ts)
            .arg("-i")
            .arg(&wav_paths[0])
            .arg("-i")
            .arg(&wav_paths[1])
            .arg("-i")
            .arg(&wav_paths[2])
            .args([
                "-map", "0:v", "-map", "1:a", "-map", "2:a", "-map", "3:a",
                "-c:v", "copy", "-c:a", "aac", "-b:a", "160k",
                "-metadata:s:a:0", "title=Mix",
                "-metadata:s:a:1", "title=Game",
                "-metadata:s:a:2", "title=Mic",
                "-shortest",
            ])
            .arg(dest)
            .status()
            .await
            .map_err(|e| format!("save mux failed: {e}"))?;
        for p in &wav_paths {
            let _ = tokio::fs::remove_file(p).await;
        }
        if !status.success() {
            return Err("save mux failed (ffmpeg)".into());
        }
        Ok(())
    }
}

impl CaptureEngine for WindowsCaptureEngine {
    async fn start_buffer(&mut self, config: CaptureConfig) -> Result<(), String> {
        if self.control.is_some() {
            return Err("recorder already running".into());
        }
        std::fs::create_dir_all(&config.output_dir)
            .map_err(|e| format!("cannot create clips dir: {e}"))?;
        let vendor = video::vendor(Path::new("")).await;
        let enc_name = video::capture_encoder_name(&vendor, &config.codec).ok_or_else(|| {
            format!(
                "codec '{}' cannot encode on '{}' (stale setting?)",
                config.codec, vendor
            )
        })?;
        let monitor = video::resolve_monitor(&config.source)
            .ok_or_else(|| "no monitor available (Windows 10 1903+ required)".to_string())?;
        let (mw, mh) = monitor
            .width()
            .and_then(|w| monitor.height().map(|h| (w, h)))
            .map_err(|_| "cannot determine monitor size".to_string())?;
        if mw == 0 || mh == 0 {
            return Err("cannot determine monitor size".to_string());
        }
        let fps = if config.fps == 30 { 30 } else { 60 };
        self.ring_cap = (((config.bitrate_kbps as usize)
            * ((config.duration_seconds as usize) + RING_MARGIN_SECS as usize))
            / 8)
            * 1024;
        self.ring_cap = self.ring_cap.max(16 * 1024 * 1024);
        self.video_dead.store(false, Ordering::Relaxed);
        self.video_ring.lock().map(|mut r| r.clear()).ok();

        let ffmpeg = video::capture_ffmpeg();
        let nvenc_hq = config.nvenc_opts.is_some();
        let (child, tx) = self.spawn_video_encoder(
            &ffmpeg,
            enc_name,
            &config.codec,
            mw,
            mh,
            config.out_height,
            config.bitrate_kbps,
            fps,
            nvenc_hq,
        )?;
        self.video_child = Some(child);
        // Prove the encoder is alive before committing to WGC: a bad combo
        // (stale codec, missing HW block) exits within milliseconds, and
        // its stderr names the cause.
        tokio::time::sleep(Duration::from_millis(ENCODER_PROOF_MS)).await;
        if let Some(child) = self.video_child.as_mut() {
            if let Ok(Some(status)) = child.try_wait() {
                let mut err = String::new();
                if let Some(stderr) = child.stderr.take() {
                    let mut out = stderr;
                    use tokio::io::AsyncReadExt as _;
                    let mut buf = Vec::new();
                    let _ = out.read_to_end(&mut buf).await;
                    err = String::from_utf8_lossy(&buf)
                        .lines()
                        .rev()
                        .take(5)
                        .collect::<Vec<_>>()
                        .join(" | ");
                }
                self.video_child = None;
                return Err(format!("video encoder exited ({status}): {err}"));
            }
        }
        // WGC session on its own thread (free-threaded control).
        let shared = Arc::new(EngineShared {
            tx: Mutex::new(tx),
            halt: AtomicBool::new(false),
            width: mw,
            height: mh,
            size_warned: AtomicBool::new(false),
        });
        let settings = Settings::new(
            monitor,
            CursorCaptureSettings::WithCursor,
            DrawBorderSettings::WithoutBorder,
            SecondaryWindowSettings::Default,
            MinimumUpdateIntervalSettings::Default,
            DirtyRegionSettings::Default,
            ColorFormat::Bgra8,
            shared,
        );
        let control = FrameHandler::start_free_threaded(settings)
            .map_err(|e| format!("WGC start failed: {e:?}"))?;
        self.control = Some(control);
        // Audio: loopback + mic. Unity gains here; the settings task applies
        // persisted gains right after start (same as Linux).
        match AudioCapture::start(
            &config.desktop_device,
            &config.mic_device,
            config.duration_seconds + RING_MARGIN_SECS as u32,
            100,
            100,
            false,
            false,
        ) {
            Ok(a) => {
                if a.live_count() < 2 {
                    eprintln!("[moonlit] audio degraded: {}/2 streams live", a.live_count());
                }
                self.audio = Some(a);
            }
            Err(e) => {
                eprintln!("[moonlit] audio failed, aborting start: {e}");
                let _ = self.stop_buffer().await;
                return Err(e);
            }
        }
        self.output_dir = config.output_dir;
        // Same synthetic shape as GSR `-a` args: mix first, then solos.
        self.audio_args = vec![
            format!("{}|{}", config.desktop_device, config.mic_device),
            config.desktop_device.clone(),
            config.mic_device.clone(),
        ];
        self.save_plan = if config.save_height > 0 {
            Some(SavePlan {
                height: config.save_height,
                bitrate_kbps: config.save_bitrate_kbps,
                codec: config.codec.clone(),
                fps,
                encoder: config.save_encoder,
            })
        } else {
            None
        };
        self.duration_secs = config.duration_seconds;
        self.bitrate_kbps = config.bitrate_kbps;
        self.fps = fps;
        eprintln!(
            "[moonlit] WGC buffer: {}x{}@{} {} ({}), audio {}/2",
            mw,
            mh,
            fps,
            enc_name,
            vendor,
            self.audio.as_ref().map(|a| a.live_count()).unwrap_or(0)
        );
        Ok(())
    }

    async fn save_clip(&mut self) -> Result<PathBuf, String> {
        if self.control.is_none() {
            return Err("recorder not running".into());
        }
        if self.video_dead.load(Ordering::Relaxed) {
            let len = self.video_ring.lock().map(|r| r.len()).unwrap_or(0);
            if len == 0 {
                return Err("video encoder died and no footage is buffered".into());
            }
        }
        let ffmpeg = video::capture_ffmpeg();
        // Video window: last (duration + 2 s) of TS, resynced.
        let keep = ((self.bitrate_kbps as usize * (self.duration_secs as usize + 2)) / 8) * 1024;
        let cut = {
            let ring = self.video_ring.lock().map_err(|_| "video ring poisoned".to_string())?;
            if ring.is_empty() {
                return Err("no video buffered yet".into());
            }
            cut_ts_window(&ring, keep.max(1024 * 1024)).to_vec()
        };
        let ts_path = std::env::temp_dir().join("moonlit-save-cut.ts");
        tokio::fs::write(&ts_path, &cut)
            .await
            .map_err(|e| format!("cannot stage video window: {e}"))?;
        // Audio tails: last `duration` seconds of each 48 kHz stereo stem.
        let tail = self.duration_secs as usize * 48_000 * 2;
        let snap = self
            .audio
            .as_ref()
            .map(|a| a.snapshot())
            .unwrap_or_default();
        let tail_of = |v: Vec<f32>| {
            if v.len() > tail {
                v[v.len() - tail..].to_vec()
            } else {
                v
            }
        };
        let dest = self.output_dir.join(replay_filename());
        let res = Self::mux_clip(
            &ffmpeg,
            &ts_path,
            &tail_of(snap.mix),
            &tail_of(snap.game),
            &tail_of(snap.mic),
            &dest,
        )
        .await;
        let _ = tokio::fs::remove_file(&ts_path).await;
        res?;
        Ok(dest)
    }

    async fn stop_buffer(&mut self) -> Result<(), String> {
        if let Some(control) = self.control.take() {
            // `stop` posts WM_QUIT to the capture thread and joins it.
            let _ = tokio::task::spawn_blocking(move || control.stop()).await;
        }
        if let Some(mut child) = self.video_child.take() {
            let _ = child.kill().await;
            let _ = tokio::time::timeout(Duration::from_secs(3), child.wait()).await;
        }
        // Dropping the capture unregisters shared audio (streams stop).
        self.audio = None;
        self.audio_args.clear();
        self.save_plan = None;
        self.video_ring.lock().map(|mut r| r.clear()).ok();
        Ok(())
    }

    fn backend_name(&self) -> &'static str {
        "windows-capture"
    }

    fn audio_args(&self) -> Vec<String> {
        self.audio_args.clone()
    }

    fn save_plan(&self) -> Option<SavePlan> {
        self.save_plan.clone()
    }
}

#[cfg(test)]
mod tests {
    use super::{cut_ts_window, live_encoder_args, replay_filename};
    use std::path::PathBuf;

    #[test]
    fn nvenc_hq_flags() {
        let a = live_encoder_args("h264_nvenc", "h264", 20000, 60, true);
        let s = a.join(" ");
        assert!(s.contains("-preset p7"), "{s}");
        assert!(s.contains("-tune hq"), "{s}");
        assert!(s.contains("-profile:v high"), "{s}");
        assert!(s.contains("-rc cbr"), "{s}");
        assert!(s.contains("-g 120"), "{s}");
    }

    #[test]
    fn nvenc_plain_no_hq() {
        // e.g. AV1 on NVIDIA: ladder + CBR, backend defaults otherwise.
        let a = live_encoder_args("av1_nvenc", "av1", 8000, 60, false);
        let s = a.join(" ");
        assert!(!s.contains("-preset"), "{s}");
        assert!(s.contains("-rc cbr"), "{s}");
    }

    #[test]
    fn x264_and_qsv_shapes() {
        let x = live_encoder_args("libx264", "x264", 20000, 30, false).join(" ");
        assert!(x.contains("-preset veryfast"), "{x}");
        assert!(x.contains("zerolatency"), "{x}");
        assert!(x.contains("-g 60"), "{x}");
        let q = live_encoder_args("h264_qsv", "h264", 20000, 60, false).join(" ");
        assert!(q.contains("-preset fast"), "{q}");
    }

    #[test]
    fn ts_cut_resyncs() {
        // Garbage head, then aligned 188 B packets starting with 0x47.
        let mut ring = vec![0xAAu8; 100];
        for _ in 0..4 {
            ring.push(0x47);
            ring.extend(std::iter::repeat(0x00).take(187));
        }
        let cut = cut_ts_window(&ring, 600);
        assert_eq!(cut[0], 0x47);
        assert_eq!(cut[188], 0x47);
        assert!(cut.len() <= 600);
        // A window with no sync at all is delivered whole, never empty.
        let plain = vec![0x11u8; 500];
        assert_eq!(cut_ts_window(&plain, 600).len(), 500);
    }

    #[test]
    fn filename_shape() {
        let n = replay_filename();
        assert!(n.starts_with("replay_"), "{n}");
        assert!(n.ends_with(".mp4"), "{n}");
        assert_eq!(n.len(), "replay_YYYY-MM-DD_HH-MM-SS.mp4".len(), "{n}");
        assert!(PathBuf::from(&n).extension().unwrap() == "mp4");
    }

    /// Live end-to-end on real hardware (ignored by default — needs WGC +
    /// NVENC + WASAPI, never runs on headless CI). Run explicitly:
    /// `cargo test --target x86_64-pc-windows-msvc live_buffer -- --ignored`.
    /// Buffers 6 s, saves, and asserts h264 video + 3×AAC.
    #[tokio::test]
    #[ignore]
    async fn live_buffer_and_save() {
        use super::super::super::{CaptureConfig, CaptureEngine, TranscodeEncoder};
        use std::time::Duration as StdDuration;

        let dir = std::env::temp_dir().join("moonlit-e2e");
        std::fs::create_dir_all(&dir).unwrap();
        let mut eng = super::WindowsCaptureEngine::new();
        eng.start_buffer(CaptureConfig {
            duration_seconds: 10,
            fps: 60,
            output_dir: dir,
            gsr_bin: None,
            desktop_device: String::new(),
            mic_device: String::new(),
            source: String::new(),
            codec: "h264".into(),
            out_height: 0,
            bitrate_kbps: 20000,
            save_height: 0,
            save_bitrate_kbps: 20000,
            save_encoder: Some(TranscodeEncoder::Nvenc),
            nvenc_opts: Some(crate::video_quality::nvenc_hq_opts("h264")),
        })
        .await
        .expect("start_buffer");
        tokio::time::sleep(StdDuration::from_secs(6)).await;
        let path = eng.save_clip().await.expect("save_clip");
        assert!(path.exists(), "clip missing");
        let size = std::fs::metadata(&path).unwrap().len();
        assert!(size > 50_000, "clip suspiciously small: {size} B");
        // Stream census via ffmpeg (no ffprobe in the static sidecar).
        let probe = tokio::process::Command::new(super::super::video::capture_ffmpeg())
            .args(["-hide_banner", "-i", &path.to_string_lossy()])
            .output()
            .await
            .expect("probe");
        let err = String::from_utf8_lossy(&probe.stderr);
        assert!(err.contains("Video: h264"), "no h264 video:\n{err}");
        assert_eq!(err.matches("Audio: aac").count(), 3, "want 3xAAC:\n{err}");
        eng.stop_buffer().await.expect("stop");
        let _ = std::fs::remove_file(&path);
    }
}
