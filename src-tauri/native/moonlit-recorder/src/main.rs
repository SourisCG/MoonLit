//! Process entry point for the MoonLit recorder.
//!
//! The current scaffold owns the protocol and lifecycle boundary. The
//! `UnavailableEngine` is intentionally fail-closed until the pinned libobs
//! bridge is built and validated.

use std::env;
use std::io::{stdin, stdout, BufReader, BufWriter};
use std::path::PathBuf;

mod bridge;

use moonlit_libobs_protocol as protocol;
use protocol::{Frame, Payload, ProbeResult, Request, Response, SidecarError, StartRequest};
use serde::Serialize;

const VERSION: &str = env!("CARGO_PKG_VERSION");

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

    let engine = Engine::new(runtime_root);
    let input = stdin();
    let output = stdout();
    run_server(
        engine,
        BufReader::new(input.lock()),
        BufWriter::new(output.lock()),
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

fn run_server<E: ReplayEngine, R: std::io::Read, W: std::io::Write>(
    mut engine: E,
    mut input: R,
    mut output: W,
) -> Result<(), protocol::ProtocolError> {
    loop {
        let Some(frame) = protocol::read_frame(&mut input)? else {
            return Ok(());
        };
        let request_id = frame.request_id;
        let Payload::Request(request) = frame.payload else {
            let response = Frame::response(
                request_id,
                Response::Error(SidecarError {
                    code: "invalidRequest".to_string(),
                    message: "el host debe enviar una solicitud".to_string(),
                    retryable: false,
                }),
            );
            protocol::write_frame(&mut output, &response)?;
            continue;
        };
        let should_stop = matches!(request, Request::Shutdown);
        let response = match request {
            Request::Hello { .. } => Response::Hello {
                sidecar_version: VERSION.to_string(),
                protocol_version: protocol::PROTOCOL_VERSION,
            },
            Request::Probe => Response::Probe(engine.probe()),
            Request::Start(request) => response_or_error(engine.start(request)),
            Request::SaveReplay => response_or_error(engine.save_replay()),
            Request::Stop => response_or_error(engine.stop()),
            Request::Ping => Response::Pong,
            Request::Shutdown => Response::Stopped,
        };
        protocol::write_frame(&mut output, &Frame::response(request_id, response))?;
        if should_stop {
            return Ok(());
        }
    }
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
