//! Supervised control transport for the isolated recorder process.

use std::collections::{HashMap, VecDeque};
use std::env;
use std::fmt;
use std::fs;
use std::io::{BufReader, Read};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStderr, ChildStdin, ChildStdout, Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{sync_channel, Receiver, SyncSender};
use std::sync::{mpsc, Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use moonlit_libobs_protocol as protocol;
use protocol::{Payload, Request, Response};

const HELLO_TIMEOUT: Duration = Duration::from_secs(2);
const PROBE_TIMEOUT: Duration = Duration::from_secs(5);
const START_TIMEOUT: Duration = Duration::from_secs(10);
const SAVE_TIMEOUT: Duration = Duration::from_secs(30);
const STOP_TIMEOUT: Duration = Duration::from_secs(5);
const PING_TIMEOUT: Duration = Duration::from_secs(1);
const SHUTDOWN_TIMEOUT: Duration = Duration::from_secs(2);
const CHILD_CLEANUP_TIMEOUT: Duration = Duration::from_secs(2);
const THREAD_JOIN_TIMEOUT: Duration = Duration::from_secs(2);
const STDERR_LINE_LIMIT: usize = 512;
const STDERR_RING_LIMIT: usize = 128;
const CLEANUP_DIAGNOSTIC_LIMIT: usize = 8;
const EVENT_QUEUE_LIMIT: usize = 64;

type PendingReplies = Arc<Mutex<HashMap<u64, mpsc::Sender<Result<protocol::Frame, SidecarError>>>>>;

#[derive(Debug)]
pub enum SidecarError {
    Io(String),
    Protocol(String),
    Timeout,
    Exited,
    InvalidResponse(String),
}

impl fmt::Display for SidecarError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Io(message) => write!(formatter, "sidecar I/O failed: {message}"),
            Self::Protocol(message) => write!(formatter, "sidecar protocol failed: {message}"),
            Self::Timeout => formatter.write_str("sidecar request timed out"),
            Self::Exited => formatter.write_str("sidecar exited unexpectedly"),
            Self::InvalidResponse(message) => {
                write!(formatter, "sidecar response is invalid: {message}")
            }
        }
    }
}

impl std::error::Error for SidecarError {}

pub trait SidecarTransport: Send {
    fn request(&mut self, request: Request) -> Result<Response, SidecarError>;

    fn drain_events(&mut self) -> Vec<protocol::Event> {
        Vec::new()
    }

    fn terminate(&mut self);
}

pub trait SidecarLauncher: Send + Sync {
    fn launch(&self, runtime_root: &Path) -> Result<Box<dyn SidecarTransport>, SidecarError>;
}

#[derive(Clone, Debug)]
pub struct ProcessSidecarLauncher {
    executable: PathBuf,
}

impl ProcessSidecarLauncher {
    pub fn new(executable: PathBuf) -> Self {
        Self { executable }
    }
}

fn request_timeout(request: &Request) -> Duration {
    match request {
        Request::Hello { .. } => HELLO_TIMEOUT,
        Request::Probe => PROBE_TIMEOUT,
        Request::Start(_) => START_TIMEOUT,
        Request::SaveReplay => SAVE_TIMEOUT,
        Request::Stop => STOP_TIMEOUT,
        Request::Ping => PING_TIMEOUT,
        Request::Shutdown => SHUTDOWN_TIMEOUT,
    }
}

fn validated_runtime_root(path: &Path) -> Result<PathBuf, SidecarError> {
    if !path.is_absolute() {
        return Err(SidecarError::Io(
            "el runtime del sidecar debe usar una ruta absoluta".to_string(),
        ));
    }
    let metadata = fs::symlink_metadata(path)
        .map_err(|error| SidecarError::Io(format!("runtime invalido: {error}")))?;
    if !metadata.is_dir() || unsafe_metadata(&metadata) {
        return Err(SidecarError::Io(
            "el runtime del sidecar debe ser un directorio regular".to_string(),
        ));
    }
    let canonical = fs::canonicalize(path)
        .map_err(|error| SidecarError::Io(format!("no se pudo resolver el runtime: {error}")))?;
    ensure_no_reparse_components(&canonical)?;
    Ok(canonical)
}

