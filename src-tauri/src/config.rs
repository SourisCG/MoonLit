#[cfg(unix)]
use std::fs::File;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use serde::{Deserialize, Serialize};

use crate::traits::{BackendId, ReplayConfig};

pub const CONFIG_SCHEMA_VERSION: u32 = 1;

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
#[serde(default)]
pub struct HotkeyConfig {
    pub save_clip: String,
}

impl Default for HotkeyConfig {
    fn default() -> Self {
        Self {
            save_clip: "F8".to_string(),
        }
    }
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
#[serde(default)]
pub struct AppConfig {
    pub schema_version: u32,
    pub backend: BackendId,
    pub replay: ReplayConfig,
    pub storage_dir: Option<PathBuf>,
    pub hotkeys: HotkeyConfig,
    pub minimize_to_tray: bool,
    pub start_minimized: bool,
    pub notifications_enabled: bool,
    pub onboarding_version: u32,
}

impl Default for AppConfig {
    fn default() -> Self {
        Self {
            schema_version: CONFIG_SCHEMA_VERSION,
            backend: if cfg!(target_os = "windows") {
                BackendId::LibobsSidecar
            } else {
                BackendId::Fake
            },
            replay: ReplayConfig::default(),
            storage_dir: None,
            hotkeys: HotkeyConfig::default(),
            minimize_to_tray: true,
            start_minimized: false,
            notifications_enabled: true,
            onboarding_version: 0,
        }
    }
}

impl AppConfig {
    pub fn validate(&self) -> Result<(), String> {
        if self.schema_version != CONFIG_SCHEMA_VERSION {
            return Err(format!(
                "version de configuracion no soportada: {}",
                self.schema_version
            ));
        }
        if self.hotkeys.save_clip.trim().is_empty() {
            return Err("La combinacion para guardar clips no puede estar vacia".to_string());
        }
        if let Some(path) = &self.storage_dir {
            if !path.is_absolute() {
                return Err("La carpeta de clips debe ser una ruta absoluta".to_string());
            }
        }
        self.replay
            .validate(&[])
            .or_else(|error| {
                if error.code == crate::traits::BackendErrorCode::SourceNotFound {
                    Ok(())
                } else {
                    Err(error)
                }
            })
            .map_err(|error| error.message)
    }
}

pub struct ConfigStore {
    path: PathBuf,
}

pub struct ConfigState(pub std::sync::Mutex<ConfigStore>);

impl ConfigStore {
    pub fn new(path: PathBuf) -> Self {
        Self { path }
    }

    pub fn load(&self) -> Result<AppConfig, String> {
        match fs::symlink_metadata(&self.path) {
            Ok(metadata) => {
                if metadata.file_type().is_symlink() || !metadata.file_type().is_file() {
                    return Err("La configuracion no es un archivo regular".to_string());
                }
            }
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
                return Ok(AppConfig::default());
            }
            Err(error) => return Err(error.to_string()),
        }
        let bytes = fs::read(&self.path).map_err(|error| error.to_string())?;
        let mut config: AppConfig = match serde_json::from_slice(&bytes) {
            Ok(config) => config,
            Err(error) => return self.recover_corrupt(&format!("JSON invalido: {error}")),
        };
        let migrated = match migrate(&mut config) {
            Ok(migrated) => migrated,
            Err(error) => return self.recover_corrupt(&error),
        };
        if let Err(error) = config.validate() {
            return self.recover_corrupt(&error);
        }
        if migrated {
            self.save(&config)?;
        }
        Ok(config)
    }

    pub fn save(&self, config: &AppConfig) -> Result<(), String> {
        config.validate()?;
        if !self.path.is_absolute() {
            return Err("La ruta de configuracion debe ser absoluta".to_string());
        }
        if let Ok(metadata) = fs::symlink_metadata(&self.path) {
            if metadata.file_type().is_symlink() || !metadata.file_type().is_file() {
                return Err("La configuracion no es un archivo regular".to_string());
            }
        }
        let parent = self
            .path
            .parent()
            .ok_or_else(|| "La configuracion no tiene una carpeta padre".to_string())?;
        fs::create_dir_all(parent).map_err(|error| error.to_string())?;
        let bytes = serde_json::to_vec_pretty(config).map_err(|error| error.to_string())?;
        let temporary = unique_sibling(&self.path, "tmp");
        let mut file = OpenOptions::new()
            .create_new(true)
            .write(true)
            .open(&temporary)
            .map_err(|error| error.to_string())?;
        if let Err(error) = file.write_all(&bytes).and_then(|_| file.sync_all()) {
            let _ = fs::remove_file(&temporary);
            return Err(error.to_string());
        }
        drop(file);
        if let Err(error) = replace_file(&temporary, &self.path) {
            let _ = fs::remove_file(&temporary);
            return Err(error.to_string());
        }
        #[cfg(unix)]
        if let Ok(directory) = File::open(parent) {
            let _ = directory.sync_all();
        }
        Ok(())
    }

