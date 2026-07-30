use std::fs;
use std::path::{Path, PathBuf};

use serde::Serialize;

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct StorageStats {
    pub root: PathBuf,
    pub clip_count: u64,
    pub bytes_used: u64,
    pub available_bytes: Option<u64>,
}

#[derive(Clone, Debug)]
pub struct StorageManager {
    root: PathBuf,
}

impl StorageManager {
    pub fn new(root: PathBuf) -> Result<Self, String> {
        if !root.is_absolute() {
            return Err("La carpeta de clips debe ser una ruta absoluta".to_string());
        }
        fs::create_dir_all(&root).map_err(|error| error.to_string())?;
        Ok(Self { root })
    }

    pub fn default_root() -> PathBuf {
        if let Some(profile) = std::env::var_os("USERPROFILE") {
            return PathBuf::from(profile).join("Videos").join("MoonLit");
        }
        std::env::var_os("XDG_VIDEOS_DIR")
            .map(PathBuf::from)
            .unwrap_or_else(|| {
                std::env::var_os("HOME")
                    .map(PathBuf::from)
                    .unwrap_or_else(|| PathBuf::from("."))
                    .join("Videos")
            })
            .join("MoonLit")
    }

    pub fn root(&self) -> &Path {
        &self.root
    }

    pub fn set_root(&mut self, root: PathBuf) -> Result<(), String> {
        if !root.is_absolute() {
            return Err("La carpeta de clips debe ser una ruta absoluta".to_string());
        }
        fs::create_dir_all(&root).map_err(|error| error.to_string())?;
        self.root = root;
        Ok(())
    }

    pub fn cleanup_partials(&self) -> Result<u64, String> {
        let mut removed = 0;
        for entry in fs::read_dir(&self.root).map_err(|error| error.to_string())? {
            let path = entry.map_err(|error| error.to_string())?.path();
            if path.is_file()
                && path
                    .extension()
                    .is_some_and(|extension| extension == "partial" || extension == "tmp")
            {
                fs::remove_file(path).map_err(|error| error.to_string())?;
                removed += 1;
            }
        }
        Ok(removed)
    }

    pub fn stats(&self) -> Result<StorageStats, String> {
        let mut clip_count: u64 = 0;
        let mut bytes_used: u64 = 0;
        collect_stats(&self.root, &mut clip_count, &mut bytes_used)?;
        Ok(StorageStats {
            root: self.root.clone(),
            clip_count,
            bytes_used,
            available_bytes: None,
        })
    }
}

fn collect_stats(path: &Path, clip_count: &mut u64, bytes_used: &mut u64) -> Result<(), String> {
    for entry in fs::read_dir(path).map_err(|error| error.to_string())? {
        let path = entry.map_err(|error| error.to_string())?.path();
        if path.is_dir() {
            collect_stats(&path, clip_count, bytes_used)?;
        } else if path.is_file() {
            let metadata = fs::metadata(&path).map_err(|error| error.to_string())?;
            *bytes_used = bytes_used.saturating_add(metadata.len());
            if path.extension().is_some_and(|extension| {
                matches!(extension.to_str(), Some("mp4" | "mkv" | "h264" | "hevc"))
            }) {
                *clip_count += 1;
            }
        }
    }
    Ok(())
}

pub struct StorageState(pub std::sync::Mutex<StorageManager>);

#[tauri::command]
pub fn get_storage_stats(state: tauri::State<'_, StorageState>) -> Result<StorageStats, String> {
    state
        .0
        .lock()
        .map_err(|_| "El almacenamiento esta bloqueado".to_string())?
        .stats()
}

#[tauri::command]
pub fn set_storage_root(
    storage: tauri::State<'_, StorageState>,
    config_state: tauri::State<'_, crate::config::ConfigState>,
    runtime: tauri::State<'_, crate::recorder::RecorderRuntime>,
    root: PathBuf,
) -> Result<StorageStats, String> {
    runtime
        .set_output_dir(root.clone())
        .map_err(|error| error.message)?;
    let mut manager = storage
        .0
        .lock()
        .map_err(|_| "El almacenamiento esta bloqueado".to_string())?;
    manager.set_root(root.clone())?;
    let config_store = config_state
        .0
        .lock()
        .map_err(|_| "La configuracion esta bloqueada".to_string())?;
    let mut config = config_store.load()?;
    config.storage_dir = Some(root);
    config_store.save(&config)?;
    manager.stats()
}
