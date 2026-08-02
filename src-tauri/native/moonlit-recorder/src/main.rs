//! Process entry point for the MoonLit recorder.
//!
//! The current scaffold owns the protocol and lifecycle boundary. The
//! `UnavailableEngine` is intentionally fail-closed until the pinned libobs
//! bridge is built and validated.

use std::env;
use std::io::{stdin, stdout, BufReader, BufWriter};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{sync_channel, RecvTimeoutError};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

mod bridge;
mod parent;

use moonlit_libobs_protocol as protocol;
use parent::ParentDeathMonitor;
use protocol::{Frame, Payload, ProbeResult, Request, Response, SidecarError, StartRequest};
use serde::Serialize;

const VERSION: &str = env!("CARGO_PKG_VERSION");
const OPERATION_POLL_INTERVAL: Duration = Duration::from_millis(25);
const ENGINE_CLEANUP_TIMEOUT: Duration = Duration::from_secs(2);
const TIMED_OUT_CLEANUP_GRACE: Duration = Duration::from_millis(25);

fn main() {
    if let Err(error) = run() {
        eprintln!("moonlit-recorder: {error}");
        std::process::exit(1);
    }
}

fn run() -> Result<(), String> {
    let arguments = env::args().skip(1).collect::<Vec<_>>();
    if arguments.iter().any(|argument| argument == "--self-test") {
        return run_self_test(&arguments);
    }
    if !arguments.iter().any(|argument| argument == "--stdio") {
        return Err("solo se admite el modo --stdio o --self-test".to_string());
    }
    let runtime_root = argument_value(&arguments, "--runtime-root")
        .ok_or_else(|| "falta --runtime-root".to_string())?;
    let runtime_root = PathBuf::from(runtime_root);
    if !runtime_root.is_absolute() || !runtime_root.is_dir() {
        return Err("--runtime-root debe ser un directorio absoluto existente".to_string());
    }
    let parent_pid = required_parent_pid(&arguments)?;
    let parent_monitor = ParentDeathMonitor::new(parent_pid)?;

    let engine = Engine::new(runtime_root);
    let input = stdin();
    let output = stdout();
    run_server(
        engine,
        BufReader::new(input),
        BufWriter::new(output.lock()),
        parent_pid,
        parent_monitor,
    )
    .map_err(|error| error.to_string())
}

fn run_self_test(arguments: &[String]) -> Result<(), String> {
    let runtime_root = argument_value(arguments, "--runtime-root")
        .map(PathBuf::from)
        .unwrap_or_default();
    let report = Engine::new(runtime_root).self_test();
    let json = serde_json::to_string(&report).map_err(|error| error.to_string())?;
    println!("{json}");
    Ok(())
}

fn argument_value(arguments: &[String], name: &str) -> Option<String> {
    arguments
        .windows(2)
        .find(|pair| pair[0] == name)
        .map(|pair| pair[1].clone())
}

fn required_parent_pid(arguments: &[String]) -> Result<u32, String> {
    let value = argument_value(arguments, "--parent-pid")
        .ok_or_else(|| "falta --parent-pid; el recorder debe estar supervisado".to_string())?;
    let pid = value
        .parse::<u32>()
        .map_err(|_| "--parent-pid debe ser un PID entero".to_string())?;
    if pid == 0 {
        return Err("--parent-pid debe ser mayor que cero".to_string());
    }
    Ok(pid)
}

trait ReplayEngine {
    fn probe(&self) -> ProbeResult;
    fn start(&mut self, request: StartRequest) -> Result<Response, SidecarError>;
    fn save_replay(&mut self) -> Result<Response, SidecarError>;
    fn stop(&mut self) -> Result<Response, SidecarError>;
    fn self_test(&self) -> SelfTestReport;
}

enum Engine {
    Bridge(bridge::BridgeEngine),
    Unavailable(UnavailableEngine),
}

impl Engine {
    fn new(runtime_root: PathBuf) -> Self {
        match bridge::BridgeEngine::new(&runtime_root) {
            Ok(engine) => Self::Bridge(engine),
            Err(error) => Self::Unavailable(UnavailableEngine::with_note(runtime_root, error)),
        }
    }
}

