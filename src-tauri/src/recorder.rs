use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;
use std::time::{SystemTime, UNIX_EPOCH};

use serde::Serialize;
use tauri::{AppHandle, Manager, State};

use crate::capture::{CaptureBackend, CaptureProfile, CapturedClip, GsrRecorder};
use crate::state::{CaptureStatus, ClipRecord, RuntimeSnapshot};

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct SimulationManifest<'a> {
    id: &'a str,
    created_at: u64,
    duration_seconds: u32,
    backend: &'a str,
    note: &'a str,
}

#[derive(Default)]
struct FakeBackend {
    profile: Option<CaptureProfile>,
}

pub struct CaptureService {
    controller: Mutex<CaptureController>,
}

struct CaptureController {
    engine: Box<dyn CaptureBackend>,
    runtime: RuntimeSnapshot,
}

impl Default for CaptureService {
    fn default() -> Self {
        Self {
            controller: Mutex::new(CaptureController {
                engine: Box::new(FakeBackend::default()),
                runtime: RuntimeSnapshot::default(),
            }),
        }
    }
}

impl CaptureService {
    fn lock(&self) -> Result<std::sync::MutexGuard<'_, CaptureController>, String> {
        self.controller
            .lock()
            .map_err(|_| "El estado de captura quedó bloqueado".to_string())
    }

    fn snapshot(&self) -> Result<RuntimeSnapshot, String> {
        Ok(self.lock()?.runtime.clone())
    }

    fn start(&self, app_data_dir: PathBuf, buffer_seconds: u32) -> Result<RuntimeSnapshot, String> {
        if !(10..=300).contains(&buffer_seconds) {
            return Err("La duración debe estar entre 10 y 300 segundos".to_string());
        }

        let mut controller = self.lock()?;
        if matches!(controller.runtime.status, CaptureStatus::Buffering) {
            return Err("El buffer ya está activo".to_string());
        }

        let profile = CaptureProfile {
            output_dir: app_data_dir.join("captures"),
            buffer_seconds,
            ..CaptureProfile::default()
        };
        controller.engine.start(&profile)?;
        controller.runtime.status = CaptureStatus::Buffering;
        controller.runtime.session_id = Some(unique_id("session"));
        controller.runtime.game_label = Some("Simulación MoonLit".to_string());
        controller.runtime.started_at = Some(now_seconds());
        controller.runtime.buffer_seconds = buffer_seconds;
        controller.runtime.message = if controller.engine.name() == "fake" {
            "Buffer simulado activo. Puedes guardar un clip.".to_string()
        } else {
            "Buffer nativo activo. Puedes guardar un clip.".to_string()
        };

        Ok(controller.runtime.clone())
    }

    fn save_clip(&self) -> Result<RuntimeSnapshot, String> {
        let mut controller = self.lock()?;
        if !matches!(controller.runtime.status, CaptureStatus::Buffering) {
            return Err("Inicia el buffer antes de guardar un clip".to_string());
        }

        let captured = controller.engine.save_clip()?;
        controller.runtime.saved_clips = controller.runtime.saved_clips.saturating_add(1);
        controller.runtime.last_clip = Some(clip_record(captured));
        controller.runtime.message = if controller.engine.name() == "fake" {
            "Clip simulado guardado en el directorio de datos de MoonLit.".to_string()
        } else {
            "Clip nativo guardado en el directorio de datos de MoonLit.".to_string()
        };

        Ok(controller.runtime.clone())
    }

    fn stop(&self) -> Result<RuntimeSnapshot, String> {
        let mut controller = self.lock()?;
        controller.engine.stop()?;
        controller.runtime.status = CaptureStatus::Idle;
        controller.runtime.session_id = None;
        controller.runtime.game_label = None;
        controller.runtime.started_at = None;
        controller.runtime.message = "Buffer detenido.".to_string();
        Ok(controller.runtime.clone())
    }

    fn select_backend(
        &self,
        backend: &str,
        resource_dir: Option<PathBuf>,
    ) -> Result<RuntimeSnapshot, String> {
        let mut controller = self.lock()?;
        if matches!(controller.runtime.status, CaptureStatus::Buffering) {
            return Err("Detén el buffer antes de cambiar de backend".to_string());
        }

        let engine: Box<dyn CaptureBackend> = match backend {
            "fake" => Box::new(FakeBackend::default()),
            "gpu-screen-recorder" => {
                let engine = GsrRecorder::discover_with_resource_dir(resource_dir);
                let status = engine.status();
                if !status.available {
                    return Err(status.note);
                }
                Box::new(engine)
            }
            _ => return Err("Backend no soportado".to_string()),
        };

        controller.runtime.backend = engine.name().to_string();
        controller.runtime.message = format!("Backend seleccionado: {}.", engine.name());
        controller.engine = engine;
        Ok(controller.runtime.clone())
    }

    fn select_external_backend(&self, path: PathBuf) -> Result<RuntimeSnapshot, String> {
        let mut controller = self.lock()?;
        if matches!(controller.runtime.status, CaptureStatus::Buffering) {
            return Err("Detén el buffer antes de cambiar de backend".to_string());
        }

        let engine = GsrRecorder::from_external_path(path)?;
        let status = engine.status();
        if !status.available {
            return Err(status.note);
        }
        controller.runtime.backend = engine.name().to_string();
        controller.runtime.message = format!("Backend externo seleccionado: {}.", engine.name());
        controller.engine = Box::new(engine);
        Ok(controller.runtime.clone())
    }
}

