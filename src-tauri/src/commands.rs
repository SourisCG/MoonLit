//! Tauri IPC handlers (Phase 2: persistence). Capture/detection/editor land in later phases.

use std::collections::HashMap;
use tauri::State;

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
