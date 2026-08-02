use std::fs;
use std::path::{Path, PathBuf};

#[cfg(windows)]
use std::os::windows::fs::MetadataExt;

use serde::Serialize;

#[cfg(test)]
use crate::backends::host_library as library_module;
#[cfg(not(test))]
use crate::{config as config_module, library as library_module, recorder as recorder_module};

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
        let root = prepare_root(&root)?;
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
        let root = prepare_root(&root)?;
        self.root = root;
        Ok(())
    }

    pub fn cleanup_partials(&self) -> Result<u64, String> {
        ensure_safe_root(&self.root)?;
        let _root_guard = library_module::hold_registered_directory(&self.root)?;
        let mut removed = 0;
        for entry in fs::read_dir(&self.root).map_err(|error| error.to_string())? {
            let path = entry.map_err(|error| error.to_string())?.path();
            let metadata = match fs::symlink_metadata(&path) {
                Ok(metadata) => metadata,
                Err(error) if error.kind() == std::io::ErrorKind::NotFound => continue,
                Err(error) => return Err(error.to_string()),
            };
            if unsafe_metadata(&metadata) || !metadata.file_type().is_file() {
                // Never follow a link, junction or directory during startup
                // cleanup. In particular, do not recurse into a user sentinel.
                continue;
            }
            if path.extension().is_some_and(|extension| {
                extension.eq_ignore_ascii_case("partial") || extension.eq_ignore_ascii_case("tmp")
            }) {
                // The library helper revalidates containment immediately
                // before deleting and uses no-follow/delete-by-handle
                // semantics on Windows. Do not canonicalize and then call
                // remove_file on a path that an attacker can swap.
                library_module::remove_registered_file(&path)?;
                removed += 1;
            }
        }
        Ok(removed)
    }

    pub fn stats(&self) -> Result<StorageStats, String> {
        ensure_safe_root(&self.root)?;
        let _root_guard = library_module::hold_registered_directory(&self.root)?;
        let mut clip_count: u64 = 0;
        let mut bytes_used: u64 = 0;
        collect_stats(&self.root, &self.root, &mut clip_count, &mut bytes_used)?;
        Ok(StorageStats {
            root: self.root.clone(),
            clip_count,
            bytes_used,
            available_bytes: None,
        })
    }
}