impl ReplayEngine for Engine {
    fn probe(&self) -> ProbeResult {
        match self {
            Self::Bridge(engine) => engine.probe().unwrap_or_else(|error| ProbeResult {
                available: false,
                sources: Vec::new(),
                encoders: Vec::new(),
                max_width: None,
                max_height: None,
                max_fps: None,
                note: Some(error.message),
                codecs: vec!["h264".to_string(), "hevc".to_string()],
                formats: vec!["mp4".to_string(), "mkv".to_string()],
                audio: protocol::AudioInfo::default(),
            }),
            Self::Unavailable(engine) => engine.probe(),
        }
    }

    fn start(&mut self, request: StartRequest) -> Result<Response, SidecarError> {
        match self {
            Self::Bridge(engine) => engine.start(&request),
            Self::Unavailable(engine) => engine.start(request),
        }
    }

    fn save_replay(&mut self) -> Result<Response, SidecarError> {
        match self {
            Self::Bridge(engine) => engine.save(),
            Self::Unavailable(engine) => engine.save_replay(),
        }
    }

    fn stop(&mut self) -> Result<Response, SidecarError> {
        match self {
            Self::Bridge(engine) => engine.stop().map(|_| Response::Stopped),
            Self::Unavailable(engine) => engine.stop(),
        }
    }

    fn self_test(&self) -> SelfTestReport {
        match self {
            Self::Bridge(engine) => match engine.probe() {
                Ok(probe) => SelfTestReport {
                    version: VERSION.to_string(),
                    protocol_version: protocol::PROTOCOL_VERSION,
                    runtime_root: engine.runtime_root().to_path_buf(),
                    ready: probe.available && !probe.sources.is_empty(),
                    missing: Vec::new(),
                    note: probe
                        .note
                        .unwrap_or_else(|| "Bridge inicializado".to_string()),
                },
                Err(error) => SelfTestReport {
                    version: VERSION.to_string(),
                    protocol_version: protocol::PROTOCOL_VERSION,
                    runtime_root: engine.runtime_root().to_path_buf(),
                    ready: false,
                    missing: Vec::new(),
                    note: error.message,
                },
            },
            Self::Unavailable(engine) => engine.self_test(),
        }
    }
}

fn run_server<E: ReplayEngine + Send + 'static, R: std::io::Read + Send + 'static, W: std::io::Write>(
    engine: E,
    mut input: R,
    mut output: W,
    expected_parent_pid: u32,
    parent_monitor: ParentDeathMonitor,
) -> Result<(), protocol::ProtocolError> {
    let engine = Arc::new(Mutex::new(engine));
    let (frames, receiver) = sync_channel(1);
    thread::Builder::new()
        .name("moonlit-recorder-protocol-reader".to_string())
        .spawn(move || loop {
            let result = protocol::read_frame(&mut input);
            let should_exit = matches!(result, Ok(None) | Err(_));
            if frames.send(result).is_err() || should_exit {
                break;
            }
        })
        .map_err(|error| protocol::ProtocolError::Io(std::io::Error::other(error)))?;
    let mut hello_seen = false;
    loop {
        if parent_monitor.is_dead() {
            let _ = cleanup_engine(&engine);
            return Ok(());
        }
        let frame = match receiver.recv_timeout(parent::POLL_INTERVAL) {
            Ok(Ok(Some(frame))) => frame,
            Ok(Ok(None)) => {
                let _ = cleanup_engine(&engine);
                return Ok(());
            }
            Ok(Err(error)) => {
                let _ = cleanup_engine(&engine);
                return Err(error);
            }
            Err(RecvTimeoutError::Timeout) => continue,
            Err(RecvTimeoutError::Disconnected) => {
                let _ = cleanup_engine(&engine);
                return Ok(());
            }
        };
        let request_id = frame.request_id;
        let Payload::Request(request) = frame.payload else {
            let response = Frame::response(
                request_id,
                invalid_request("el host debe enviar una solicitud"),
            );
            let result = protocol::write_frame(&mut output, &response);
            let _ = cleanup_engine(&engine);
            result?;
            return Ok(());
        };
        if !hello_seen {
            let (response, should_stop) = match request {
                Request::Hello {
                    parent_pid: Some(pid),
                } if pid == expected_parent_pid && !parent_monitor.is_dead() => {
                    hello_seen = true;
                    (
                        Response::Hello {
                            sidecar_version: VERSION.to_string(),
                            protocol_version: protocol::PROTOCOL_VERSION,
                        },
                        false,
                    )
                }
                Request::Hello { .. } => (
                    invalid_request("el parent PID del handshake no coincide con el supervisor"),
                    true,
                ),
                _ => (
                    invalid_request("el primer mensaje debe ser Hello con parent PID"),
                    true,
                ),
            };
            let result = protocol::write_frame(&mut output, &Frame::response(request_id, response));
            if should_stop {
                let _ = cleanup_engine(&engine);
            }
            result?;
            if should_stop {
                return Ok(());
            }
            continue;
        }

        let timeout = operation_timeout(&request);
        match dispatch_request_with_timeout(
            Arc::clone(&engine),
            request,
            timeout,
            &parent_monitor,
        ) {
            OperationWait::Complete(result) => {
                let write_result =
                    protocol::write_frame(&mut output, &Frame::response(request_id, result.response));
                write_result?;
                if result.should_stop {
                    return Ok(());
                }
            }
            OperationWait::TimedOut => {
                let response = Frame::response(
                    request_id,
                    timeout_response("la operacion del sidecar excedio su deadline"),
                );
                let result = protocol::write_frame(&mut output, &response);
                // The operation worker is deliberately not joined here. A
                // synchronous bridge call cannot be forcefully cancelled in
                // safe Rust, so the process boundary contains it after the
                // bounded timeout response instead of waiting forever.
                let _ = cleanup_engine_with_timeout(&engine, TIMED_OUT_CLEANUP_GRACE);
                result?;
                return Ok(());
            }
            OperationWait::ParentDied => {
                let _ = cleanup_engine(&engine);
                return Ok(());
            }
        }
    }
}

