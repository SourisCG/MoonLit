use std::path::{Path, PathBuf};
use std::sync::mpsc::{channel, sync_channel, Receiver, RecvTimeoutError, Sender, SyncSender};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use tauri::{AppHandle, Emitter, State};

use crate::backends;
use crate::state::{CapturePhase, CaptureSnapshot, ClipRecord, RecorderEvent, SessionSnapshot};
use crate::traits::{
    BackendDescriptor, BackendError, BackendErrorCode, BackendId, ClipArtifact, ReplayBackend,
    ReplayConfig,
};

type Reply<T> = Sender<Result<T, BackendError>>;

enum RuntimeCommand {
    Backends {
        reply: Reply<Vec<BackendDescriptor>>,
    },
    Sources {
        reply: Reply<Vec<crate::traits::CaptureSource>>,
    },
    SelectBackend {
        id: BackendId,
        reply: Reply<CaptureSnapshot>,
    },
    Start {
        config: ReplayConfig,
        reply: Reply<CaptureSnapshot>,
    },
    Save {
        reply: Reply<CaptureSnapshot>,
    },
    Stop {
        reply: Reply<CaptureSnapshot>,
    },
    Shutdown,
}

pub struct RecorderRuntime {
    commands: SyncSender<RuntimeCommand>,
    snapshot: Arc<Mutex<CaptureSnapshot>>,
    join: Mutex<Option<JoinHandle<()>>>,
}

impl RecorderRuntime {
    pub fn new(
        output_dir: PathBuf,
        resource_dir: Option<PathBuf>,
        app_handle: Option<AppHandle>,
    ) -> Self {
        let fake = backends::fake::FakeBackend::new();
        Self::new_with_backend(output_dir, resource_dir, app_handle, Box::new(fake))
    }

    fn new_with_backend(
        output_dir: PathBuf,
        resource_dir: Option<PathBuf>,
        app_handle: Option<AppHandle>,
        initial_backend: Box<dyn ReplayBackend>,
    ) -> Self {
        let initial_snapshot = CaptureSnapshot {
            revision: 0,
            phase: CapturePhase::Idle,
            backend: initial_backend.descriptor(),
            config: None,
            session: None,
            saved_clips: 0,
            last_clip: None,
            last_error: None,
        };
        let snapshot = Arc::new(Mutex::new(initial_snapshot));
        let (commands, receiver) = sync_channel(64);
        let actor_snapshot = Arc::clone(&snapshot);
        let join = thread::Builder::new()
            .name("moonlit-recorder".to_string())
            .spawn(move || {
                actor_loop(
                    receiver,
                    actor_snapshot,
                    output_dir,
                    resource_dir,
                    app_handle,
                    initial_backend,
                )
            })
            .expect("failed to start MoonLit recorder actor");

        Self {
            commands,
            snapshot,
            join: Mutex::new(Some(join)),
        }
    }

    #[cfg(test)]
    pub fn new_for_test(output_dir: PathBuf) -> Self {
        Self::new(output_dir, None, None)
    }

    #[cfg(test)]
    fn new_for_test_with_backend(output_dir: PathBuf, backend: Box<dyn ReplayBackend>) -> Self {
        Self::new_with_backend(output_dir, None, None, backend)
    }

    fn disconnected<T>() -> Result<T, BackendError> {
        Err(BackendError::new(
            BackendErrorCode::Internal,
            "El actor de captura ya no esta disponible",
            true,
        ))
    }

    fn request<T>(
        &self,
        command: RuntimeCommand,
        receiver: Receiver<Result<T, BackendError>>,
    ) -> Result<T, BackendError> {
        if self.commands.send(command).is_err() {
            return Self::disconnected();
        }
        receiver.recv().unwrap_or_else(|_| Self::disconnected())
    }

    pub fn snapshot(&self) -> Result<CaptureSnapshot, BackendError> {
        self.snapshot
            .lock()
            .map(|snapshot| snapshot.clone())
            .map_err(|_| {
                BackendError::new(
                    BackendErrorCode::Internal,
                    "No se pudo leer el estado de captura",
                    true,
                )
            })
    }

    pub fn list_backends(&self) -> Result<Vec<BackendDescriptor>, BackendError> {
        let (reply, receiver) = channel();
        self.request(RuntimeCommand::Backends { reply }, receiver)
    }

    pub fn list_sources(&self) -> Result<Vec<crate::traits::CaptureSource>, BackendError> {
        let (reply, receiver) = channel();
        self.request(RuntimeCommand::Sources { reply }, receiver)
    }

