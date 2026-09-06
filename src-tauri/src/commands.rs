//! Tauri IPC handlers (Phase 2: persistence; Phase 3: capture).

use std::collections::HashMap;
use tauri::{AppHandle, Emitter, Manager, State};

use crate::capture::{CaptureConfig, CaptureEngine};
use crate::state::{AppState, Engine};
use crate::storage::models::{ClipRecord, CustomApp, RegisterAppInput};
use crate::storage::{secrets, DbState};

#[tauri::command]
pub fn list_clips(db: State<'_, DbState>) -> Result<Vec<ClipRecord>, String> {
    db.list_clips()
}

#[tauri::command]
pub fn toggle_favorite(db: State<'_, DbState>, id: String) -> Result<bool, String> {
    db.toggle_favorite(&id)
}

#[tauri::command]
pub fn delete_clip(db: State<'_, DbState>, id: String) -> Result<(), String> {
    db.delete_clip(&id)
}

/// Absolute filesystem path for a clip file name (for <video> / convertFileSrc).
#[tauri::command]
pub fn resolve_clip_src(db: State<'_, DbState>, file_name: String) -> Result<String, String> {
    if file_name.contains("..") || file_name.starts_with('/') || file_name.starts_with('\\') {
        return Err("invalid file name".into());
    }
    let base = db.clips_dir()?;
    Ok(base.join(&file_name).to_string_lossy().to_string())
}

#[tauri::command]
pub fn get_settings(db: State<'_, DbState>) -> Result<HashMap<String, String>, String> {
    db.get_settings()
}

#[tauri::command]
pub fn set_setting(db: State<'_, DbState>, key: String, value: String) -> Result<(), String> {
    db.set_setting(&key, &value)
}

#[tauri::command]
pub fn list_custom_apps(db: State<'_, DbState>) -> Result<Vec<CustomApp>, String> {
    db.list_custom_apps()
}

#[tauri::command]
pub fn register_app(
    db: State<'_, DbState>,
    input: RegisterAppInput,
) -> Result<CustomApp, String> {
    db.register_app(input)
}

#[tauri::command]
pub fn delete_app(db: State<'_, DbState>, id: String) -> Result<(), String> {
    db.delete_app(&id)
}

#[tauri::command]
pub fn secret_store(alias: String, value: String) -> Result<(), String> {
    secrets::store_secret(&alias, &value)
}

#[tauri::command]
pub fn secret_get(alias: String) -> Result<String, String> {
    secrets::get_secret(&alias)
}

#[tauri::command]
pub fn secret_delete(alias: String) -> Result<(), String> {
    secrets::delete_secret(&alias)
}

// ---------------------------------------------------------------------------
// Capture (Phase 3)
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, serde::Serialize)]
pub struct EngineStatus {
    pub running: bool,
    pub backend: String,
}

#[cfg(target_os = "linux")]
fn backend_name() -> &'static str {
    "gpu-screen-recorder"
}
#[cfg(not(target_os = "linux"))]
fn backend_name() -> &'static str {
    "windows-capture (stub)"
}

fn buffer_seconds(db: &DbState) -> i64 {
    db.get_settings()
        .ok()
        .and_then(|s| s.get("buffer_seconds").and_then(|v| v.parse().ok()))
        .unwrap_or(30)
        .clamp(5, 300)
}

fn is_spanish(app: &AppHandle) -> bool {
    app.try_state::<DbState>()
        .and_then(|db| db.get_settings().ok())
        .and_then(|s| s.get("locale").cloned())
        .map(|l| l.starts_with("es"))
        .unwrap_or(true)
}

pub fn notify(app: &AppHandle, body_es: &str, body_en: &str) {
    use tauri_plugin_notification::NotificationExt;
    let body = if is_spanish(app) { body_es } else { body_en };
    let _ = app.notification().builder().title("MoonLit").body(body).show();
}

