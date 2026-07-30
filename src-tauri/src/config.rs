use std::fs;
use std::path::PathBuf;

use serde::{Deserialize, Serialize};

use crate::traits::{BackendId, ReplayConfig};

pub const CONFIG_SCHEMA_VERSION: u32 = 1;

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
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
            backend: BackendId::Fake,
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
        if !self.path.is_file() {
            return Ok(AppConfig::default());
        }
        let bytes = fs::read(&self.path).map_err(|error| error.to_string())?;
        let mut config: AppConfig =
            serde_json::from_slice(&bytes).map_err(|error| error.to_string())?;
        migrate(&mut config)?;
        config.validate()?;
        Ok(config)
    }

    pub fn save(&self, config: &AppConfig) -> Result<(), String> {
        config.validate()?;
        let parent = self
            .path
            .parent()
            .ok_or_else(|| "La configuracion no tiene una carpeta padre".to_string())?;
        fs::create_dir_all(parent).map_err(|error| error.to_string())?;
        let bytes = serde_json::to_vec_pretty(config).map_err(|error| error.to_string())?;
        let temporary = self.path.with_extension("json.tmp");
        fs::write(&temporary, bytes).map_err(|error| error.to_string())?;
        if let Err(error) = fs::rename(&temporary, &self.path) {
            let _ = fs::remove_file(&temporary);
            return Err(error.to_string());
        }
        Ok(())
    }
}

#[tauri::command]
pub fn get_app_config(state: tauri::State<'_, ConfigState>) -> Result<AppConfig, String> {
    state
        .0
        .lock()
        .map_err(|_| "La configuracion esta bloqueada".to_string())?
        .load()
}

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

fn migrate(config: &mut AppConfig) -> Result<(), String> {
    if config.schema_version == 0 {
        config.schema_version = CONFIG_SCHEMA_VERSION;
    }
    if config.schema_version != CONFIG_SCHEMA_VERSION {
        return Err(format!(
            "No existe una migracion para la configuracion v{}",
            config.schema_version
        ));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use std::fs;

    use super::{AppConfig, ConfigStore};

    #[test]
    fn config_round_trips_atomically() {
        let directory = std::env::temp_dir().join(format!("moonlit-config-{}", std::process::id()));
        fs::create_dir_all(&directory).expect("directory");
        let store = ConfigStore::new(directory.join("config.json"));
        let config = AppConfig::default();
        store.save(&config).expect("save");
        assert_eq!(
            store.load().expect("load").schema_version,
            config.schema_version
        );
        fs::remove_dir_all(directory).expect("cleanup");
    }
}