impl Drop for CaptureService {
    fn drop(&mut self) {
        if let Ok(mut controller) = self.controller.lock() {
            let _ = controller.engine.stop();
        }
    }
}

impl CaptureBackend for FakeBackend {
    fn name(&self) -> &'static str {
        "fake"
    }

    fn start(&mut self, profile: &CaptureProfile) -> Result<(), String> {
        if self.profile.is_some() {
            return Err("El buffer simulado ya está activo".to_string());
        }
        self.profile = Some(profile.clone());
        Ok(())
    }

    fn save_clip(&mut self) -> Result<CapturedClip, String> {
        let profile = self
            .profile
            .as_ref()
            .ok_or_else(|| "Inicia el buffer antes de guardar un clip".to_string())?;
        let id = unique_id("sim");
        let created_at = now_seconds();
        let clips_dir = profile.output_dir.join("simulated-clips");
        fs::create_dir_all(&clips_dir)
            .map_err(|error| format!("No se pudo crear el directorio de clips: {error}"))?;

        let manifest_path = clips_dir.join(format!("{id}.json"));
        let manifest = SimulationManifest {
            id: &id,
            created_at,
            duration_seconds: profile.buffer_seconds,
            backend: "fake",
            note: "Manifest generado por FakeBackend; todavía no contiene vídeo real.",
        };
        let contents = serde_json::to_vec_pretty(&manifest)
            .map_err(|error| format!("No se pudo serializar el manifest: {error}"))?;
        write_atomic(&manifest_path, &contents)?;

        Ok(CapturedClip {
            path: manifest_path,
            duration_seconds: profile.buffer_seconds,
            kind: "simulation".to_string(),
        })
    }

    fn stop(&mut self) -> Result<(), String> {
        self.profile = None;
        Ok(())
    }
}

#[tauri::command]
pub fn get_runtime_snapshot(service: State<'_, CaptureService>) -> Result<RuntimeSnapshot, String> {
    service.snapshot()
}

#[tauri::command]
pub fn start_capture(
    app: AppHandle,
    service: State<'_, CaptureService>,
    buffer_seconds: u32,
) -> Result<RuntimeSnapshot, String> {
    let data_dir = app
        .path()
        .app_data_dir()
        .map_err(|error| format!("No se pudo resolver el directorio de datos: {error}"))?;
    service.start(data_dir, buffer_seconds)
}

