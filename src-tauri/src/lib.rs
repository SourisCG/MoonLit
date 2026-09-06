// MoonLit Phase 3 — tray + F9 + persistence + replay capture.
// Detection / editor logic lands in later phases.

use std::sync::Mutex;
use tauri::{
    menu::{Menu, MenuItem},
    tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent},
    Manager, WindowEvent,
};

mod commands;
mod cue;
mod editor;
mod os;
mod sidecar;
mod state;
mod storage;

/// Minimum gap between accepted hotkey presses (kills key auto-repeat doubles).
const HOTKEY_DEBOUNCE_MS: u128 = 400;

#[derive(Default)]
struct HotkeyState {
    last_emit_ms: Option<u128>,
}

fn now_ms() -> u128 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis())
        .unwrap_or(0)
}

#[tauri::command]
fn greet(name: &str) -> String {
    format!("Hello, {}! You've been greeted from Rust!", name)
}

#[tauri::command]
fn get_hotkey() -> String {
    "F9".to_string()
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .manage(Mutex::new(HotkeyState::default()))
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_notification::init())
        .plugin(tauri_plugin_clipboard_manager::init())
        .plugin(
            tauri_plugin_global_shortcut::Builder::new()
                .with_handler(|app, shortcut, event| {
                    use tauri_plugin_global_shortcut::ShortcutState;
                    if event.state == ShortcutState::Pressed {
                        let now = now_ms();
                        let accept = {
                            let state = app.state::<Mutex<HotkeyState>>();
                            let mut guard = state.lock().unwrap_or_else(|e| e.into_inner());
                            let ok = guard
                                .last_emit_ms
                                .map(|last| now.saturating_sub(last) >= HOTKEY_DEBOUNCE_MS)
                                .unwrap_or(true);
                            if ok {
                                guard.last_emit_ms = Some(now);
                            }
                            ok
                        };
                        if !accept {
                            return;
                        }
                        // Phase 3: counter event + real save pipeline (async).
                        let handle = app.clone();
                        let shortcut_str = shortcut.to_string();
                        tauri::async_runtime::spawn(async move {
                            commands::handle_hotkey(handle, shortcut_str, now.to_string()).await;
                        });
                    }
                })
                .build(),
        )
        .plugin(tauri_plugin_opener::init())
        .setup(|app| {
            // --- Tray ---
            let show = MenuItem::with_id(app, "show", "Show MoonLit", true, None::<&str>)?;
            let quit = MenuItem::with_id(app, "quit", "Quit", true, None::<&str>)?;
            let menu = Menu::with_items(app, &[&show, &quit])?;
            let icon = app
                .default_window_icon()
                .cloned()
                .expect("missing default window icon");
            TrayIconBuilder::with_id("main-tray")
                .icon(icon)
                .tooltip("MoonLit — replay buffer standby")
                .menu(&menu)
                .show_menu_on_left_click(false)
                .on_menu_event(|app, event| match event.id.as_ref() {
                    "show" => {
                        if let Some(w) = app.get_webview_window("main") {
                            let _ = w.show();
                            let _ = w.set_focus();
                        }
                    }
                    "quit" => app.exit(0),
                    _ => {}
                })
                .on_tray_icon_event(|tray, event| {
                    if let TrayIconEvent::Click {
                        button: MouseButton::Left,
                        button_state: MouseButtonState::Up,
                        ..
                    } = event
                    {
                        let app = tray.app_handle();
                        if let Some(w) = app.get_webview_window("main") {
                            let _ = w.show();
                            let _ = w.set_focus();
                        }
                    }
                })
                .build(app)?;

            // --- Global shortcut F9 ---
            use tauri_plugin_global_shortcut::GlobalShortcutExt;
            match app.global_shortcut().register("F9") {
                Ok(_) => eprintln!("[moonlit] global shortcut F9 registered"),
                Err(e) => eprintln!("[moonlit] could not register F9: {}", e),
            }

            // --- Persistence (Phase 2) ---
            let db = storage::DbState::open(app.handle()).map_err(std::io::Error::other)?;
            app.manage(db);
            app.manage(state::AppState::default());
            // One-time duration backfill for pre-probing rows (background).
            let handle = app.handle().clone();
            tauri::async_runtime::spawn(async move {
                commands::backfill_durations(&handle).await;
            });
            Ok(())
        })
        .on_window_event(|window, event| {
            // Minimize-to-tray: hide instead of closing.
            if let WindowEvent::CloseRequested { api, .. } = event {
                api.prevent_close();
                let _ = window.hide();
            }
        })
        .invoke_handler(tauri::generate_handler![
            greet,
            get_hotkey,
            commands::list_clips,
            commands::toggle_favorite,
            commands::delete_clip,
            commands::resolve_clip_src,
            commands::get_settings,
            commands::set_setting,
            commands::list_custom_apps,
            commands::register_app,
            commands::delete_app,
            commands::secret_store,
            commands::secret_get,
            commands::secret_delete,
            commands::start_buffer,
            commands::stop_buffer,
            commands::engine_status,
            commands::save_clip_now,
            commands::audio_levels,
            commands::set_track_gain,
            commands::set_track_mute,
            commands::gsr_info,
            commands::fix_gsr_caps,
            commands::list_audio_devices,
            commands::preview_track,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