    fn recover_corrupt(&self, reason: &str) -> Result<AppConfig, String> {
        let quarantine = quarantine_path(&self.path)?;
        fs::rename(&self.path, &quarantine).map_err(|error| {
            format!("No se pudo apartar la configuracion corrupta ({reason}): {error}")
        })?;
        Err(format!(
            "La configuracion fue apartada por estar corrupta ({reason}): {}",
            quarantine.display()
        ))
    }
}

#[cfg(not(test))]
#[tauri::command]
pub fn get_app_config(state: tauri::State<'_, ConfigState>) -> Result<AppConfig, String> {
    state
        .0
        .lock()
        .map_err(|_| "La configuracion esta bloqueada".to_string())?
        .load()
}

#[cfg(not(test))]
#[tauri::command]
pub fn save_app_config(
    state: tauri::State<'_, ConfigState>,
    config: AppConfig,
) -> Result<AppConfig, String> {
    let store = state
        .0
        .lock()
        .map_err(|_| "La configuracion esta bloqueada".to_string())?;
    store.save(&config)?;
    Ok(config)
}

fn migrate(config: &mut AppConfig) -> Result<bool, String> {
    if config.schema_version == 0 {
        config.schema_version = CONFIG_SCHEMA_VERSION;
        return Ok(true);
    }
    if config.schema_version != CONFIG_SCHEMA_VERSION {
        return Err(format!(
            "No existe una migracion para la configuracion v{}",
            config.schema_version
        ));
    }
    Ok(false)
}

fn unique_sibling(path: &Path, kind: &str) -> PathBuf {
    let file_name = path
        .file_name()
        .and_then(|name| name.to_str())
        .unwrap_or("config.json");
    let timestamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_nanos();
    path.with_file_name(format!(
        ".{file_name}.{kind}-{}-{timestamp}",
        std::process::id()
    ))
}

fn quarantine_path(path: &Path) -> Result<PathBuf, String> {
    let file_name = path
        .file_name()
        .and_then(|name| name.to_str())
        .ok_or_else(|| "La configuracion no tiene un nombre valido".to_string())?;
    let timestamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_nanos();
    for suffix in 0..1000u32 {
        let candidate = path.with_file_name(format!(
            ".{file_name}.corrupt-{}-{timestamp}-{suffix}",
            std::process::id()
        ));
        if !candidate.exists() {
            return Ok(candidate);
        }
    }
    Err("No se encontro un nombre seguro para la configuracion corrupta".to_string())
}

fn replace_file(temporary: &Path, target: &Path) -> std::io::Result<()> {
    match fs::rename(temporary, target) {
        Ok(()) => Ok(()),
        Err(_first_error) if target.exists() => {
            let backup = unique_sibling(target, "backup");
            fs::rename(target, &backup)?;
            match fs::rename(temporary, target) {
                Ok(()) => {
                    let _ = fs::remove_file(backup);
                    Ok(())
                }
                Err(error) => {
                    let _ = fs::rename(&backup, target);
                    let _ = fs::remove_file(temporary);
                    Err(error)
                }
            }
        }
        Err(error) => Err(error),
    }
}

#[cfg(test)]
mod tests {
    use std::fs;
    use std::time::{SystemTime, UNIX_EPOCH};

    use super::{AppConfig, ConfigStore, CONFIG_SCHEMA_VERSION};

    fn temporary_directory(label: &str) -> std::path::PathBuf {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos();
        let directory = std::env::temp_dir().join(format!("moonlit-config-{label}-{stamp}"));
        fs::create_dir_all(&directory).expect("directory");
        directory
    }

    #[test]
    fn config_round_trips_atomically() {
        let directory = temporary_directory("round-trip");
        let store = ConfigStore::new(directory.join("config.json"));
        let config = AppConfig::default();
        store.save(&config).expect("save");
        assert_eq!(
            store.load().expect("load").schema_version,
            config.schema_version
        );
        fs::remove_dir_all(directory).expect("cleanup");
    }

    #[test]
    fn schema_zero_is_migrated_and_persisted() {
        let directory = temporary_directory("migration");
        let path = directory.join("config.json");
        fs::write(&path, br#"{"schemaVersion":0}"#).expect("legacy config");
        let store = ConfigStore::new(path.clone());
        assert_eq!(
            store.load().expect("load").schema_version,
            CONFIG_SCHEMA_VERSION
        );
        let persisted: serde_json::Value =
            serde_json::from_slice(&fs::read(path).expect("persisted config")).expect("json");
        assert_eq!(persisted["schemaVersion"], CONFIG_SCHEMA_VERSION);
        fs::remove_dir_all(directory).expect("cleanup");
    }

    #[test]
    fn corrupt_config_is_quarantined_and_reported() {
        let directory = temporary_directory("corrupt");
        let path = directory.join("config.json");
        fs::write(&path, b"not json").expect("corrupt config");
        let store = ConfigStore::new(path.clone());
        let error = store.load().expect_err("corrupt config must fail closed");
        assert!(error.contains("apartada"));
        assert!(!path.exists());
        assert!(fs::read_dir(&directory)
            .expect("directory")
            .flatten()
            .any(|entry| entry.file_name().to_string_lossy().contains("corrupt-")));
        fs::remove_dir_all(directory).expect("cleanup");
    }
}
