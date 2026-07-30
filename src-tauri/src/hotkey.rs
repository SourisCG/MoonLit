use std::thread::{self, JoinHandle};

use tauri::{AppHandle, Emitter};

pub struct HotkeyState {
    join: Option<JoinHandle<()>>,
}

impl HotkeyState {
    pub fn start(app: AppHandle) -> Self {
        #[cfg(target_os = "windows")]
        {
            use global_hotkey::hotkey::{Code, HotKey, Modifiers};
            use global_hotkey::GlobalHotKeyManager;

            let manager = GlobalHotKeyManager::new().ok();
            let hotkey = HotKey::new(Some(Modifiers::empty()), Code::F8);
            let registered = manager
                .as_ref()
                .is_some_and(|manager| manager.register(hotkey).is_ok());
            if let Some(manager) = manager {
                std::mem::forget(manager);
            }
            if !registered {
                let _ = app.emit(
                    "moonlit://hotkey",
                    serde_json::json!({"type":"registrationFailed","key":"F8"}),
                );
                return Self { join: None };
            }
            Self {
                join: Some(
                    thread::Builder::new()
                        .name("moonlit-hotkey".to_string())
                        .spawn(move || windows_loop(app, hotkey.id()))
                        .expect("hotkey thread"),
                ),
            }
        }
        #[cfg(not(target_os = "windows"))]
        {
            let _ = app;
            Self { join: None }
        }
    }
}

impl Drop for HotkeyState {
    fn drop(&mut self) {
        // The message thread is owned by the process and exits with Tauri.
        // Keeping it detached avoids calling Win32 APIs during shutdown.
        let _ = self.join.take();
    }
}

#[cfg(target_os = "windows")]
fn windows_loop(app: AppHandle, hotkey_id: u32) {
    use global_hotkey::GlobalHotKeyEvent;

    let _ = app.emit(
        "moonlit://hotkey",
        serde_json::json!({"type":"registered","key":"F8"}),
    );
    let receiver = GlobalHotKeyEvent::receiver();
    while let Ok(event) = receiver.recv() {
        if event.id() == hotkey_id {
            let _ = app.emit(
                "moonlit://hotkey",
                serde_json::json!({"type":"saveClip","key":"F8"}),
            );
        }
    }
}
