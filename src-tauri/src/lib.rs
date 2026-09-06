// MoonLit Phase 1 — tray + global F9 + close-to-hide.
// No capture / DB / editor logic here (later phases).

use std::sync::Mutex;
use tauri::{
    menu::{Menu, MenuItem},
    tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent},
    Emitter, Manager, WindowEvent,
};

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
        .plugin(tauri_plugin_sql::Builder::new().build())
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
                        let _ = app.emit(
                            "moonlit://clip-hotkey",
                            serde_json::json!({
                                "shortcut": shortcut.to_string(),
                                "pressed_at": now.to_string(),
                            }),
                        );
                        use tauri_plugin_notification::NotificationExt;
                        let _ = app
                            .notification()
                            .builder()
                            .title("MoonLit")
                            .body(format!("Hotkey {} pressed (Phase 1 test)", shortcut))
                            .show();
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
            Ok(())
        })
        .on_window_event(|window, event| {
            // Minimize-to-tray: hide instead of closing.
            if let WindowEvent::CloseRequested { api, .. } = event {
                api.prevent_close();
                let _ = window.hide();
            }
        })
        .invoke_handler(tauri::generate_handler![greet, get_hotkey])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