    pub fn select_backend(&self, id: BackendId) -> Result<CaptureSnapshot, BackendError> {
        let (reply, receiver) = channel();
        self.request(RuntimeCommand::SelectBackend { id, reply }, receiver)
    }

    pub fn start(&self, config: ReplayConfig) -> Result<CaptureSnapshot, BackendError> {
        let (reply, receiver) = channel();
        self.request(RuntimeCommand::Start { config, reply }, receiver)
    }

    pub fn save(&self) -> Result<CaptureSnapshot, BackendError> {
        let (reply, receiver) = channel();
        self.request(RuntimeCommand::Save { reply }, receiver)
    }

    pub fn stop(&self) -> Result<CaptureSnapshot, BackendError> {
        let (reply, receiver) = channel();
        self.request(RuntimeCommand::Stop { reply }, receiver)
    }
}

impl Drop for RecorderRuntime {
    fn drop(&mut self) {
        let _ = self.commands.send(RuntimeCommand::Shutdown);
        if let Ok(mut join) = self.join.lock() {
            if let Some(handle) = join.take() {
                let _ = handle.join();
            }
        }
    }
}

fn actor_loop(
    receiver: Receiver<RuntimeCommand>,
    snapshot_ref: Arc<Mutex<CaptureSnapshot>>,
    output_dir: PathBuf,
    resource_dir: Option<PathBuf>,
    app_handle: Option<AppHandle>,
    initial_backend: Box<dyn ReplayBackend>,
) {
    let mut backend = initial_backend;
    let mut snapshot = snapshot_ref
        .lock()
        .map(|state| state.clone())
        .unwrap_or_else(|_| CaptureSnapshot {
            revision: 0,
            phase: CapturePhase::Faulted,
            backend: backend.descriptor(),
            config: None,
            session: None,
            saved_clips: 0,
            last_clip: None,
            last_error: Some(BackendError::new(
                BackendErrorCode::Internal,
                "No se pudo inicializar el estado de captura",
                false,
            )),
        });

    loop {
        let command = match receiver.recv_timeout(Duration::from_millis(250)) {
            Ok(command) => command,
            Err(RecvTimeoutError::Timeout) => {
                if snapshot.phase == CapturePhase::Buffering {
                    if let Err(error) = backend.poll_health() {
                        let _ = fail_with_state(&mut snapshot, &snapshot_ref, &app_handle, error);
                    }
                }
                continue;
            }
            Err(RecvTimeoutError::Disconnected) => break,
        };
        match command {
            RuntimeCommand::Backends { reply } => {
                let _ = reply.send(Ok(backends::descriptors(resource_dir.clone())));
            }
            RuntimeCommand::Sources { reply } => {
                let _ = reply.send(backend.list_sources());
            }
            RuntimeCommand::SelectBackend { id, reply } => {
                let result = select_backend(
                    &mut backend,
                    &mut snapshot,
                    id,
                    resource_dir.clone(),
                    &snapshot_ref,
                    &app_handle,
                );
                let _ = reply.send(result);
            }
            RuntimeCommand::Start { config, reply } => {
                let result = actor_start_capture(
                    &mut backend,
                    &mut snapshot,
                    config,
                    &output_dir,
                    &snapshot_ref,
                    &app_handle,
                );
                let _ = reply.send(result);
            }
            RuntimeCommand::Save { reply } => {
                let result =
                    actor_save_clip(&mut backend, &mut snapshot, &snapshot_ref, &app_handle);
                let _ = reply.send(result);
            }
            RuntimeCommand::Stop { reply } => {
                let result =
                    actor_stop_capture(&mut backend, &mut snapshot, &snapshot_ref, &app_handle);
                let _ = reply.send(result);
            }
            RuntimeCommand::Shutdown => {
                let _ = backend.stop();
                break;
            }
        }
    }
}