#[tauri::command]
pub async fn start_buffer(app: AppHandle) -> Result<EngineStatus, String> {
    {
        let st = app.state::<AppState>();
        if st.recorder.lock().await.is_some() {
            return Err("buffer already running".into());
        }
    }
    let db = app.state::<DbState>();
    let dir = db.clips_dir()?;
    let secs = buffer_seconds(&db) as u32;
    let (gsr_bin, source) = crate::sidecar::gsr_binary(&app)?;
    eprintln!("[moonlit] capture backend: {} ({})", gsr_bin.display(), source);
    let mut engine = Engine::new();
    engine
        .start_buffer(CaptureConfig {
            duration_seconds: secs,
            fps: 60,
            output_dir: dir,
            gsr_bin: Some(gsr_bin),
        })
        .await?;
    let status = EngineStatus {
        running: true,
        backend: engine.backend_name().to_string(),
    };
    {
        let st = app.state::<AppState>();
        *st.recorder.lock().await = Some(engine);
    }
    // Apply persisted per-track gains to the fresh GSR streams (best effort).
    let app2 = app.clone();
    tauri::async_runtime::spawn(async move {
        if let Err(e) = apply_saved_gains(&app2).await {
            eprintln!("[moonlit] initial gain apply failed: {e}");
        }
    });
    Ok(status)
}

#[tauri::command]
pub async fn stop_buffer(app: AppHandle) -> Result<EngineStatus, String> {
    let st = app.state::<AppState>();
    let mut guard = st.recorder.lock().await;
    if let Some(mut engine) = guard.take() {
        engine.stop_buffer().await?;
    }
    Ok(EngineStatus {
        running: false,
        backend: backend_name().to_string(),
    })
}

#[tauri::command]
pub async fn engine_status(app: AppHandle) -> Result<EngineStatus, String> {
    let st = app.state::<AppState>();
    let guard = st.recorder.lock().await;
    Ok(EngineStatus {
        running: guard.is_some(),
        backend: backend_name().to_string(),
    })
}

/// Full save pipeline: flush ring -> thumbnail -> DB index -> ding -> event.
pub(crate) async fn do_save_clip(app: &AppHandle) -> Result<ClipRecord, String> {
    let path = {
        let st = app.state::<AppState>();
        let mut guard = st.recorder.lock().await;
        let eng = guard
            .as_mut()
            .ok_or_else(|| "buffer not running".to_string())?;
        eng.save_clip().await?
    };
    let size = tokio::fs::metadata(&path)
        .await
        .map_err(|e| format!("cannot stat clip: {e}"))?
        .len() as i64;
    let db = app.state::<DbState>();
    let secs = buffer_seconds(&db);
    let stem = path
        .file_stem()
        .and_then(|s| s.to_str())
        .ok_or("bad clip file name")?;
    let file_name = path
        .file_name()
        .and_then(|s| s.to_str())
        .ok_or("bad clip file name")?
        .to_string();
    let thumb_name = format!("thumb_{stem}.jpg");
    let base = db.clips_dir()?;
    let ffmpeg = crate::editor::ffmpeg::resolve_ffmpeg(app)?;
    crate::editor::ffmpeg::make_thumbnail(&ffmpeg, &path, &base.join(&thumb_name)).await?;
    let clip = db.insert_clip(&file_name, &thumb_name, "Unknown", secs * 1000, size)?;
    crate::cue::play_ding();
    let _ = app.emit("moonlit://clip-saved", &clip);
    Ok(clip)
}

#[tauri::command]
pub async fn save_clip_now(app: AppHandle) -> Result<ClipRecord, String> {
    do_save_clip(&app).await
}

/// F9 entry point: counter event always fires; clip saves only when running.
pub(crate) async fn handle_hotkey(app: AppHandle, shortcut: String, pressed_at: String) {
    let _ = app.emit(
        "moonlit://clip-hotkey",
        serde_json::json!({ "shortcut": shortcut, "pressed_at": pressed_at }),
    );
    let st = app.state::<AppState>();
    let guard = st.recorder.lock().await;
    let running = guard.is_some();
    drop(guard);
    if !running {
        notify(&app, "Búfer detenido — pulsa Start para grabar", "Buffer stopped — press Start to record");
        return;
    }
    match do_save_clip(&app).await {
        Ok(clip) => notify(
            &app,
            &format!("Clip guardado: {}", clip.file_name),
            &format!("Clip saved: {}", clip.file_name),
        ),
        Err(e) => notify(&app, &format!("Error al guardar: {e}"), &format!("Save failed: {e}")),
    }
}