const fn operation_timeout(request: &Request) -> Duration {
    match request {
        Request::Hello { .. } => Duration::from_secs(2),
        Request::Probe => Duration::from_secs(5),
        Request::Start(_) => Duration::from_secs(10),
        Request::SaveReplay => Duration::from_secs(30),
        Request::Stop => Duration::from_secs(5),
        Request::Ping => Duration::from_secs(1),
        Request::Shutdown => Duration::from_secs(2),
    }
}

enum OperationWait {
    Complete(OperationResult),
    TimedOut,
    ParentDied,
}

struct OperationResult {
    response: Response,
    should_stop: bool,
}

fn dispatch_request_with_timeout<E: ReplayEngine + Send + 'static>(
    engine: Arc<Mutex<E>>,
    request: Request,
    timeout: Duration,
    parent_monitor: &ParentDeathMonitor,
) -> OperationWait {
    let deadline = Instant::now() + timeout;
    let (result_sender, result_receiver) = sync_channel(1);
    let cancelled = Arc::new(AtomicBool::new(false));
    let cancelled_signal = Arc::clone(&cancelled);
    let worker_engine = Arc::clone(&engine);
    let should_stop = matches!(&request, Request::Shutdown);
    if thread::Builder::new()
        .name("moonlit-recorder-engine-operation".to_string())
        .spawn(move || {
            let result = match worker_engine.lock() {
                Ok(mut engine) => {
                    let result = dispatch_request_now(&mut *engine, request, should_stop);
                    if cancelled_signal.load(Ordering::Acquire) {
                        let _ = engine.stop();
                    }
                    result
                }
                Err(_) => OperationResult {
                    response: internal_error("el estado del engine esta bloqueado"),
                    should_stop: true,
                },
            };
            let _ = result_sender.send(result);
        })
        .is_err()
    {
        return OperationWait::Complete(OperationResult {
            response: internal_error("no se pudo iniciar la operacion del engine"),
            should_stop: true,
        });
    }

    loop {
        if parent_monitor.is_dead() {
            cancelled.store(true, Ordering::Release);
            return OperationWait::ParentDied;
        }
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            cancelled.store(true, Ordering::Release);
            return OperationWait::TimedOut;
        }
        match result_receiver.recv_timeout(remaining.min(OPERATION_POLL_INTERVAL)) {
            Ok(result) => return OperationWait::Complete(result),
            Err(RecvTimeoutError::Timeout) => continue,
            Err(RecvTimeoutError::Disconnected) => {
                cancelled.store(true, Ordering::Release);
                return OperationWait::Complete(OperationResult {
                    response: internal_error("la operacion del engine se desconecto"),
                    should_stop: true,
                });
            }
        }
    }
}

