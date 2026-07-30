//! Supervised control transport for the isolated recorder process.

use std::collections::VecDeque;
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
        let worker_dead = Arc::clone(&dead);
        let worker_child = Arc::clone(&child);
        let worker = thread::Builder::new()
            .name("moonlit-sidecar-io".to_string())
            .spawn(move || process_worker(stdin, stdout, receiver, worker_child, worker_dead))
            .map_err(|error| {
                terminate_child(&child);
                SidecarError::Io(error.to_string())
            })?;

        Ok(Box::new(ProcessSidecarTransport {
            requests: Some(requests),
            child,
            dead,
            worker: Some(worker),
            stderr_join: Some(stderr_join),
            next_request_id: 1,
            stderr_ring,
        }))
    }
}

struct ProcessSidecarTransport {
    requests: Option<SyncSender<WorkerRequest>>,
    child: Arc<Mutex<Child>>,
    dead: Arc<AtomicBool>,
    worker: Option<JoinHandle<()>>,
    stderr_join: Option<JoinHandle<()>>,
    next_request_id: u64,
    stderr_ring: Arc<Mutex<VecDeque<String>>>,
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
                "evento inesperado despues de esperar respuesta: {event:?}"
            ))),
            Payload::Request(_) => Err(SidecarError::InvalidResponse(
                "el sidecar envio una solicitud al host".to_string(),
            )),
        }
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
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
        if let Some(stderr) = self.stderr_join.take() {
            let _ = stderr.join();
        }
        if let Ok(mut ring) = self.stderr_ring.lock() {
            ring.clear();
        }
    }
}

fn process_worker(
    mut stdin: ChildStdin,
    mut stdout: ChildStdout,
    receiver: Receiver<WorkerRequest>,
    child: Arc<Mutex<Child>>,
    dead: Arc<AtomicBool>,
) {
    while let Ok(request) = receiver.recv() {
        let request_id = request.frame.request_id;
        let result = write_and_read(&mut stdin, &mut stdout, &request.frame, request_id);
        let terminal = result.is_err();
        let _ = request.reply.send(result);
        if terminal {
            dead.store(true, Ordering::Release);
            terminate_child(&child);
            break;
        }
    }
    dead.store(true, Ordering::Release);
}

fn write_and_read(
    stdin: &mut ChildStdin,
    stdout: &mut ChildStdout,
    request: &protocol::Frame,
    request_id: u64,
) -> Result<protocol::Frame, SidecarError> {
    protocol::write_frame(stdin, request).map_err(protocol_error)?;
    loop {
        let frame = protocol::read_frame(stdout)
            .map_err(protocol_error)?
            .ok_or(SidecarError::Exited)?;
        if frame.request_id != request_id {
            return Err(SidecarError::Protocol(format!(
                "request id inesperado: {}/{}",
                frame.request_id, request_id
            )));
        }
        match frame.payload {
            Payload::Event(protocol::Event::Heartbeat) => continue,
            Payload::Event(protocol::Event::SourceEnded { source_id }) => {
                return Ok(protocol::Frame::response(
                    request_id,
                    Response::Error(protocol::SidecarError {
                        code: "sourceEnded".to_string(),
                        message: format!("la fuente termino: {source_id}"),
                        retryable: true,
                    }),
                ));
            }
            Payload::Event(protocol::Event::Fatal(error)) => {
                return Ok(protocol::Frame::response(
                    request_id,
                    Response::Error(error),
                ));
            }
            Payload::Response(_) => return Ok(frame),
            Payload::Request(_) => {
                return Err(SidecarError::InvalidResponse(
                    "el sidecar envio una solicitud al host".to_string(),
                ));
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
