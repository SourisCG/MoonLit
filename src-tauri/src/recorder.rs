use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::mpsc::{channel, sync_channel, Receiver, RecvTimeoutError, Sender, SyncSender};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

#[cfg(not(test))]
use tauri::{AppHandle, Emitter, State};
#[cfg(test)]
type AppHandle = ();
#[cfg(not(test))]
use tauri_plugin_notification::NotificationExt;

use crate::backends;
#[cfg(test)]
use crate::backends::host_state::{
    CapturePhase, CaptureSnapshot, ClipRecord, RecorderEvent, SessionSnapshot,
};
#[cfg(not(test))]
use crate::config as config_module;
#[cfg(not(test))]
use crate::library::LibraryState;
#[cfg(not(test))]
use crate::state::{CapturePhase, CaptureSnapshot, ClipRecord, RecorderEvent, SessionSnapshot};
use crate::traits::{
    BackendDescriptor, BackendError, BackendErrorCode, BackendId, ClipArtifact, ReplayBackend,
    ReplayConfig,
};

static NEXT_ID: AtomicU64 = AtomicU64::new(0);
const ACTOR_SHUTDOWN_TIMEOUT: Duration = Duration::from_secs(2);
const ACTOR_REQUEST_TIMEOUT: Duration = Duration::from_secs(35);

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
    SetOutputDir {
        path: PathBuf,
        reply: Reply<CaptureSnapshot>,
    },
    Start {
        config: ReplayConfig,
        reply: Reply<CaptureSnapshot>,
    },
    Save {
        reply: Reply<CaptureSnapshot>,
    },
    RestoreSave {
        saved_clips: u32,
        last_clip: Option<ClipRecord>,
        error: BackendError,
        reply: Reply<CaptureSnapshot>,
    },
    ConfirmSave {
        notifications_enabled: bool,
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
    actor_done: Arc<AtomicBool>,
}

impl RecorderRuntime {
    #[cfg(test)]
    pub fn new(
        output_dir: PathBuf,
        resource_dir: Option<PathBuf>,
        app_handle: Option<AppHandle>,
    ) -> Self {
        let fake = backends::fake::FakeBackend::new();
        Self::new_with_backend(output_dir, resource_dir, app_handle, Box::new(fake))
    }

    pub(crate) fn new_with_backend(
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
            effective: None,
            can_save: false,
            session: None,
            saved_clips: 0,
            last_clip: None,
            last_error: None,
        };
        let snapshot = Arc::new(Mutex::new(initial_snapshot));
        let (commands, receiver) = sync_channel(64);
        let actor_snapshot = Arc::clone(&snapshot);
        let actor_done = Arc::new(AtomicBool::new(false));
        let actor_done_signal = Arc::clone(&actor_done);
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
                    actor_done_signal,
                )
            })
            .expect("failed to start MoonLit recorder actor");

        Self {
            commands,
            snapshot,
            join: Mutex::new(Some(join)),
            actor_done,
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
        let deadline = std::time::Instant::now() + ACTOR_REQUEST_TIMEOUT;
        let mut command = command;
        loop {
            match self.commands.try_send(command) {
                Ok(()) => break,
                Err(std::sync::mpsc::TrySendError::Disconnected(_)) => return Self::disconnected(),
                Err(std::sync::mpsc::TrySendError::Full(next)) => {
                    if std::time::Instant::now() >= deadline {
                        return Err(BackendError::new(
                            BackendErrorCode::Internal,
                            "El actor de captura excedio el tiempo limite",
                            true,
                        ));
                    }
                    command = next;
                    thread::sleep(
                        deadline
                            .saturating_duration_since(std::time::Instant::now())
                            .min(Duration::from_millis(10)),
                    );
                }
            }
        }
        match receiver.recv_timeout(deadline.saturating_duration_since(std::time::Instant::now())) {
            Ok(result) => result,
            Err(std::sync::mpsc::RecvTimeoutError::Timeout) => Err(BackendError::new(
                BackendErrorCode::Internal,
                "El actor de captura excedio el tiempo limite",
                true,
            )),
            Err(std::sync::mpsc::RecvTimeoutError::Disconnected) => Self::disconnected(),
        }
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

    pub fn set_output_dir(&self, path: PathBuf) -> Result<CaptureSnapshot, BackendError> {
        let (reply, receiver) = channel();
        self.request(RuntimeCommand::SetOutputDir { path, reply }, receiver)
    }

    pub fn start(&self, config: ReplayConfig) -> Result<CaptureSnapshot, BackendError> {
        let (reply, receiver) = channel();
        self.request(RuntimeCommand::Start { config, reply }, receiver)
    }

    pub fn save(&self) -> Result<CaptureSnapshot, BackendError> {
        let (reply, receiver) = channel();
        self.request(RuntimeCommand::Save { reply }, receiver)
    }

    fn restore_failed_save(
        &self,
        previous: &CaptureSnapshot,
        error: BackendError,
    ) -> Result<CaptureSnapshot, BackendError> {
        let (reply, receiver) = channel();
        self.request(
            RuntimeCommand::RestoreSave {
                saved_clips: previous.saved_clips,
                last_clip: previous.last_clip.clone(),
                error,
                reply,
            },
            receiver,
        )
    }

    fn confirm_save(&self, notifications_enabled: bool) -> Result<CaptureSnapshot, BackendError> {
        let (reply, receiver) = channel();
        self.request(
            RuntimeCommand::ConfirmSave {
                notifications_enabled,
                reply,
            },
            receiver,
        )
    }

    pub fn stop(&self) -> Result<CaptureSnapshot, BackendError> {
        let (reply, receiver) = channel();
        self.request(RuntimeCommand::Stop { reply }, receiver)
    }
}