fn dispatch_request_now<E: ReplayEngine>(
    engine: &mut E,
    request: Request,
    should_stop: bool,
) -> OperationResult {
    let response = match request {
        Request::Probe => Response::Probe(engine.probe()),
        Request::Start(request) => response_or_error(engine.start(request)),
        Request::SaveReplay => response_or_error(engine.save_replay()),
        Request::Stop => response_or_error(engine.stop()),
        Request::Ping => Response::Pong,
        Request::Shutdown => response_or_error(engine.stop()),
        Request::Hello { .. } => invalid_request("Hello solo puede aparecer al inicio"),
    };
    OperationResult {
        response,
        should_stop,
    }
}

fn cleanup_engine<E: ReplayEngine + Send + 'static>(engine: &Arc<Mutex<E>>) -> bool {
    cleanup_engine_with_timeout(engine, ENGINE_CLEANUP_TIMEOUT)
}

fn cleanup_engine_with_timeout<E: ReplayEngine + Send + 'static>(
    engine: &Arc<Mutex<E>>,
    timeout: Duration,
) -> bool {
    let engine = Arc::clone(engine);
    let (result_sender, result_receiver) = sync_channel(1);
    if thread::Builder::new()
        .name("moonlit-recorder-engine-cleanup".to_string())
        .spawn(move || {
            let stopped = engine
                .lock()
                .map(|mut engine| engine.stop().is_ok())
                .unwrap_or(false);
            let _ = result_sender.send(stopped);
        })
        .is_err()
    {
        return false;
    }
    result_receiver.recv_timeout(timeout).unwrap_or(false)
}

fn timeout_response(message: &str) -> Response {
    Response::Error(SidecarError {
        code: "timeout".to_string(),
        message: message.to_string(),
        retryable: true,
    })
}

fn internal_error(message: &str) -> Response {
    Response::Error(SidecarError {
        code: "internal".to_string(),
        message: message.to_string(),
        retryable: true,
    })
}

fn invalid_request(message: &str) -> Response {
    Response::Error(SidecarError {
        code: "invalidRequest".to_string(),
        message: message.to_string(),
        retryable: false,
    })
}

fn response_or_error(result: Result<Response, SidecarError>) -> Response {
    result.unwrap_or_else(Response::Error)
}

struct UnavailableEngine {
    runtime_root: PathBuf,
    note: Option<String>,
}

impl UnavailableEngine {
    fn with_note(runtime_root: PathBuf, note: String) -> Self {
        Self {
            runtime_root,
            note: Some(note),
        }
    }

    fn note(&self) -> String {
        self.note
            .clone()
            .unwrap_or_else(|| "El bridge libobs no esta disponible".to_string())
    }

    fn required_runtime_files(&self) -> Vec<String> {
        [
            "bin/64bit/obs.dll",
            "bin/64bit/libobs-d3d11.dll",
            "bin/64bit/moonlit-obs-bridge.dll",
            "bin/64bit/obs-ffmpeg-mux.exe",
        ]
        .iter()
        .filter(|relative| !self.runtime_root.join(relative).is_file())
        .map(|relative| (*relative).to_string())
        .collect()
    }
}

impl ReplayEngine for UnavailableEngine {
    fn probe(&self) -> ProbeResult {
        let missing = self.required_runtime_files();
        ProbeResult {
            available: false,
            sources: Vec::new(),
            encoders: vec![protocol::EncoderInfo {
                id: "software".to_string(),
                available: false,
                reason: Some(
                    "La fuente WGC y el bridge libobs aun no estan compilados".to_string(),
                ),
            }],
            max_width: None,
            max_height: None,
            max_fps: None,
            note: Some(if missing.is_empty() {
                self.note()
            } else {
                format!(
                    "{}; faltan componentes: {}",
                    self.note(),
                    missing.join(", ")
                )
            }),
            codecs: vec!["h264".to_string(), "hevc".to_string()],
            formats: vec!["mp4".to_string(), "mkv".to_string()],
            audio: protocol::AudioInfo {
                available: false,
                system_audio: false,
                microphone: false,
                application_audio: false,
                note: Some("WASAPI aun no esta habilitado en este build".to_string()),
            },
        }
    }

    fn start(&mut self, _request: StartRequest) -> Result<Response, SidecarError> {
        Err(SidecarError {
            code: "backendUnavailable".to_string(),
            message: "El bridge libobs aun no esta habilitado".to_string(),
            retryable: true,
        })
    }

    fn save_replay(&mut self) -> Result<Response, SidecarError> {
        Err(SidecarError {
            code: "backendExited".to_string(),
            message: "No hay una sesion libobs activa".to_string(),
            retryable: true,
        })
    }