fn collect_stats(
    root: &Path,
    path: &Path,
    clip_count: &mut u64,
    bytes_used: &mut u64,
) -> Result<(), String> {
    if !path.starts_with(root) {
        return Err("La estadistica salio de la raiz registrada".to_string());
    }
    let _directory_guard = library_module::hold_registered_directory(path)?;
    for entry in fs::read_dir(path).map_err(|error| error.to_string())? {
        let path = entry.map_err(|error| error.to_string())?.path();
        let metadata = match fs::symlink_metadata(&path) {
            Ok(metadata) => metadata,
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => continue,
            Err(error) => return Err(error.to_string()),
        };
        if unsafe_metadata(&metadata) {
            continue;
        }
        if metadata.file_type().is_dir() {
            collect_stats(root, &path, clip_count, bytes_used)?;
        } else if metadata.file_type().is_file() {
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

fn prepare_root(root: &Path) -> Result<PathBuf, String> {
    if !root.is_absolute() {
        return Err("La carpeta de clips debe ser una ruta absoluta".to_string());
    }
    ensure_no_reparse_components(root)?;
    fs::create_dir_all(root).map_err(|error| error.to_string())?;
    ensure_no_reparse_components(root)?;
    let canonical = fs::canonicalize(root).map_err(|error| error.to_string())?;
    let metadata = fs::symlink_metadata(&canonical).map_err(|error| error.to_string())?;
    if unsafe_metadata(&metadata) || !metadata.file_type().is_dir() {
        return Err("La carpeta de clips no es un directorio seguro".to_string());
    }
    library_module::register_root(&canonical)?;
    Ok(canonical)
}

fn ensure_safe_root(root: &Path) -> Result<(), String> {
    ensure_no_reparse_components(root)?;
    let metadata = fs::symlink_metadata(root).map_err(|error| error.to_string())?;
    if unsafe_metadata(&metadata) || !metadata.file_type().is_dir() {
        return Err("La carpeta de clips no es un directorio seguro".to_string());
    }
    let canonical = fs::canonicalize(root).map_err(|error| error.to_string())?;
    if canonical != root {
        return Err("La carpeta de clips cambio de identidad".to_string());
    }
    Ok(())
}

fn ensure_no_reparse_components(path: &Path) -> Result<(), String> {
    for ancestor in path.ancestors() {
        match fs::symlink_metadata(ancestor) {
            Ok(metadata) if unsafe_metadata(&metadata) => {
                return Err(format!(
                    "La ruta contiene un enlace o reparse point: {}",
                    ancestor.display()
                ));
            }
            Ok(_) => {}
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
            Err(error) => return Err(error.to_string()),
        }
    }
    Ok(())
}

fn unsafe_metadata(metadata: &fs::Metadata) -> bool {
    #[cfg(windows)]
    {
        metadata.file_type().is_symlink() || metadata.file_attributes() & 0x0000_0400 != 0
    }
    #[cfg(not(windows))]
    {
        metadata.file_type().is_symlink()
    }
}

pub struct StorageState(pub std::sync::Mutex<StorageManager>);

#[cfg(not(test))]
#[tauri::command]
pub fn get_storage_stats(state: tauri::State<'_, StorageState>) -> Result<StorageStats, String> {
    state
        .0
        .lock()
        .map_err(|_| "El almacenamiento esta bloqueado".to_string())?
        .stats()
}

#[cfg(not(test))]
#[tauri::command]
pub fn set_storage_root(
    storage: tauri::State<'_, StorageState>,
    config_state: tauri::State<'_, config_module::ConfigState>,
    runtime: tauri::State<'_, recorder_module::RecorderRuntime>,
    root: PathBuf,
) -> Result<StorageStats, String> {
    let old_root = storage
        .0
        .lock()
        .map_err(|_| "El almacenamiento esta bloqueado".to_string())?
        .root()
        .to_path_buf();
    let old_config = {
        let store = config_state
            .0
            .lock()
            .map_err(|_| "La configuracion esta bloqueada".to_string())?;
        store.load()?
    };

    // Prepare the directory and register it before changing the recorder, but
    // do not change the active manager until the durable config commit works.
    let prepared_root = prepare_root(&root)?;
    runtime
        .set_output_dir(prepared_root.clone())
        .map_err(|error| error.message)?;

    let mut next_config = old_config.clone();
    next_config.storage_dir = Some(prepared_root.clone());
    let save_result = match config_state.0.lock() {
        Ok(store) => store.save(&next_config),
        Err(_) => Err("La configuracion esta bloqueada".to_string()),
    };
    if let Err(error) = save_result {
        return Err(rollback_root_change(
            &runtime,
            &config_state,
            &old_root,
            &old_config,
            error,
            false,
        ));
    }

    let mut manager = match storage.0.lock() {
        Ok(manager) => manager,
        Err(_) => {
            return Err(rollback_root_change(
                &runtime,
                &config_state,
                &old_root,
                &old_config,
                "El almacenamiento esta bloqueado".to_string(),
                true,
            ));
        }
    };
    if let Err(error) = manager.set_root(prepared_root) {
        drop(manager);
        return Err(rollback_root_change(
            &runtime,
            &config_state,
            &old_root,
            &old_config,
            error,
            true,
        ));
    }
    match manager.stats() {
        Ok(stats) => Ok(stats),
        Err(error) => {
            manager.root = old_root.clone();
            drop(manager);
            Err(rollback_root_change(
                &runtime,
                &config_state,
                &old_root,
                &old_config,
                error,
                true,
            ))
        }
    }
}

#[cfg(not(test))]
fn rollback_root_change(
    runtime: &tauri::State<'_, recorder_module::RecorderRuntime>,
    config_state: &tauri::State<'_, config_module::ConfigState>,
    old_root: &Path,
    old_config: &config_module::AppConfig,
    original_error: String,
    restore_config: bool,
) -> String {
    let runtime_error = runtime
        .set_output_dir(old_root.to_path_buf())
        .err()
        .map(|error| error.message);
    let config_error = if restore_config {
        match config_state.0.lock() {
            Ok(store) => store.save(old_config).err(),
            Err(_) => Some("La configuracion esta bloqueada".to_string()),
        }
    } else {
        None
    };
    match (runtime_error, config_error) {
        (None, None) => original_error,
        (runtime_error, config_error) => format!(
            "{original_error}; fallo al revertir recorder/configuracion: recorder={:?}, config={:?}",
            runtime_error, config_error
        ),
    }
}

#[cfg(test)]
mod tests {
    use std::fs;
    use std::time::{SystemTime, UNIX_EPOCH};

    use super::StorageManager;

    fn temporary_directory(label: &str) -> std::path::PathBuf {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos();
        let path = std::env::temp_dir().join(format!("moonlit-storage-{label}-{stamp}"));
        fs::create_dir_all(&path).expect("temporary directory");
        path
    }

    #[test]
    fn cleanup_only_removes_direct_safe_partials() {
        let directory = temporary_directory("cleanup");
        let root = directory.join("clips");
        let manager = StorageManager::new(root.clone()).expect("storage");
        fs::write(root.join("safe.partial"), b"partial").expect("partial");
        fs::write(root.join("keep.sentinel"), b"sentinel").expect("sentinel");
        fs::create_dir_all(root.join("nested")).expect("nested");
        fs::write(root.join("nested/unrelated.tmp"), b"unrelated").expect("nested sentinel");
        assert_eq!(manager.cleanup_partials().expect("cleanup"), 1);
        assert!(!root.join("safe.partial").exists());
        assert!(root.join("keep.sentinel").exists());
        assert!(root.join("nested/unrelated.tmp").exists());
        fs::remove_dir_all(directory).expect("cleanup");
    }

    #[cfg(unix)]
    #[test]
    fn symlink_root_is_rejected_before_cleanup() {
        use std::os::unix::fs::symlink;

        let directory = temporary_directory("symlink");
        let real = directory.join("real");
        let link = directory.join("link");
        fs::create_dir_all(&real).expect("real");
        symlink(&real, &link).expect("symlink");
        assert!(StorageManager::new(link).is_err());
        fs::remove_dir_all(directory).expect("cleanup");
    }

    #[cfg(unix)]
    #[test]
    fn partial_symlink_is_left_alongside_its_outside_sentinel() {
        use std::os::unix::fs::symlink;

        let directory = temporary_directory("partial-sentinel");
        let root = directory.join("clips");
        let manager = StorageManager::new(root.clone()).expect("storage");
        let sentinel = directory.join("sentinel.txt");
        fs::write(&sentinel, b"do not delete").expect("sentinel");
        symlink(&sentinel, root.join("attacker.partial")).expect("partial symlink");

        assert_eq!(manager.cleanup_partials().expect("cleanup"), 0);
        assert!(sentinel.exists());
        assert!(root.join("attacker.partial").exists());
        fs::remove_dir_all(directory).expect("cleanup");
    }
}