#[tauri::command]
pub fn save_clip(service: State<'_, CaptureService>) -> Result<RuntimeSnapshot, String> {
    service.save_clip()
}

#[tauri::command]
pub fn stop_capture(service: State<'_, CaptureService>) -> Result<RuntimeSnapshot, String> {
    service.stop()
}

#[tauri::command]
pub fn set_capture_backend(
    app: AppHandle,
    service: State<'_, CaptureService>,
    backend: String,
) -> Result<RuntimeSnapshot, String> {
    let resource_dir = app.path().resource_dir().ok();
    service.select_backend(&backend, resource_dir)
}

#[tauri::command]
pub fn set_external_capture_backend(
    service: State<'_, CaptureService>,
    path: String,
) -> Result<RuntimeSnapshot, String> {
    service.select_external_backend(PathBuf::from(path))
}

fn clip_record(captured: CapturedClip) -> ClipRecord {
    ClipRecord {
        id: unique_id("clip"),
        path: captured.path.to_string_lossy().into_owned(),
        created_at: now_seconds(),
        duration_seconds: captured.duration_seconds,
        kind: captured.kind,
    }
}

fn write_atomic(path: &Path, contents: &[u8]) -> Result<(), String> {
    let temporary_path = path.with_extension("json.tmp");
    fs::write(&temporary_path, contents)
        .map_err(|error| format!("No se pudo escribir el manifest temporal: {error}"))?;
    if let Err(error) = fs::rename(&temporary_path, path) {
        let _ = fs::remove_file(&temporary_path);
        return Err(format!("No se pudo finalizar el manifest: {error}"));
    }
    Ok(())
}

fn now_seconds() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}

static NEXT_ID: AtomicU64 = AtomicU64::new(0);

fn unique_id(prefix: &str) -> String {
    let millis = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis();
    let sequence = NEXT_ID.fetch_add(1, Ordering::Relaxed);
    format!("{prefix}-{millis}-{sequence}")
}

#[cfg(test)]
mod tests {
    use std::fs;
    use std::path::PathBuf;

    use super::{CaptureService, FakeBackend};
    use crate::capture::{CaptureBackend, CaptureProfile};
    use crate::state::CaptureStatus;

    fn temporary_directory() -> PathBuf {
        let path = std::env::temp_dir().join(format!("moonlit-test-{}", super::unique_id("dir")));
        fs::create_dir_all(&path).expect("temporary directory");
        path
    }

    #[test]
    fn fake_backend_writes_a_manifest_atomically() {
        let directory = temporary_directory();
        let profile = CaptureProfile {
            output_dir: directory.clone(),
            buffer_seconds: 10,
            ..CaptureProfile::default()
        };
        let mut backend = FakeBackend::default();
        backend.start(&profile).expect("start fake backend");
        let clip = backend.save_clip().expect("save fake clip");
        let contents = fs::read_to_string(&clip.path).expect("manifest exists");
        assert!(contents.contains("FakeBackend"));
        let temporary_manifest = clip.path.with_extension("json.tmp");
        assert!(!temporary_manifest.exists());
        backend.stop().expect("stop fake backend");
        fs::remove_dir_all(directory).expect("remove temporary directory");
    }

    #[test]
    fn fake_backend_rejects_save_before_start() {
        let mut backend = FakeBackend::default();
        assert!(backend.save_clip().is_err());
    }

    #[test]
    fn service_starts_with_fake_backend() {
        let directory = temporary_directory();
        let service = CaptureService::default();
        let snapshot = service.start(directory.clone(), 10).expect("start service");
        assert_eq!(snapshot.status, CaptureStatus::Buffering);
        let saved = service.save_clip().expect("save service clip");
        assert_eq!(saved.saved_clips, 1);
        assert!(saved.last_clip.is_some());
        service.stop().expect("stop service");
        fs::remove_dir_all(directory).expect("remove temporary directory");
    }
}