    fn stop(&mut self) -> Result<Response, SidecarError> {
        Ok(Response::Stopped)
    }

    fn self_test(&self) -> SelfTestReport {
        let missing = self.required_runtime_files();
        SelfTestReport {
            version: VERSION.to_string(),
            protocol_version: protocol::PROTOCOL_VERSION,
            runtime_root: self.runtime_root.clone(),
            ready: false,
            missing,
            note: self.note(),
        }
    }
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct SelfTestReport {
    version: String,
    protocol_version: u16,
    runtime_root: PathBuf,
    ready: bool,
    missing: Vec<String>,
    note: String,
}

#[cfg(test)]
mod tests {
    use std::io::{Cursor, Read};
    use std::process::Command;
    use std::sync::{Arc, Mutex};
    use std::thread;
    use std::time::{Duration, Instant};

    use super::{
        dispatch_request_with_timeout, run_server, OperationWait, ParentDeathMonitor, ProbeResult,
        ReplayEngine, Response, SelfTestReport, SidecarError, StartRequest, VERSION,
    };
    use moonlit_libobs_protocol as protocol;

    struct TestEngine {
        stop_calls: Arc<Mutex<u32>>,
        probe_delay: Duration,
    }

    struct DelayedEof {
        delay: Duration,
        returned: bool,
    }

    impl Read for DelayedEof {
        fn read(&mut self, _buffer: &mut [u8]) -> std::io::Result<usize> {
            if !self.returned {
                self.returned = true;
                thread::sleep(self.delay);
            }
            Ok(0)
        }
    }

    impl ReplayEngine for TestEngine {
        fn probe(&self) -> ProbeResult {
            thread::sleep(self.probe_delay);
            ProbeResult {
                available: false,
                sources: Vec::new(),
                encoders: Vec::new(),
                max_width: None,
                max_height: None,
                max_fps: None,
                note: None,
                codecs: Vec::new(),
                formats: Vec::new(),
                audio: protocol::AudioInfo::default(),
            }
        }

        fn start(&mut self, _request: StartRequest) -> Result<Response, SidecarError> {
            Ok(Response::Started {
                encoder: "software".to_string(),
                codec: "h264".to_string(),
                format: "mp4".to_string(),
            })
        }

        fn save_replay(&mut self) -> Result<Response, SidecarError> {
            Ok(Response::Stopped)
        }

        fn stop(&mut self) -> Result<Response, SidecarError> {
            *self.stop_calls.lock().expect("stop count") += 1;
            Ok(Response::Stopped)
        }

        fn self_test(&self) -> SelfTestReport {
            SelfTestReport {
                version: VERSION.to_string(),
                protocol_version: protocol::PROTOCOL_VERSION,
                runtime_root: std::path::PathBuf::new(),
                ready: false,
                missing: Vec::new(),
                note: "test".to_string(),
            }
        }
    }

    fn request_bytes(requests: impl IntoIterator<Item = protocol::Frame>) -> Vec<u8> {
        let mut bytes = Vec::new();
        for frame in requests {
            protocol::write_frame(&mut bytes, &frame).expect("request frame");
        }
        bytes
    }

    fn test_engine() -> (TestEngine, Arc<Mutex<u32>>) {
        let stop_calls = Arc::new(Mutex::new(0));
        (
            TestEngine {
                stop_calls: Arc::clone(&stop_calls),
                probe_delay: Duration::ZERO,
            },
            stop_calls,
        )
    }

    #[test]
    fn shutdown_is_acknowledged_and_stops_the_engine() {
        let (engine, stop_calls) = test_engine();
        let parent_pid = std::process::id();
        let input = request_bytes([
            protocol::Frame::request(
                1,
                protocol::Request::Hello {
                    parent_pid: Some(parent_pid),
                },
            ),
            protocol::Frame::request(2, protocol::Request::Shutdown),
        ]);
        let mut output = Vec::new();
        run_server(
            engine,
            Cursor::new(input),
            &mut output,
            parent_pid,
            ParentDeathMonitor::new(parent_pid).expect("parent monitor"),
        )
        .expect("server");
        let mut output = Cursor::new(output);
        assert!(matches!(
            protocol::read_frame(&mut output).expect("hello response"),
            Some(protocol::Frame {
                payload: protocol::Payload::Response(Response::Hello { .. }),
                ..
            })
        ));
        assert!(matches!(
            protocol::read_frame(&mut output).expect("shutdown response"),
            Some(protocol::Frame {
                payload: protocol::Payload::Response(Response::Stopped),
                ..
            })
        ));
        assert_eq!(*stop_calls.lock().expect("stop count"), 1);
    }

