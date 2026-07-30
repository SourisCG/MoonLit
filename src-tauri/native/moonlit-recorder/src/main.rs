//! Process entry point for the MoonLit recorder.
//!
//! The current scaffold owns the protocol and lifecycle boundary. The
//! `UnavailableEngine` is intentionally fail-closed until the pinned libobs
//! bridge is built and validated.

use std::env;
use std::io::{stdin, stdout, BufReader, BufWriter};
use std::path::PathBuf;

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

    let engine = UnavailableEngine::new(runtime_root);
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
    let report = UnavailableEngine::new(runtime_root).self_test();
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
}

impl UnavailableEngine {
    fn new(runtime_root: PathBuf) -> Self {
        Self { runtime_root }
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
                "El bridge libobs aun no esta habilitado en este build".to_string()
            } else {
                format!("Faltan componentes del runtime: {}", missing.join(", "))
            }),
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
            ready: missing.is_empty(),
            missing,
            note: "El build actual contiene solo el contrato; falta compilar el bridge libobs"
                .to_string(),
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
