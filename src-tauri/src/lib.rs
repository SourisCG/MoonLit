mod backends;
mod capture;
mod doctor;
mod recorder;
mod state;
mod traits;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .manage(recorder::CaptureService::default())
        .invoke_handler(tauri::generate_handler![
            capture::get_capture_backend,
            doctor::run_doctor,
            recorder::get_runtime_snapshot,
            recorder::start_capture,
            recorder::save_clip,
            recorder::stop_capture,
            recorder::set_capture_backend,
            recorder::set_external_capture_backend
        ])
        .run(tauri::generate_context!())
        .expect("error while running MoonLit");
}