    #[test]
    fn eof_stops_the_engine_without_waiting_for_a_frame() {
        let (engine, stop_calls) = test_engine();
        let mut output = Vec::new();
        run_server(
            engine,
            Cursor::new(Vec::<u8>::new()),
            &mut output,
            std::process::id(),
            ParentDeathMonitor::new(std::process::id()).expect("parent monitor"),
        )
        .expect("server");
        assert!(output.is_empty());
        assert_eq!(*stop_calls.lock().expect("stop count"), 1);
    }

    #[test]
    fn mismatched_parent_pid_is_rejected_before_engine_requests() {
        let (engine, stop_calls) = test_engine();
        let expected = std::process::id();
        let input = request_bytes([protocol::Frame::request(
            1,
            protocol::Request::Hello {
                parent_pid: Some(expected.saturating_add(1)),
            },
        )]);
        let mut output = Vec::new();
        run_server(
            engine,
            Cursor::new(input),
            &mut output,
            expected,
            ParentDeathMonitor::new(expected).expect("parent monitor"),
        )
        .expect("server");
        let response = protocol::read_frame(&mut Cursor::new(output))
            .expect("response")
            .expect("frame");
        assert!(matches!(
            response.payload,
            protocol::Payload::Response(Response::Error(SidecarError { code, .. }))
                if code == "invalidRequest"
        ));
        assert_eq!(*stop_calls.lock().expect("stop count"), 1);
    }

    #[test]
    fn malformed_host_payload_fails_closed() {
        let (engine, _stop_calls) = test_engine();
        let input = request_bytes([protocol::Frame::response(1, Response::Pong)]);
        let mut output = Vec::new();
        run_server(
            engine,
            Cursor::new(input),
            &mut output,
            std::process::id(),
            ParentDeathMonitor::new(std::process::id()).expect("parent monitor"),
        )
        .expect("server");
        let response = protocol::read_frame(&mut Cursor::new(output))
            .expect("response")
            .expect("frame");
        assert!(matches!(
            response.payload,
            protocol::Payload::Response(Response::Error(SidecarError { code, .. }))
                if code == "invalidRequest"
        ));
    }

    #[test]
    fn operation_timeout_returns_a_fail_closed_error() {
        let (mut engine, _stop_calls) = test_engine();
        engine.probe_delay = Duration::from_millis(10);
        let monitor = ParentDeathMonitor::new(std::process::id()).expect("parent monitor");
        let result = dispatch_request_with_timeout(
            Arc::new(Mutex::new(engine)),
            protocol::Request::Probe,
            Duration::from_millis(1),
            &monitor,
        );
        assert!(matches!(result, OperationWait::TimedOut));
    }

    #[test]
    fn hung_engine_work_is_contained_without_waiting_for_return() {
        let (mut engine, _stop_calls) = test_engine();
        engine.probe_delay = Duration::from_millis(250);
        let monitor = ParentDeathMonitor::new(std::process::id()).expect("parent monitor");
        let started = Instant::now();
        let result = dispatch_request_with_timeout(
            Arc::new(Mutex::new(engine)),
            protocol::Request::Probe,
            Duration::from_millis(5),
            &monitor,
        );
        assert!(matches!(result, OperationWait::TimedOut));
        assert!(started.elapsed() < Duration::from_millis(100));
    }

    #[test]
    fn parent_death_stops_the_engine_before_server_exit() {
        let mut parent = Command::new(std::env::current_exe().expect("test executable"))
            .arg("--help")
            .spawn()
            .expect("parent process");
        let (engine, stop_calls) = test_engine();
        let parent_monitor = ParentDeathMonitor::new(parent.id()).expect("parent monitor");
        let mut output = Vec::new();
        run_server(
            engine,
            DelayedEof {
                delay: Duration::from_millis(500),
                returned: false,
            },
            &mut output,
            parent.id(),
            parent_monitor,
        )
        .expect("server cleanup");
        parent.wait().expect("parent exit");
        assert!(output.is_empty());
        assert_eq!(*stop_calls.lock().expect("stop count"), 1);
    }
}