// ---------------------------------------------------------------------------
// Live capture gain + backend info (Phase 3)
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, serde::Serialize)]
pub struct TrackGains {
    pub game: u32,
    pub mic: u32,
    pub mute_game: bool,
    pub mute_mic: bool,
}

fn read_gains(app: &AppHandle) -> TrackGains {
    let map = app
        .try_state::<DbState>()
        .and_then(|db| db.get_settings().ok())
        .unwrap_or_default();
    let num = |k: &str, d: u32| {
        map.get(k)
            .and_then(|v| v.parse().ok())
            .unwrap_or(d)
            .clamp(0, 150)
    };
    let flag = |k: &str| map.get(k).map(|v| v == "1" || v == "true").unwrap_or(false);
    TrackGains {
        game: num("gain_game", 100),
        mic: num("gain_mic", 100),
        mute_game: flag("mute_game"),
        mute_mic: flag("mute_mic"),
    }
}

async fn apply_saved_gains(app: &AppHandle) -> Result<(), String> {
    let g = read_gains(app);
    #[cfg(target_os = "linux")]
    {
        crate::capture::audio::apply_gains(g.game, g.mic, g.mute_game, g.mute_mic).await
    }
    #[cfg(not(target_os = "linux"))]
    {
        let _ = g;
        Err("per-stream gain lands on the Windows trip".into())
    }
}

#[tauri::command]
pub async fn audio_levels(app: AppHandle) -> Result<TrackGains, String> {
    Ok(read_gains(&app))
}

fn check_track(track: &str) -> Result<(), String> {
    if track == "game" || track == "mic" {
        Ok(())
    } else {
        Err("track must be 'game' or 'mic'".into())
    }
}

#[tauri::command]
pub async fn set_track_gain(app: AppHandle, track: String, percent: u32) -> Result<TrackGains, String> {
    check_track(&track)?;
    let pct = percent.clamp(0, 150);
    {
        let db = app.state::<DbState>();
        db.set_setting(if track == "game" { "gain_game" } else { "gain_mic" }, &pct.to_string())?;
    }
    // Live-apply if the buffer is running; persisting alone is fine otherwise.
    let running = {
        let st = app.state::<AppState>();
        let guard = st.recorder.lock().await;
        let r = guard.is_some();
        drop(guard);
        r
    };
    if running {
        apply_saved_gains(&app).await?;
    }
    Ok(read_gains(&app))
}

#[tauri::command]
pub async fn set_track_mute(app: AppHandle, track: String, muted: bool) -> Result<TrackGains, String> {
    check_track(&track)?;
    {
        let db = app.state::<DbState>();
        db.set_setting(
            if track == "game" { "mute_game" } else { "mute_mic" },
            if muted { "1" } else { "0" },
        )?;
    }
    let running = {
        let st = app.state::<AppState>();
        let guard = st.recorder.lock().await;
        let r = guard.is_some();
        drop(guard);
        r
    };
    if running {
        apply_saved_gains(&app).await?;
    }
    Ok(read_gains(&app))
}

#[derive(Debug, Clone, serde::Serialize)]
pub struct GsrInfo {
    pub path: String,
    pub source: String,
    pub caps_ok: bool,
    pub present: bool,
}

#[tauri::command]
pub async fn gsr_info(app: AppHandle) -> Result<GsrInfo, String> {
    match crate::sidecar::gsr_binary(&app) {
        Ok((path, source)) => {
            let caps_ok = crate::sidecar::gsr_caps_ok(&path);
            Ok(GsrInfo {
                path: path.to_string_lossy().to_string(),
                source: source.to_string(),
                caps_ok,
                present: true,
            })
        }
        Err(_) => Ok(GsrInfo {
            path: String::new(),
            source: "missing".to_string(),
            caps_ok: false,
            present: false,
        }),
    }
}

/// One-click KMS permission fix (polkit dialog) for OUR bundled binary.
#[tauri::command]
pub async fn fix_gsr_caps(app: AppHandle) -> Result<(), String> {
    let (path, source) = crate::sidecar::gsr_binary(&app)?;
    if source != "bundled" {
        return Err("one-click fix applies to the bundled binary only".into());
    }
    crate::sidecar::fix_gsr_caps(&path).await
}
