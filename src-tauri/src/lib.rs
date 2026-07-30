mod backends;
mod doctor;
mod recorder;
pub(crate) mod replay;
#[cfg(target_os = "windows")]
mod sidecar;
mod state;
mod traits;

use tauri::Manager;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .setup(|app| {
            let data_dir = app
                .path()
                .app_data_dir()
                .map_err(|error| std::io::Error::other(error.to_string()))?
                .join("captures");
            let resource_dir = app.path().resource_dir().ok();
            app.manage(recorder::RecorderRuntime::new(
                data_dir,
                resource_dir,
                Some(app.handle().clone()),
            ));
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            doctor::run_doctor,
            recorder::get_capture_snapshot,
            recorder::list_capture_backends,
            recorder::list_capture_sources,
            recorder::select_capture_backend,
            recorder::start_capture,
            recorder::save_clip,
            recorder::stop_capture
        ])
        .run(tauri::generate_context!())
        .expect("error while running MoonLit");
}