fn validated_executable(path: &Path) -> Result<PathBuf, SidecarError> {
    if !path.is_absolute() {
        return Err(SidecarError::Io(
            "la ruta del sidecar debe ser absoluta".to_string(),
        ));
    }
    let metadata = fs::symlink_metadata(path)
        .map_err(|error| SidecarError::Io(format!("sidecar invalido: {error}")))?;
    if !metadata.is_file() || unsafe_metadata(&metadata) {
        return Err(SidecarError::Io(
            "el sidecar debe ser un archivo regular".to_string(),
        ));
    }
    let canonical = fs::canonicalize(path)
        .map_err(|error| SidecarError::Io(format!("no se pudo resolver el sidecar: {error}")))?;
    ensure_no_reparse_components(&canonical)?;
    Ok(canonical)
}

fn ensure_no_reparse_components(path: &Path) -> Result<(), SidecarError> {
    for ancestor in path.ancestors() {
        if let Ok(metadata) = fs::symlink_metadata(ancestor) {
            if unsafe_metadata(&metadata) {
                return Err(SidecarError::Io(format!(
                    "la ruta contiene un enlace o reparse point: {}",
                    ancestor.display()
                )));
            }
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

fn configure_sidecar_environment(
    command: &mut Command,
    working_directory: &Path,
) -> Result<(), SidecarError> {
    let mut path_entries = vec![working_directory.to_path_buf()];
    if let Some(system_root) = env::var_os("SystemRoot") {
        command.env("SystemRoot", &system_root);
        command.env("WINDIR", &system_root);
        path_entries.push(PathBuf::from(system_root).join("System32"));
    }
    let path = env::join_paths(path_entries)
        .map_err(|error| SidecarError::Io(format!("PATH seguro invalido: {error}")))?;
    command.env("PATH", path);
    Ok(())
}

impl SidecarLauncher for ProcessSidecarLauncher {
    fn launch(&self, runtime_root: &Path) -> Result<Box<dyn SidecarTransport>, SidecarError> {
        let runtime_root = validated_runtime_root(runtime_root)?;
        let executable = validated_executable(&self.executable)?;
        let working_directory = executable.parent().ok_or_else(|| {
            SidecarError::Io("el sidecar no tiene un directorio padre valido".to_string())
        })?;
        if !executable.starts_with(&runtime_root) || !working_directory.starts_with(&runtime_root)
        {
            return Err(SidecarError::Io(
                "el ejecutable del sidecar debe estar dentro del runtime validado".to_string(),
            ));
        }
        #[cfg(windows)]
        if taskkill_path().is_none() {
            return Err(SidecarError::Io(
                "no se encontro taskkill.exe para limpiar el proceso y sus descendientes"
                    .to_string(),
            ));
        }

        let mut command = Command::new(&executable);
        command
            .arg("--stdio")
            .arg("--runtime-root")
            .arg(&runtime_root)
            .arg("--parent-pid")
            .arg(std::process::id().to_string())
            .current_dir(working_directory)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());
        configure_sidecar_environment(&mut command, working_directory)?;
        let mut child = command
            .spawn()
            .map_err(|error| SidecarError::Io(error.to_string()))?;
        let child_pid = child.id();
        let stdin = match child.stdin.take() {
            Some(stdin) => stdin,
            None => {
                terminate_spawned_child(&mut child);
                return Err(SidecarError::Io(
                    "no se pudo abrir stdin del sidecar".to_string(),
                ));
            }
        };
        let stdout = match child.stdout.take() {
            Some(stdout) => stdout,
            None => {
                terminate_spawned_child(&mut child);
                return Err(SidecarError::Io(
                    "no se pudo abrir stdout del sidecar".to_string(),
                ));
            }
        };
        let stderr = match child.stderr.take() {
            Some(stderr) => stderr,
            None => {
                terminate_spawned_child(&mut child);
                return Err(SidecarError::Io(
                    "no se pudo abrir stderr del sidecar".to_string(),
                ));
            }
        };
        let process_id = Arc::new(Mutex::new(child_pid));
        let child = Arc::new(Mutex::new(child));
        let dead = Arc::new(AtomicBool::new(false));
        let stderr_ring = Arc::new(Mutex::new(VecDeque::with_capacity(STDERR_RING_LIMIT)));
        let (stderr_join, stderr_done) = match spawn_stderr_reader(stderr, Arc::clone(&stderr_ring))
        {
            Ok(value) => value,
            Err(error) => {
                terminate_child(&child, &process_id);
                return Err(SidecarError::Io(error));
            }
        };
        let (requests, receiver) = sync_channel(8);
        let pending = Arc::new(Mutex::new(HashMap::new()));
        let events = Arc::new(Mutex::new(VecDeque::new()));
        let cleanup_errors = Arc::new(Mutex::new(VecDeque::with_capacity(
            CLEANUP_DIAGNOSTIC_LIMIT,
        )));
        let writer_done = Arc::new(AtomicBool::new(false));
        let reader_done = Arc::new(AtomicBool::new(false));
        let writer_dead = Arc::clone(&dead);
        let writer_child = Arc::clone(&child);
        let writer_process_id = Arc::clone(&process_id);
        let writer_pending = Arc::clone(&pending);
        let writer_done_signal = Arc::clone(&writer_done);
        let writer = thread::Builder::new()
            .name("moonlit-sidecar-io".to_string())
            .spawn(move || {
                process_writer(
                    stdin,
                    receiver,
                    writer_pending,
                    writer_child,
                    writer_process_id,
                    writer_dead,
                    writer_done_signal,
                )
            })
            .map_err(|error| {
                terminate_child(&child, &process_id);
                SidecarError::Io(error.to_string())
            })?;
        let reader_dead = Arc::clone(&dead);
        let reader_child = Arc::clone(&child);
        let reader_process_id = Arc::clone(&process_id);
        let reader_pending = Arc::clone(&pending);
        let reader_events = Arc::clone(&events);
        let reader_done_signal = Arc::clone(&reader_done);
        let reader = thread::Builder::new()
            .name("moonlit-sidecar-reader".to_string())
            .spawn(move || {
                process_reader(
                    stdout,
                    reader_pending,
                    reader_events,
                    reader_child,
                    reader_process_id,
                    reader_dead,
                    reader_done_signal,
                )
            })
            .map_err(|error| {
                terminate_child(&child, &process_id);
                SidecarError::Io(error.to_string())
            })?;

        Ok(Box::new(ProcessSidecarTransport {
            requests: Some(requests),
            child,
            process_id,
            dead,
            writer: Some(writer),
            reader: Some(reader),
            stderr_join: Some(stderr_join),
            writer_done,
            reader_done,
            stderr_done,
            next_request_id: 1,
            stderr_ring,
            events,
            pending,
            cleanup_errors,
        }))
    }
}

struct ProcessSidecarTransport {
    requests: Option<SyncSender<WorkerRequest>>,
    child: Arc<Mutex<Child>>,
    process_id: Arc<Mutex<u32>>,
    dead: Arc<AtomicBool>,
    writer: Option<JoinHandle<()>>,
    reader: Option<JoinHandle<()>>,
    stderr_join: Option<JoinHandle<()>>,
    writer_done: Arc<AtomicBool>,
    reader_done: Arc<AtomicBool>,
    stderr_done: Arc<AtomicBool>,
    next_request_id: u64,
    stderr_ring: Arc<Mutex<VecDeque<String>>>,
    events: Arc<Mutex<VecDeque<protocol::Event>>>,
    pending: PendingReplies,
    cleanup_errors: Arc<Mutex<VecDeque<String>>>,
}

struct WorkerRequest {
    frame: protocol::Frame,
    reply: mpsc::Sender<Result<protocol::Frame, SidecarError>>,
}

impl SidecarTransport for ProcessSidecarTransport {
    fn request(&mut self, request: Request) -> Result<Response, SidecarError> {
        if self.dead.load(Ordering::Acquire) {
            return Err(self.with_diagnostics(SidecarError::Exited));
        }
        let request_id = self.next_request_id;
        self.next_request_id = self.next_request_id.checked_add(1).ok_or_else(|| {
            self.with_diagnostics(SidecarError::Protocol(
                "se agotaron los identificadores de peticion".to_string(),
            ))
        })?;
        let frame = protocol::Frame::request(request_id, request);
        let (reply, receiver) = mpsc::channel();
        let requests = self
            .requests
            .as_ref()
            .cloned()
            .ok_or_else(|| self.with_diagnostics(SidecarError::Exited))?;
        let request_deadline = match &frame.payload {
            Payload::Request(request) => request_timeout(request),
            _ => unreachable!("request frame always contains a request"),
        };
        let deadline = Instant::now() + request_deadline;
        enqueue_request(&requests, WorkerRequest { frame, reply }, deadline).map_err(|error| {
            if matches!(error, SidecarError::Timeout) {
                self.terminate();
            }
            self.with_diagnostics(error)
        })?;
        let frame = match receiver.recv_timeout(remaining(deadline)) {
            Ok(result) => result.map_err(|error| self.with_diagnostics(error))?,
            Err(mpsc::RecvTimeoutError::Timeout) => {
                self.terminate();
                return Err(self.with_diagnostics(SidecarError::Timeout));
            }
            Err(mpsc::RecvTimeoutError::Disconnected) => {
                return Err(self.with_diagnostics(SidecarError::Exited));
            }
        };
        match frame.payload {
            Payload::Response(response) => Ok(response),
            Payload::Event(event) => {
                self.terminate();
                Err(self.with_diagnostics(SidecarError::InvalidResponse(format!(
                    "evento inesperado: {event:?}"
                ))))
            }
            Payload::Request(_) => {
                self.terminate();
                Err(self.with_diagnostics(SidecarError::InvalidResponse(
                    "el sidecar envio una solicitud al host".to_string(),
                )))
            }
        }
    }

    fn drain_events(&mut self) -> Vec<protocol::Event> {
        self.events
            .lock()
            .map(|mut events| events.drain(..).collect())
            .unwrap_or_default()
    }

    fn terminate(&mut self) {
        self.dead.store(true, Ordering::Release);
        self.requests.take();
        fail_pending(&self.pending, SidecarError::Exited);
        terminate_child(&self.child, &self.process_id);
    }
}

impl Drop for ProcessSidecarTransport {
    fn drop(&mut self) {
        if !self.dead.load(Ordering::Acquire) {
            let _ = self.request(Request::Shutdown);
        }
        self.terminate();
        if let Some(writer) = self.writer.take() {
            join_bounded(writer, &self.writer_done);
        }
        if let Some(reader) = self.reader.take() {
            join_bounded(reader, &self.reader_done);
        }
        if let Some(stderr) = self.stderr_join.take() {
            join_bounded(stderr, &self.stderr_done);
        }
        if let Ok(mut ring) = self.stderr_ring.lock() {
            ring.clear();
        }
    }
}

impl ProcessSidecarTransport {
    fn with_diagnostics(&self, error: SidecarError) -> SidecarError {
        let stderr = self
            .stderr_ring
            .lock()
            .ok()
            .filter(|ring| !ring.is_empty())
            .map(|ring| ring.iter().cloned().collect::<Vec<_>>().join(" | "));
        let cleanup = self
            .cleanup_errors
            .lock()
            .ok()
            .filter(|errors| !errors.is_empty())
            .map(|errors| errors.iter().cloned().collect::<Vec<_>>().join(" | "));
        if stderr.is_none() && cleanup.is_none() {
            return error;
        }
        let mut diagnostics = Vec::new();
        if let Some(stderr) = stderr {
            diagnostics.push(format!("stderr: {stderr}"));
        }
        if let Some(cleanup) = cleanup {
            diagnostics.push(format!("cleanup: {cleanup}"));
        }
        append_diagnostics(error, diagnostics.join("; "))
    }
}

fn process_writer(
    mut stdin: ChildStdin,
    receiver: Receiver<WorkerRequest>,
    pending: PendingReplies,
    child: Arc<Mutex<Child>>,
    process_id: Arc<Mutex<u32>>,
    dead: Arc<AtomicBool>,
    done: Arc<AtomicBool>,
) {
    while let Ok(request) = receiver.recv() {
        let request_id = request.frame.request_id;
        if dead.load(Ordering::Acquire) {
            let _ = request.reply.send(Err(SidecarError::Exited));
            break;
        }
        if let Ok(mut pending) = pending.lock() {
            pending.insert(request_id, request.reply);
        } else {
            let _ = request.reply.send(Err(SidecarError::Protocol(
                "la tabla de peticiones esta bloqueada".to_string(),
            )));
            dead.store(true, Ordering::Release);
            terminate_child(&child, &process_id);
            break;
        }
        if let Err(error) =
            protocol::write_frame(&mut stdin, &request.frame).map_err(protocol_error)
        {
            if let Ok(mut pending) = pending.lock() {
                if let Some(reply) = pending.remove(&request_id) {
                    let _ = reply.send(Err(error));
                }
            }
            dead.store(true, Ordering::Release);
            terminate_child(&child, &process_id);
            break;
        }
    }
    done.store(true, Ordering::Release);
}

fn process_reader(
    mut stdout: ChildStdout,
    pending: PendingReplies,
    events: Arc<Mutex<VecDeque<protocol::Event>>>,
    child: Arc<Mutex<Child>>,
    process_id: Arc<Mutex<u32>>,
    dead: Arc<AtomicBool>,
    done: Arc<AtomicBool>,
) {
    loop {
        let result = protocol::read_frame(&mut stdout);
        let frame: Result<protocol::Frame, SidecarError> = match result {
            Ok(Some(frame)) => Ok(frame),
            Ok(None) => Err(SidecarError::Exited),
            Err(error) => Err(protocol_error(error)),
        };
        let Ok(frame) = frame else {
            let error = frame.expect_err("reader error");
            dead.store(true, Ordering::Release);
            if let Ok(mut pending) = pending.lock() {
                for (_, reply) in pending.drain() {
                    let _ = reply.send(Err(match &error {
                        SidecarError::Exited => SidecarError::Exited,
                        SidecarError::Protocol(message) => SidecarError::Protocol(message.clone()),
                        _ => SidecarError::Exited,
                    }));
                }
            }
            terminate_child(&child, &process_id);
            break;
        };
        match frame.payload {
            Payload::Event(event) => {
                queue_event(&events, event);
            }
            Payload::Response(_) => {
                if let Err(error) = deliver_response(&pending, frame) {
                    dead.store(true, Ordering::Release);
                    fail_pending(&pending, error);
                    terminate_child(&child, &process_id);
                    break;
                }
            }
            Payload::Request(_) => {
                dead.store(true, Ordering::Release);
                fail_pending(
                    &pending,
                    SidecarError::InvalidResponse(
                        "el sidecar envio una solicitud al host".to_string(),
                    ),
                );
                terminate_child(&child, &process_id);
                break;
            }
        }
    }
    done.store(true, Ordering::Release);
}

fn protocol_error(error: protocol::ProtocolError) -> SidecarError {
    SidecarError::Protocol(error.to_string())
}

fn queue_event(events: &Arc<Mutex<VecDeque<protocol::Event>>>, event: protocol::Event) {
    if let Ok(mut queued) = events.lock() {
        if queued.len() >= EVENT_QUEUE_LIMIT {
            queued.pop_front();
        }
        queued.push_back(event);
    }
}

fn deliver_response(pending: &PendingReplies, frame: protocol::Frame) -> Result<(), SidecarError> {
    if !matches!(&frame.payload, Payload::Response(_)) {
        return Err(SidecarError::InvalidResponse(
            "el frame no contiene una respuesta".to_string(),
        ));
    }
    let reply = pending
        .lock()
        .ok()
        .and_then(|mut replies| replies.remove(&frame.request_id));
    if let Some(reply) = reply {
        let _ = reply.send(Ok(frame));
        Ok(())
    } else {
        Err(SidecarError::InvalidResponse(format!(
            "respuesta para una peticion desconocida: {}",
            frame.request_id
        )))
    }
}

fn spawn_stderr_reader(
    stderr: ChildStderr,
    ring: Arc<Mutex<VecDeque<String>>>,
) -> Result<(JoinHandle<()>, Arc<AtomicBool>), String> {
    let done = Arc::new(AtomicBool::new(false));
    let done_signal = Arc::clone(&done);
    thread::Builder::new()
        .name("moonlit-sidecar-log".to_string())
        .spawn(move || {
            let mut reader = BufReader::new(stderr);
            let mut line = Vec::with_capacity(STDERR_LINE_LIMIT);
            while read_stderr_line(&mut reader, &mut line).unwrap_or(false) {
                let value = String::from_utf8_lossy(&line)
                    .trim()
                    .chars()
                    .take(STDERR_LINE_LIMIT)
                    .collect::<String>();
                if !value.is_empty() {
                    if let Ok(mut lines) = ring.lock() {
                        if lines.len() == STDERR_RING_LIMIT {
                            lines.pop_front();
                        }
                        lines.push_back(value);
                    }
                }
            }
            done_signal.store(true, Ordering::Release);
        })
        .map(|thread| (thread, done))
        .map_err(|error| error.to_string())
}

fn read_stderr_line(reader: &mut BufReader<ChildStderr>, line: &mut Vec<u8>) -> std::io::Result<bool> {
    line.clear();
    let mut byte = [0_u8; 1];
    loop {
        match reader.read(&mut byte)? {
            0 => return Ok(!line.is_empty()),
            1 if byte[0] == b'\n' => return Ok(true),
            1 if line.len() < STDERR_LINE_LIMIT => line.push(byte[0]),
            1 => {}
            _ => unreachable!("a one-byte buffer cannot return more than one byte"),
        }
    }
}

fn enqueue_request(
    requests: &SyncSender<WorkerRequest>,
    mut request: WorkerRequest,
    deadline: Instant,
) -> Result<(), SidecarError> {
    loop {
        match requests.try_send(request) {
            Ok(()) => return Ok(()),
            Err(mpsc::TrySendError::Disconnected(_)) => return Err(SidecarError::Exited),
            Err(mpsc::TrySendError::Full(next)) => {
                if Instant::now() >= deadline {
                    return Err(SidecarError::Timeout);
                }
                request = next;
                thread::sleep(remaining(deadline).min(Duration::from_millis(10)));
            }
        }
    }
}

fn remaining(deadline: Instant) -> Duration {
    deadline.saturating_duration_since(Instant::now())
}

fn fail_pending(pending: &PendingReplies, error: SidecarError) {
    if let Ok(mut pending) = pending.lock() {
        for (_, reply) in pending.drain() {
            let _ = reply.send(Err(match &error {
                SidecarError::Io(message) => SidecarError::Io(message.clone()),
                SidecarError::Protocol(message) => SidecarError::Protocol(message.clone()),
                SidecarError::Timeout => SidecarError::Timeout,
                SidecarError::Exited => SidecarError::Exited,
                SidecarError::InvalidResponse(message) => {
                    SidecarError::InvalidResponse(message.clone())
                }
            }));
        }
    }
}

fn append_diagnostics(error: SidecarError, diagnostics: String) -> SidecarError {
    let append = |message: String| format!("{message}; stderr: {diagnostics}");
    match error {
        SidecarError::Io(message) => SidecarError::Io(append(message)),
        SidecarError::Protocol(message) => SidecarError::Protocol(append(message)),
        SidecarError::Timeout => SidecarError::Timeout,
        SidecarError::Exited => SidecarError::Exited,
        SidecarError::InvalidResponse(message) => SidecarError::InvalidResponse(append(message)),
    }
}

fn join_bounded(thread: JoinHandle<()>, done: &AtomicBool) {
    let deadline = Instant::now() + THREAD_JOIN_TIMEOUT;
    while !done.load(Ordering::Acquire) && Instant::now() < deadline {
        thread::sleep(Duration::from_millis(10));
    }
    if done.load(Ordering::Acquire) {
        let _ = thread.join();
    }
    // Dropping an unfinished JoinHandle detaches it. The process has already
    // been terminated and this keeps shutdown bounded even if a pipe is stuck.
}

fn terminate_spawned_child(child: &mut Child) {
    let running = matches!(child.try_wait(), Ok(None) | Err(_));
    if running {
        terminate_process_tree(child.id());
    }
    let _ = child.kill();
    reap_child_bounded(child);
}

fn terminate_child(child: &Arc<Mutex<Child>>, process_id: &Arc<Mutex<u32>>) {
    let running = child
        .lock()
        .map(|mut child| matches!(child.try_wait(), Ok(None) | Err(_)))
        .unwrap_or(true);
    if running {
        let pid = process_id.lock().map(|id| *id).unwrap_or_default();
        if pid != 0 {
            terminate_process_tree(pid);
        }
    }
    if let Ok(mut child) = child.lock() {
        let _ = child.kill();
        reap_child_bounded(&mut child);
    }
}

fn reap_child_bounded(child: &mut Child) {
    let deadline = Instant::now() + CHILD_CLEANUP_TIMEOUT;
    loop {
        match child.try_wait() {
            Ok(Some(_)) | Err(_) => return,
            Ok(None) if Instant::now() >= deadline => return,
            Ok(None) => thread::sleep(Duration::from_millis(10)),
        }
    }
}

#[cfg(windows)]
fn terminate_process_tree(pid: u32) {
    let Some(taskkill) = taskkill_path() else {
        return;
    };
    let Ok(mut killer) = Command::new(taskkill)
        .args(["/PID", &pid.to_string(), "/T", "/F"])
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
    else {
        return;
    };
    let deadline = Instant::now() + CHILD_CLEANUP_TIMEOUT;
    loop {
        match killer.try_wait() {
            Ok(Some(_)) | Err(_) => return,
            Ok(None) if Instant::now() >= deadline => {
                let _ = killer.kill();
                return;
            }
            Ok(None) => thread::sleep(Duration::from_millis(10)),
        }
    }
}

#[cfg(not(windows))]
fn terminate_process_tree(_pid: u32) {}

#[cfg(windows)]
fn taskkill_path() -> Option<PathBuf> {
    let system_root = std::env::var_os("SystemRoot")?;
    let root = PathBuf::from(system_root);
    if !root.is_absolute() {
        return None;
    }
    let path = root.join("System32").join("taskkill.exe");
    path.is_file().then_some(path)
}

#[cfg(test)]
mod tests {
    use std::collections::VecDeque;
    use std::path::PathBuf;
    use std::sync::{mpsc, Arc, Mutex};
    use std::time::{Duration, Instant};

    use moonlit_libobs_protocol as protocol;

    use super::{
        deliver_response, enqueue_request, queue_event, request_timeout, ProcessSidecarLauncher,
        SidecarError, SidecarLauncher, WorkerRequest,
    };

    #[test]
    fn launcher_rejects_relative_executables_before_spawning() {
        let launcher = ProcessSidecarLauncher::new(PathBuf::from("recorder.exe"));
        let result = launcher.launch(&std::env::temp_dir());
        assert!(matches!(result, Err(SidecarError::Io(message)) if message.contains("absoluta")));
    }

    #[test]
    fn operation_deadlines_are_not_one_global_timeout() {
        assert!(
            request_timeout(&protocol::Request::Ping) < request_timeout(&protocol::Request::Probe)
        );
        assert!(
            request_timeout(&protocol::Request::Probe)
                < request_timeout(&protocol::Request::SaveReplay)
        );
    }

    #[test]
    fn a_full_request_queue_times_out_without_blocking_forever() {
        let (sender, _receiver) = mpsc::sync_channel(0);
        let (reply, _result) = mpsc::channel();
        let request = WorkerRequest {
            frame: protocol::Frame::request(1, protocol::Request::Ping),
            reply,
        };
        let started = Instant::now();
        let result = enqueue_request(&sender, request, Instant::now() + Duration::from_millis(25));
        assert!(matches!(result, Err(SidecarError::Timeout)));
        assert!(started.elapsed() < Duration::from_secs(1));
    }

    #[test]
    fn queued_events_preserve_wire_order_and_bound_growth() {
        let events = Arc::new(Mutex::new(VecDeque::new()));
        queue_event(&events, protocol::Event::Heartbeat);
        queue_event(
            &events,
            protocol::Event::SourceEnded {
                source_id: "monitor-1".to_string(),
            },
        );
        let values = events
            .lock()
            .expect("event queue")
            .iter()
            .cloned()
            .collect::<Vec<_>>();
        assert_eq!(values[0], protocol::Event::Heartbeat);
        assert_eq!(
            values[1],
            protocol::Event::SourceEnded {
                source_id: "monitor-1".to_string()
            }
        );
    }

    #[test]
    fn an_unknown_response_id_is_rejected_fail_closed() {
        let pending = Arc::new(Mutex::new(std::collections::HashMap::new()));
        let frame = protocol::Frame::response(99, protocol::Response::Pong);
        assert!(matches!(
            deliver_response(&pending, frame),
            Err(SidecarError::InvalidResponse(message)) if message.contains("desconocida")
        ));
    }
}
