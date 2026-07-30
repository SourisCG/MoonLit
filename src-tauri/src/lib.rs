#![cfg_attr(test, allow(dead_code))]

#[cfg(not(test))]
mod audio;
mod backends;
#[cfg(not(test))]
mod config;
#[cfg(not(test))]
mod doctor;
#[cfg(not(test))]
mod hotkey;
#[cfg(not(test))]
mod library;
#[cfg(not(test))]
mod media;
#[cfg(not(test))]
mod recorder;
pub(crate) mod replay;
#[cfg(target_os = "windows")]
mod sidecar;
#[cfg(not(test))]
mod state;
#[cfg(not(test))]
mod storage;
mod traits;

#[cfg(not(test))]
use tauri::menu::{Menu, MenuItem};
#[cfg(not(test))]
use tauri::tray::TrayIconBuilder;
#[cfg(not(test))]
use tauri::Emitter;
#[cfg(not(test))]
use tauri::Manager;
#[cfg(not(test))]
use tauri::WindowEvent;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
#[cfg(not(test))]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_notification::init())
        .setup(|app| {
            let app_data_dir = app
                .path()
                .app_data_dir()
                .map_err(|error| std::io::Error::other(error.to_string()))?;
            let config_store = config::ConfigStore::new(app_data_dir.join("config.json"));
            let config = config_store.load().map_err(std::io::Error::other)?;
            let storage_root = config
                .storage_dir
                .clone()
                .unwrap_or_else(storage::StorageManager::default_root);
            let storage_manager =
                storage::StorageManager::new(storage_root).map_err(std::io::Error::other)?;
            let _ = storage_manager.cleanup_partials();
            let library_store = library::LibraryStore::open(&app_data_dir.join("library.sqlite"))
                .map_err(std::io::Error::other)?;
            let resource_dir = app.path().resource_dir().ok();
            app.manage(audio::AudioState(std::sync::Mutex::new(
                audio::AudioMixer::new(config.replay.audio.clone()),
            )));
            app.manage(config::ConfigState(std::sync::Mutex::new(config_store)));
            app.manage(library::LibraryState(std::sync::Mutex::new(library_store)));
            let media_jobs =
                media::MediaJobService::new(resource_dir.clone(), app_data_dir.join("proxy-cache"))
                    .map_err(std::io::Error::other)?;
            app.manage(media::MediaJobState(std::sync::Mutex::new(media_jobs)));
            app.manage(storage::StorageState(std::sync::Mutex::new(
                storage_manager.clone(),
            )));
            app.manage(hotkey::HotkeyState::start(app.handle().clone()));
            let open = MenuItem::with_id(app, "open", "Abrir MoonLit", true, None::<&str>)?;
            let save = MenuItem::with_id(app, "save", "Guardar clip", true, Some("F8"))?;
            let quit = MenuItem::with_id(app, "quit", "Salir", true, None::<&str>)?;
            let menu = Menu::with_items(app, &[&open, &save, &quit])?;
            let _tray = TrayIconBuilder::new()
                .menu(&menu)
                .icon(tauri::image::Image::from_bytes(include_bytes!("../icons/icon.png"))?)
                .tooltip("MoonLit")
                .on_menu_event(|app, event| match event.id().as_ref() {
                    "open" => {
                        if let Some(window) = app.get_webview_window("main") {
                            let _ = window.show();
                            let _ = window.set_focus();
                        }
                    }
                    "save" => {
                        let _ = app.emit(
                            "moonlit://hotkey",
                            serde_json::json!({"type":"saveClip","key":"tray"}),
                        );
                    }
                    "quit" => app.exit(0),
                    _ => {}
                })
                .build(app)?;
            let initial_backend = backends::create(config.backend.clone(), resource_dir.clone())
                .unwrap_or_else(|_| Box::new(backends::fake::FakeBackend::new()));
            app.manage(recorder::RecorderRuntime::new_with_backend(
                storage_manager.root().to_path_buf(),
                resource_dir,
                Some(app.handle().clone()),
                initial_backend,
            ));
            Ok(())
        })
        .on_window_event(|window, event| {
            if let WindowEvent::CloseRequested { api, .. } = event {
                api.prevent_close();
                let _ = window.hide();
            }
        })
        .invoke_handler(tauri::generate_handler![
            doctor::run_doctor,
            config::get_app_config,
            config::save_app_config,
            audio::get_audio_snapshot,
            audio::set_audio_config,
            recorder::get_capture_snapshot,
            recorder::list_capture_backends,
            recorder::list_capture_sources,
            recorder::select_capture_backend,
            recorder::set_capture_output_dir,
            recorder::start_capture,
            recorder::save_clip,
            recorder::stop_capture,
            storage::get_storage_stats,
            storage::set_storage_root,
            library::list_library,
            library::get_library_clip,
            library::update_library_clip,
            library::delete_library_clip,
            library::mark_library_proxy,
            media::create_clip_proxy
        ])
        .run(tauri::generate_context!())
        .expect("error while running MoonLit");
}

#[cfg(test)]
pub fn run() {}
