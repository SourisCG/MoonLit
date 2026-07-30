//! Supervised control transport for the isolated recorder process.

use std::collections::{HashMap, VecDeque};
use std::fmt;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStderr, ChildStdin, ChildStdout, Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{sync_channel, Receiver, SyncSender};
use std::sync::{mpsc, Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::Duration;

use moonlit_libobs_protocol as protocol;
use protocol::{Payload, Request, Response};

const REQUEST_TIMEOUT: Duration = Duration::from_secs(5);
const STDERR_LINE_LIMIT: usize = 512;
const STDERR_RING_LIMIT: usize = 128;

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

impl SidecarLauncher for ProcessSidecarLauncher {
    fn launch(&self, runtime_root: &Path) -> Result<Box<dyn SidecarTransport>, SidecarError> {
        if !self.executable.is_absolute() {
            return Err(SidecarError::Io(
                "la ruta del sidecar debe ser absoluta".to_string(),
            ));
        }
        if !self.executable.is_file() {
            return Err(SidecarError::Io(format!(
                "no se encontro el ejecutable del sidecar: {}",
                self.executable.display()
            )));
        }
        if !runtime_root.is_absolute() || !runtime_root.is_dir() {
            return Err(SidecarError::Io(format!(
                "el runtime del sidecar no es un directorio valido: {}",
                runtime_root.display()
            )));
        }

        let working_directory = self.executable.parent().ok_or_else(|| {
            SidecarError::Io("el sidecar no tiene un directorio padre valido".to_string())
        })?;
        let mut command = Command::new(&self.executable);
        command
            .arg("--stdio")
            .arg("--runtime-root")
            .arg(runtime_root)
            .arg("--parent-pid")
            .arg(std::process::id().to_string())
            .current_dir(working_directory)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());
        let mut child = command
            .spawn()
            .map_err(|error| SidecarError::Io(error.to_string()))?;
        let stdin = match child.stdin.take() {
            Some(stdin) => stdin,
            None => {
                let _ = child.kill();
                let _ = child.wait();
                return Err(SidecarError::Io(
                    "no se pudo abrir stdin del sidecar".to_string(),
                ));
            }
        };
        let stdout = match child.stdout.take() {
            Some(stdout) => stdout,
            None => {
                let _ = child.kill();
                let _ = child.wait();
                return Err(SidecarError::Io(
                    "no se pudo abrir stdout del sidecar".to_string(),
                ));
            }
        };
        let stderr = match child.stderr.take() {
            Some(stderr) => stderr,
            None => {
                let _ = child.kill();
                let _ = child.wait();
                return Err(SidecarError::Io(
                    "no se pudo abrir stderr del sidecar".to_string(),
                ));
            }
        };
        let child = Arc::new(Mutex::new(child));
        let dead = Arc::new(AtomicBool::new(false));
        let stderr_ring = Arc::new(Mutex::new(VecDeque::with_capacity(STDERR_RING_LIMIT)));
        let stderr_join = spawn_stderr_reader(stderr, Arc::clone(&stderr_ring));
        let (requests, receiver) = sync_channel(8);
        let pending = Arc::new(Mutex::new(HashMap::new()));
        let events = Arc::new(Mutex::new(VecDeque::new()));
        let writer_dead = Arc::clone(&dead);
        let writer_child = Arc::clone(&child);
        let writer_pending = Arc::clone(&pending);
        let writer = thread::Builder::new()
            .name("moonlit-sidecar-io".to_string())
            .spawn(move || {
                process_writer(stdin, receiver, writer_pending, writer_child, writer_dead)
            })
            .map_err(|error| {
                terminate_child(&child);
                SidecarError::Io(error.to_string())
            })?;
        let reader_dead = Arc::clone(&dead);
        let reader_child = Arc::clone(&child);
        let reader_pending = Arc::clone(&pending);
        let reader_events = Arc::clone(&events);
        let reader = thread::Builder::new()
            .name("moonlit-sidecar-reader".to_string())
            .spawn(move || {
                process_reader(
                    stdout,
                    reader_pending,
                    reader_events,
                    reader_child,
                    reader_dead,
                )
            })
            .map_err(|error| {
                terminate_child(&child);
                SidecarError::Io(error.to_string())
            })?;

        Ok(Box::new(ProcessSidecarTransport {
            requests: Some(requests),
            child,
            dead,
            writer: Some(writer),
            reader: Some(reader),
            stderr_join: Some(stderr_join),
            next_request_id: 1,
            stderr_ring,
            events,
        }))
    }
}

struct ProcessSidecarTransport {
    requests: Option<SyncSender<WorkerRequest>>,
    child: Arc<Mutex<Child>>,
    dead: Arc<AtomicBool>,
    writer: Option<JoinHandle<()>>,
    reader: Option<JoinHandle<()>>,
    stderr_join: Option<JoinHandle<()>>,
    next_request_id: u64,
    stderr_ring: Arc<Mutex<VecDeque<String>>>,
    events: Arc<Mutex<VecDeque<protocol::Event>>>,
}