fn select_backend(
    backend: &mut Box<dyn ReplayBackend>,
    snapshot: &mut CaptureSnapshot,
    id: BackendId,
    resource_dir: Option<PathBuf>,
    snapshot_ref: &Arc<Mutex<CaptureSnapshot>>,
    app_handle: &Option<AppHandle>,
) -> Result<CaptureSnapshot, BackendError> {
    if snapshot.phase != CapturePhase::Idle {
        return Err(BackendError::invalid_state(
            "Deten el buffer antes de cambiar de backend",
        ));
    }

    let candidate = backends::create(id, resource_dir)?;
    let descriptor = candidate.descriptor();
    if !descriptor.available {
        return Err(BackendError::backend_unavailable(
            descriptor
                .note
                .clone()
                .unwrap_or_else(|| "El backend no esta disponible".to_string()),
        ));
    }
    *backend = candidate;
    snapshot.backend = descriptor;
    snapshot.last_error = None;
    commit_state(snapshot, snapshot_ref, app_handle);
    Ok(snapshot.clone())
}

fn actor_start_capture(
    backend: &mut Box<dyn ReplayBackend>,
    snapshot: &mut CaptureSnapshot,
    config: ReplayConfig,
    output_dir: &Path,
    snapshot_ref: &Arc<Mutex<CaptureSnapshot>>,
    app_handle: &Option<AppHandle>,
) -> Result<CaptureSnapshot, BackendError> {
    if snapshot.phase != CapturePhase::Idle {
        return fail_with_state(
            snapshot,
            snapshot_ref,
            app_handle,
            BackendError::invalid_state("El buffer no esta en reposo"),
        );
    }
    let sources = match backend.list_sources() {
        Ok(sources) => sources,
        Err(error) => return fail_with_state(snapshot, snapshot_ref, app_handle, error),
    };
    if let Err(error) = config.validate(&sources) {
        return fail_with_state(snapshot, snapshot_ref, app_handle, error);
    }
    if !backend.descriptor().available {
        return fail_with_state(
            snapshot,
            snapshot_ref,
            app_handle,
            BackendError::backend_unavailable("El backend seleccionado no esta disponible"),
        );
    }

    snapshot.phase = CapturePhase::Starting;
    snapshot.config = Some(config.clone());
    snapshot.last_error = None;
    commit_state(snapshot, snapshot_ref, app_handle);

    if let Err(error) = backend.start(&config, output_dir) {
        return fail_with_state(snapshot, snapshot_ref, app_handle, error);
    }

    let source_label = sources
        .iter()
        .find(|source| source.id == config.source_id)
        .map(|source| source.label.clone())
        .unwrap_or_else(|| config.source_id.clone());
    snapshot.phase = CapturePhase::Buffering;
    snapshot.session = Some(SessionSnapshot {
        id: unique_id("session"),
        source_id: config.source_id,
        source_label,
        started_at_ms: now_millis(),
    });
    snapshot.last_error = None;
    commit_state(snapshot, snapshot_ref, app_handle);
    Ok(snapshot.clone())
}

fn actor_save_clip(
    backend: &mut Box<dyn ReplayBackend>,
    snapshot: &mut CaptureSnapshot,
    snapshot_ref: &Arc<Mutex<CaptureSnapshot>>,
    app_handle: &Option<AppHandle>,
) -> Result<CaptureSnapshot, BackendError> {
    if snapshot.phase != CapturePhase::Buffering {
        return fail_with_state(
            snapshot,
            snapshot_ref,
            app_handle,
            BackendError::invalid_state("Inicia el buffer antes de guardar"),
        );
    }
    snapshot.phase = CapturePhase::Saving;
    commit_state(snapshot, snapshot_ref, app_handle);

    match backend.save_replay() {
        Ok(artifact) => {
            let clip = clip_record(artifact);
            snapshot.saved_clips = snapshot.saved_clips.saturating_add(1);
            snapshot.last_clip = Some(clip.clone());
            snapshot.phase = CapturePhase::Buffering;
            snapshot.last_error = None;
            commit_state(snapshot, snapshot_ref, app_handle);
            emit_event(
                app_handle,
                RecorderEvent::ClipSaved {
                    snapshot: snapshot.clone(),
                    clip,
                },
            );
            Ok(snapshot.clone())
        }
        Err(error) => {
            if backend.poll_health().is_ok() {
                snapshot.phase = CapturePhase::Buffering;
                snapshot.last_error = Some(error.clone());
                commit_state(snapshot, snapshot_ref, app_handle);
                emit_event(
                    app_handle,
                    RecorderEvent::ErrorOccurred {
                        snapshot: snapshot.clone(),
                        error: error.clone(),
                    },
                );
                Err(error)
            } else {
                fail_with_state(snapshot, snapshot_ref, app_handle, error)
            }
        }
    }
}