impl Drop for RecorderRuntime {
    fn drop(&mut self) {
        // SyncSender::send can block forever when a backend is stuck and the
        // queue is full. Queue shutdown opportunistically and never make
        // application teardown depend on command capacity or backend code.
        let _ = self.commands.try_send(RuntimeCommand::Shutdown);
        if let Ok(mut join) = self.join.lock() {
            if let Some(handle) = join.take() {
                let deadline = std::time::Instant::now() + ACTOR_SHUTDOWN_TIMEOUT;
                while !self.actor_done.load(Ordering::Acquire)
                    && !handle.is_finished()
                    && std::time::Instant::now() < deadline
                {
                    thread::sleep(Duration::from_millis(10));
                }
                if self.actor_done.load(Ordering::Acquire) || handle.is_finished() {
                    let _ = handle.join();
                }
                // Dropping an unfinished JoinHandle detaches the actor. A
                // backend must own its own kill/cancellation policy; the host
                // cannot wait indefinitely for an implementation defect.
            }
        }
    }
}

fn actor_loop(
    receiver: Receiver<RuntimeCommand>,
    snapshot_ref: Arc<Mutex<CaptureSnapshot>>,
    mut output_dir: PathBuf,
    resource_dir: Option<PathBuf>,
    app_handle: Option<AppHandle>,
    initial_backend: Box<dyn ReplayBackend>,
    actor_done: Arc<AtomicBool>,
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
            effective: None,
            can_save: false,
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
                    } else if snapshot.can_save != backend.can_save() {
                        snapshot.can_save = backend.can_save();
                        commit_state(&mut snapshot, &snapshot_ref, &app_handle);
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
            RuntimeCommand::SetOutputDir { path, reply } => {
                let result = if snapshot.phase != CapturePhase::Idle {
                    Err(BackendError::invalid_state(
                        "Deten el buffer antes de cambiar la carpeta",
                    ))
                } else if let Err(error) = std::fs::create_dir_all(&path) {
                    Err(BackendError::io(error.to_string()))
                } else {
                    output_dir = path;
                    Ok(snapshot.clone())
                };
                let _ = reply.send(result);
            }
            RuntimeCommand::Save { reply } => {
                let result = actor_save_clip(
                    &mut backend,
                    &mut snapshot,
                    &output_dir,
                    &snapshot_ref,
                    &app_handle,
                );
                let _ = reply.send(result);
            }
            RuntimeCommand::RestoreSave {
                saved_clips,
                last_clip,
                error,
                reply,
            } => {
                let result = actor_restore_failed_save(
                    &mut snapshot,
                    saved_clips,
                    last_clip,
                    error,
                    &snapshot_ref,
                    &app_handle,
                );
                let _ = reply.send(result);
            }
            RuntimeCommand::ConfirmSave {
                notifications_enabled,
                reply,
            } => {
                let result = actor_confirm_save(
                    &mut snapshot,
                    notifications_enabled,
                    &snapshot_ref,
                    &app_handle,
                );
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
    actor_done.store(true, Ordering::Release);
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
    snapshot.effective = None;
    snapshot.can_save = false;
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

    snapshot.effective = backend.effective_settings();
    snapshot.can_save = backend.can_save();

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
    output_dir: &Path,
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
    if !backend.can_save() {
        return fail_with_state(
            snapshot,
            snapshot_ref,
            app_handle,
            BackendError::invalid_state("El buffer aun no tiene un keyframe guardable"),
        );
    }
    snapshot.phase = CapturePhase::Saving;
    commit_state(snapshot, snapshot_ref, app_handle);

    match backend.save_replay() {
        Ok(artifact) => match clip_record(artifact, output_dir) {
            Ok(clip) => {
                snapshot.saved_clips = snapshot.saved_clips.saturating_add(1);
                snapshot.last_clip = Some(clip);
                snapshot.phase = CapturePhase::Buffering;
                snapshot.last_error = None;
                commit_state(snapshot, snapshot_ref, app_handle);
                // ClipSaved and its notification are deliberately deferred
                // until save_clip has indexed this record in SQLite.
                Ok(snapshot.clone())
            }
            Err(error) => recover_save_error(backend, snapshot, snapshot_ref, app_handle, error),
        },
        Err(error) => recover_save_error(backend, snapshot, snapshot_ref, app_handle, error),
    }
}

fn recover_save_error(
    backend: &mut Box<dyn ReplayBackend>,
    snapshot: &mut CaptureSnapshot,
    snapshot_ref: &Arc<Mutex<CaptureSnapshot>>,
    app_handle: &Option<AppHandle>,
    error: BackendError,
) -> Result<CaptureSnapshot, BackendError> {
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

fn actor_restore_failed_save(
    snapshot: &mut CaptureSnapshot,
    saved_clips: u32,
    last_clip: Option<ClipRecord>,
    error: BackendError,
    snapshot_ref: &Arc<Mutex<CaptureSnapshot>>,
    app_handle: &Option<AppHandle>,
) -> Result<CaptureSnapshot, BackendError> {
    if snapshot.phase != CapturePhase::Buffering {
        return Err(BackendError::invalid_state(
            "No se puede revertir un guardado fuera del buffer",
        ));
    }
    snapshot.saved_clips = saved_clips;
    snapshot.last_clip = last_clip;
    snapshot.last_error = Some(error.clone());
    commit_state(snapshot, snapshot_ref, app_handle);
    emit_event(
        app_handle,
        RecorderEvent::ErrorOccurred {
            snapshot: snapshot.clone(),
            error: error.clone(),
        },
    );
    Ok(snapshot.clone())
}

fn actor_confirm_save(
    snapshot: &mut CaptureSnapshot,
    notifications_enabled: bool,
    _snapshot_ref: &Arc<Mutex<CaptureSnapshot>>,
    app_handle: &Option<AppHandle>,
) -> Result<CaptureSnapshot, BackendError> {
    let clip = snapshot
        .last_clip
        .clone()
        .ok_or_else(|| BackendError::invalid_state("No hay un clip pendiente de confirmar"))?;
    emit_event(
        app_handle,
        RecorderEvent::ClipSaved {
            snapshot: snapshot.clone(),
            clip,
        },
    );
    #[cfg(not(test))]
    if notifications_enabled {
        if let Some(app_handle) = app_handle {
            let _ = app_handle
                .notification()
                .builder()
                .title("MoonLit")
                .body("Clip guardado en tu biblioteca")
                .show();
        }
    }
    #[cfg(test)]
    let _ = notifications_enabled;
    Ok(snapshot.clone())
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
    snapshot.effective = None;
    snapshot.can_save = false;
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

#[cfg(not(test))]
fn emit_event(app_handle: &Option<AppHandle>, event: RecorderEvent) {
    if let Some(app_handle) = app_handle {
        let _ = app_handle.emit("moonlit://recorder", event);
    }
}

#[cfg(test)]
fn emit_event(_app_handle: &Option<AppHandle>, _event: RecorderEvent) {}

fn clip_record(artifact: ClipArtifact, output_dir: &Path) -> Result<ClipRecord, BackendError> {
    let path = finalized_artifact_path(&artifact.path, output_dir)?;
    let size_bytes = fs::metadata(&path)
        .map_err(|error| BackendError::io(format!("No se pudo leer el clip final: {error}")))?
        .len();
    Ok(ClipRecord {
        id: unique_id("clip"),
        path: path.to_string_lossy().into_owned(),
        created_at_ms: now_millis(),
        duration_seconds: artifact.duration_seconds,
        kind: match artifact.kind {
            crate::traits::ClipKind::Simulation => "simulation".to_string(),
            crate::traits::ClipKind::Media => "media".to_string(),
        },
        size_bytes,
        codec: match artifact.codec {
            crate::traits::VideoCodec::H264 => "h264".to_string(),
            crate::traits::VideoCodec::Hevc => "hevc".to_string(),
        },
        format: match artifact.format {
            crate::traits::ContainerFormat::Mp4 => "mp4".to_string(),
            crate::traits::ContainerFormat::Mkv => "mkv".to_string(),
        },
        width: artifact.width,
        height: artifact.height,
        fps: artifact.fps,
        has_audio: artifact.has_audio,
        proxy_path: None,
        proxy_status: "notNeeded".to_string(),
    })
}

fn finalized_artifact_path(path: &Path, output_dir: &Path) -> Result<PathBuf, BackendError> {
    if !path.is_absolute() || !output_dir.is_absolute() {
        return Err(BackendError::io("El clip final debe usar rutas absolutas"));
    }
    ensure_no_reparse_components(path).map_err(BackendError::io)?;
    ensure_no_reparse_components(output_dir).map_err(BackendError::io)?;
    let metadata = fs::symlink_metadata(path)
        .map_err(|error| BackendError::io(format!("No se encontro el clip final: {error}")))?;
    if unsafe_metadata(&metadata) || !metadata.file_type().is_file() {
        return Err(BackendError::io(
            "El backend no produjo un archivo final regular",
        ));
    }
    let canonical_path = fs::canonicalize(path)
        .map_err(|error| BackendError::io(format!("No se pudo finalizar el clip: {error}")))?;
    let canonical_root = fs::canonicalize(output_dir).map_err(|error| {
        BackendError::io(format!("No se pudo validar la carpeta de clips: {error}"))
    })?;
    if canonical_path == canonical_root || !canonical_path.starts_with(&canonical_root) {
        return Err(BackendError::io(
            "El clip final esta fuera de la carpeta de salida",
        ));
    }
    Ok(canonical_path)
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
        use std::os::windows::fs::MetadataExt;

        metadata.file_type().is_symlink() || metadata.file_attributes() & 0x0000_0400 != 0
    }
    #[cfg(not(windows))]
    {
        metadata.file_type().is_symlink()
    }
}

fn now_millis() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
}

fn unique_id(prefix: &str) -> String {
    format!(
        "{prefix}-{}-{}",
        now_millis(),
        NEXT_ID.fetch_add(1, Ordering::Relaxed)
    )
}

#[cfg(not(test))]
#[tauri::command]
pub fn get_capture_snapshot(
    runtime: State<'_, RecorderRuntime>,
) -> Result<CaptureSnapshot, BackendError> {
    runtime.snapshot()
}

#[cfg(not(test))]
#[tauri::command]
pub fn list_capture_backends(
    runtime: State<'_, RecorderRuntime>,
) -> Result<Vec<BackendDescriptor>, BackendError> {
    runtime.list_backends()
}

#[cfg(not(test))]
#[tauri::command]
pub fn list_capture_sources(
    runtime: State<'_, RecorderRuntime>,
) -> Result<Vec<crate::traits::CaptureSource>, BackendError> {
    runtime.list_sources()
}

#[cfg(not(test))]
#[tauri::command]
pub fn select_capture_backend(
    runtime: State<'_, RecorderRuntime>,
    backend: BackendId,
) -> Result<CaptureSnapshot, BackendError> {
    runtime.select_backend(backend)
}

#[cfg(not(test))]
#[tauri::command]
pub fn set_capture_output_dir(
    runtime: State<'_, RecorderRuntime>,
    path: PathBuf,
) -> Result<CaptureSnapshot, BackendError> {
    runtime.set_output_dir(path)
}

#[cfg(not(test))]
#[tauri::command]
pub fn start_capture(
    runtime: State<'_, RecorderRuntime>,
    config: ReplayConfig,
) -> Result<CaptureSnapshot, BackendError> {
    runtime.start(config)
}

#[cfg(not(test))]
#[tauri::command]
pub fn save_clip(
    runtime: State<'_, RecorderRuntime>,
    library: State<'_, LibraryState>,
    config: State<'_, config_module::ConfigState>,
) -> Result<CaptureSnapshot, BackendError> {
    let previous = runtime.snapshot()?;
    let snapshot = runtime.save()?;
    let clip = match snapshot.last_clip.clone() {
        Some(clip) => clip,
        None => {
            let error = BackendError::io("El recorder no devolvio un clip final para indexar");
            let _ = runtime.restore_failed_save(&previous, error.clone());
            return Err(error);
        }
    };
    let insert_result = match library.0.lock() {
        Ok(store) => {
            let result = store.insert_record(&clip);
            if result.is_err() {
                let _ = store.remove_unindexed_file(Path::new(&clip.path));
            }
            result
        }
        Err(_) => Err("La biblioteca esta bloqueada".to_string()),
    };
    if let Err(error) = insert_result {
        let rollback_error = BackendError::io(error);
        let _ = runtime.restore_failed_save(&previous, rollback_error.clone());
        return Err(rollback_error);
    }

    // A notification is policy-controlled and is only emitted by
    // confirm_save, after the SQLite insert above succeeded.
    let notifications_enabled = config
        .0
        .lock()
        .ok()
        .and_then(|store| store.load().ok())
        .is_some_and(|value| value.notifications_enabled);
    runtime.confirm_save(notifications_enabled)
}

#[cfg(not(test))]
#[tauri::command]
pub fn stop_capture(runtime: State<'_, RecorderRuntime>) -> Result<CaptureSnapshot, BackendError> {
    runtime.stop()
}

#[cfg(test)]
mod tests {
    use std::fs;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;
    use std::time::{Duration, Instant};

    use super::CapturePhase;
    use super::RecorderRuntime;
    use crate::backends::fake::FakeBackend;
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

    struct HangingStopBackend {
        release: Arc<AtomicBool>,
        active: bool,
    }

    impl ReplayBackend for HangingStopBackend {
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
            Err(BackendError::invalid_state("synthetic save is unused"))
        }

        fn stop(&mut self) -> Result<(), BackendError> {
            while !self.release.load(Ordering::Acquire) {
                std::thread::sleep(Duration::from_millis(10));
            }
            self.active = false;
            Ok(())
        }

        fn poll_health(&mut self) -> Result<(), BackendError> {
            Ok(())
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
        drop(runtime);
        fs::remove_dir_all(directory).expect("remove temporary directory");
    }

    #[test]
    fn snapshot_is_available_without_waiting_for_actor() {
        let directory = temporary_directory();
        let runtime = RecorderRuntime::new_for_test(directory.clone());
        let snapshot = runtime.snapshot().expect("snapshot");
        assert_eq!(snapshot.phase, CapturePhase::Idle);
        drop(runtime);
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
        drop(runtime);
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
        drop(runtime);
        fs::remove_dir_all(directory).expect("remove temporary directory");
    }

    #[test]
    fn actor_shutdown_does_not_wait_forever_for_a_hung_backend() {
        let directory = temporary_directory();
        let release = Arc::new(AtomicBool::new(false));
        let runtime = RecorderRuntime::new_for_test_with_backend(
            directory.clone(),
            Box::new(HangingStopBackend {
                release: Arc::clone(&release),
                active: false,
            }),
        );
        runtime.start(ReplayConfig::default()).expect("start");

        let started = Instant::now();
        drop(runtime);
        assert!(started.elapsed() < Duration::from_secs(4));
        release.store(true, Ordering::Release);
        fs::remove_dir_all(directory).expect("remove temporary directory");
    }
}