struct WorkerRequest {
    frame: protocol::Frame,
    reply: mpsc::Sender<Result<protocol::Frame, SidecarError>>,
}

impl SidecarTransport for ProcessSidecarTransport {
    fn request(&mut self, request: Request) -> Result<Response, SidecarError> {
        if self.dead.load(Ordering::Acquire) {
            return Err(SidecarError::Exited);
        }
        let request_id = self.next_request_id;
        self.next_request_id = self.next_request_id.saturating_add(1);
        let frame = protocol::Frame::request(request_id, request);
        let (reply, receiver) = mpsc::channel();
        let requests = self.requests.as_ref().ok_or(SidecarError::Exited)?;
        requests
            .send(WorkerRequest { frame, reply })
            .map_err(|_| SidecarError::Exited)?;
        let frame = match receiver.recv_timeout(REQUEST_TIMEOUT) {
            Ok(result) => result?,
            Err(mpsc::RecvTimeoutError::Timeout) => {
                self.terminate();
                return Err(SidecarError::Timeout);
            }
            Err(mpsc::RecvTimeoutError::Disconnected) => return Err(SidecarError::Exited),
        };
        match frame.payload {
            Payload::Response(response) => Ok(response),
            Payload::Event(event) => Err(SidecarError::InvalidResponse(format!(
                "evento inesperado: {event:?}"
            ))),
            Payload::Request(_) => Err(SidecarError::InvalidResponse(
                "el sidecar envio una solicitud al host".to_string(),
            )),
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
        terminate_child(&self.child);
    }
}

impl Drop for ProcessSidecarTransport {
    fn drop(&mut self) {
        self.terminate();
        if let Some(writer) = self.writer.take() {
            let _ = writer.join();
        }
        if let Some(reader) = self.reader.take() {
            let _ = reader.join();
        }
        if let Some(stderr) = self.stderr_join.take() {
            let _ = stderr.join();
        }
        if let Ok(mut ring) = self.stderr_ring.lock() {
            ring.clear();
        }
    }
}

fn process_writer(
    mut stdin: ChildStdin,
    receiver: Receiver<WorkerRequest>,
    pending: PendingReplies,
    child: Arc<Mutex<Child>>,
    dead: Arc<AtomicBool>,
) {
    while let Ok(request) = receiver.recv() {
        let request_id = request.frame.request_id;
        if let Ok(mut pending) = pending.lock() {
            pending.insert(request_id, request.reply);
        } else {
            let _ = request.reply.send(Err(SidecarError::Protocol(
                "la tabla de peticiones esta bloqueada".to_string(),
            )));
            dead.store(true, Ordering::Release);
            terminate_child(&child);
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
            terminate_child(&child);
            break;
        }
    }
}

fn process_reader(
    mut stdout: ChildStdout,
    pending: PendingReplies,
    events: Arc<Mutex<VecDeque<protocol::Event>>>,
    child: Arc<Mutex<Child>>,
    dead: Arc<AtomicBool>,
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
            terminate_child(&child);
            break;
        };
        match frame.payload {
            Payload::Event(event) => {
                if let Ok(mut queued) = events.lock() {
                    if queued.len() >= 64 {
                        queued.pop_front();
                    }
                    queued.push_back(event);
                }
            }
            Payload::Response(_) => {
                if let Ok(mut pending) = pending.lock() {
                    if let Some(reply) = pending.remove(&frame.request_id) {
                        let _ = reply.send(Ok(frame));
                    }
                }
            }
            Payload::Request(_) => {
                dead.store(true, Ordering::Release);
                terminate_child(&child);
                break;
            }
        }
    }
}

fn protocol_error(error: protocol::ProtocolError) -> SidecarError {
    SidecarError::Protocol(error.to_string())
}

fn spawn_stderr_reader(stderr: ChildStderr, ring: Arc<Mutex<VecDeque<String>>>) -> JoinHandle<()> {
    thread::Builder::new()
        .name("moonlit-sidecar-log".to_string())
        .spawn(move || {
            let mut reader = BufReader::new(stderr);
            let mut line = String::new();
            while reader.read_line(&mut line).unwrap_or(0) > 0 {
                let value = line
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
                line.clear();
            }
        })
        .expect("failed to start sidecar stderr reader")
}

fn terminate_child(child: &Arc<Mutex<Child>>) {
    if let Ok(mut child) = child.lock() {
        let _ = child.kill();
        let _ = child.wait();
    }
}

#[cfg(test)]
mod tests {
    use std::path::PathBuf;

    use super::{ProcessSidecarLauncher, SidecarError, SidecarLauncher};

    #[test]
    fn launcher_rejects_relative_executables_before_spawning() {
        let launcher = ProcessSidecarLauncher::new(PathBuf::from("recorder.exe"));
        let result = launcher.launch(&std::env::temp_dir());
        assert!(matches!(result, Err(SidecarError::Io(message)) if message.contains("absoluta")));
    }
}