fn actor_stop_capture(
    backend: &mut Box<dyn ReplayBackend>,
    snapshot: &mut CaptureSnapshot,
    snapshot_ref: &Arc<Mutex<CaptureSnapshot>>,
    app_handle: &Option<AppHandle>,
) -> Result<CaptureSnapshot, BackendError> {
    if snapshot.phase == CapturePhase::Idle {
        return Ok(snapshot.clone());
    }
    let was_faulted = snapshot.phase == CapturePhase::Faulted;
    snapshot.phase = CapturePhase::Stopping;
    commit_state(snapshot, snapshot_ref, app_handle);
    if let Err(error) = backend.stop() {
        if was_faulted {
            snapshot.phase = CapturePhase::Idle;
            snapshot.config = None;
            snapshot.session = None;
            snapshot.last_error = Some(error);
            commit_state(snapshot, snapshot_ref, app_handle);
            return Ok(snapshot.clone());
        }
        return fail_with_state(snapshot, snapshot_ref, app_handle, error);
    }
    snapshot.phase = CapturePhase::Idle;
    snapshot.config = None;
    snapshot.session = None;
    snapshot.last_error = None;
    commit_state(snapshot, snapshot_ref, app_handle);
    Ok(snapshot.clone())
}

fn fail_with_state(
    snapshot: &mut CaptureSnapshot,
    snapshot_ref: &Arc<Mutex<CaptureSnapshot>>,
    app_handle: &Option<AppHandle>,
    error: BackendError,
) -> Result<CaptureSnapshot, BackendError> {
    if matches!(
        snapshot.phase,
        CapturePhase::Starting
            | CapturePhase::Saving
            | CapturePhase::Stopping
            | CapturePhase::Buffering
    ) {
        snapshot.phase = CapturePhase::Faulted;
    }
    snapshot.last_error = Some(error.clone());
    commit_state(snapshot, snapshot_ref, app_handle);
    emit_event(
        app_handle,
        RecorderEvent::ErrorOccurred {
            snapshot: snapshot.clone(),
            error: error.clone(),
        },
    );
    Err(error)
}

fn commit_state(
    snapshot: &mut CaptureSnapshot,
    snapshot_ref: &Arc<Mutex<CaptureSnapshot>>,
    app_handle: &Option<AppHandle>,
) {
    snapshot.revision = snapshot.revision.saturating_add(1);
    if let Ok(mut state) = snapshot_ref.lock() {
        *state = snapshot.clone();
    }
    emit_event(
        app_handle,
        RecorderEvent::StateChanged {
            snapshot: snapshot.clone(),
        },
    );
}

fn emit_event(app_handle: &Option<AppHandle>, event: RecorderEvent) {
    if let Some(app_handle) = app_handle {
        let _ = app_handle.emit("moonlit://recorder", event);
    }
}

fn clip_record(artifact: ClipArtifact) -> ClipRecord {
    ClipRecord {
        id: unique_id("clip"),
        path: artifact.path.to_string_lossy().into_owned(),
        created_at_ms: now_millis(),
        duration_seconds: artifact.duration_seconds,
        kind: match artifact.kind {
            crate::traits::ClipKind::Simulation => "simulation".to_string(),
            crate::traits::ClipKind::Media => "media".to_string(),
        },
    }
}

fn now_millis() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
}

fn unique_id(prefix: &str) -> String {
    format!("{prefix}-{}", now_millis())
}

#[tauri::command]
pub fn get_capture_snapshot(
    runtime: State<'_, RecorderRuntime>,
) -> Result<CaptureSnapshot, BackendError> {
    runtime.snapshot()
}

#[tauri::command]
pub fn list_capture_backends(
    runtime: State<'_, RecorderRuntime>,
) -> Result<Vec<BackendDescriptor>, BackendError> {
    runtime.list_backends()
}

#[tauri::command]
pub fn list_capture_sources(
    runtime: State<'_, RecorderRuntime>,
) -> Result<Vec<crate::traits::CaptureSource>, BackendError> {
    runtime.list_sources()
}

#[tauri::command]
pub fn select_capture_backend(
    runtime: State<'_, RecorderRuntime>,
    backend: BackendId,
) -> Result<CaptureSnapshot, BackendError> {
    runtime.select_backend(backend)
}

#[tauri::command]
pub fn start_capture(
    runtime: State<'_, RecorderRuntime>,
    config: ReplayConfig,
) -> Result<CaptureSnapshot, BackendError> {
    runtime.start(config)
}

#[tauri::command]
pub fn save_clip(runtime: State<'_, RecorderRuntime>) -> Result<CaptureSnapshot, BackendError> {
    runtime.save()
}

#[tauri::command]
pub fn stop_capture(runtime: State<'_, RecorderRuntime>) -> Result<CaptureSnapshot, BackendError> {
    runtime.stop()
}

#[cfg(test)]
mod tests {
    use std::fs;

    use super::RecorderRuntime;
    use crate::backends::fake::FakeBackend;
    use crate::state::CapturePhase;
    use crate::traits::{BackendError, ReplayBackend, ReplayConfig};

    struct FailingBackend {
        health_fails: bool,
        active: bool,
    }

    impl ReplayBackend for FailingBackend {
        fn descriptor(&self) -> crate::traits::BackendDescriptor {
            FakeBackend::new().descriptor()
        }

        fn list_sources(&self) -> Result<Vec<crate::traits::CaptureSource>, BackendError> {
            FakeBackend::new().list_sources()
        }

        fn start(
            &mut self,
            config: &ReplayConfig,
            _output_dir: &std::path::Path,
        ) -> Result<(), BackendError> {
            config.validate(&self.list_sources()?)?;
            self.active = true;
            Ok(())
        }

        fn save_replay(&mut self) -> Result<crate::traits::ClipArtifact, BackendError> {
            if !self.active {
                return Err(BackendError::invalid_state("inactive"));
            }
            Err(BackendError::io("synthetic save failure"))
        }

        fn stop(&mut self) -> Result<(), BackendError> {
            self.active = false;
            Ok(())
        }

        fn poll_health(&mut self) -> Result<(), BackendError> {
            if self.health_fails {
                Err(BackendError::new(
                    crate::traits::BackendErrorCode::BackendExited,
                    "synthetic worker exit",
                    true,
                ))
            } else {
                Ok(())
            }
        }
    }

    fn temporary_directory() -> std::path::PathBuf {
        let path =
            std::env::temp_dir().join(format!("moonlit-runtime-{}", super::unique_id("dir")));
        fs::create_dir_all(&path).expect("temporary directory");
        path
    }

    #[test]
    fn runtime_serializes_fake_start_save_stop() {
        let directory = temporary_directory();
        let runtime = RecorderRuntime::new_for_test(directory.clone());
        let started = runtime.start(ReplayConfig::default()).expect("start");
        assert_eq!(started.phase, CapturePhase::Buffering);
        let saved = runtime.save().expect("save");
        assert_eq!(saved.saved_clips, 1);
        assert_eq!(saved.phase, CapturePhase::Buffering);
        let stopped = runtime.stop().expect("stop");
        assert_eq!(stopped.phase, CapturePhase::Idle);
        fs::remove_dir_all(directory).expect("remove temporary directory");
    }

    #[test]
    fn snapshot_is_available_without_waiting_for_actor() {
        let directory = temporary_directory();
        let runtime = RecorderRuntime::new_for_test(directory.clone());
        let snapshot = runtime.snapshot().expect("snapshot");
        assert_eq!(snapshot.phase, CapturePhase::Idle);
        fs::remove_dir_all(directory).expect("remove temporary directory");
    }

    #[test]
    fn recoverable_save_failure_keeps_buffering() {
        let directory = temporary_directory();
        let runtime = RecorderRuntime::new_for_test_with_backend(
            directory.clone(),
            Box::new(FailingBackend {
                health_fails: false,
                active: false,
            }),
        );
        runtime.start(ReplayConfig::default()).expect("start");
        assert!(runtime.save().is_err());
        assert_eq!(
            runtime.snapshot().expect("snapshot").phase,
            CapturePhase::Buffering
        );
        runtime.stop().expect("stop");
        fs::remove_dir_all(directory).expect("remove temporary directory");
    }

    #[test]
    fn dead_backend_enters_faulted_and_can_be_reset() {
        let directory = temporary_directory();
        let runtime = RecorderRuntime::new_for_test_with_backend(
            directory.clone(),
            Box::new(FailingBackend {
                health_fails: true,
                active: false,
            }),
        );
        runtime.start(ReplayConfig::default()).expect("start");
        assert!(runtime.save().is_err());
        assert_eq!(
            runtime.snapshot().expect("snapshot").phase,
            CapturePhase::Faulted
        );
        assert_eq!(runtime.stop().expect("reset").phase, CapturePhase::Idle);
        fs::remove_dir_all(directory).expect("remove temporary directory");
    }
}
