//! Linux-only adapter for gpu-screen-recorder.
//!
//! GSR is kept as a legacy process backend. It uses the same replay contract
//! as the fake and native backends but does not define the Windows design.

use std::collections::VecDeque;
use std::env;
use std::ffi::OsString;
use std::fs;
use std::io::{BufRead, BufReader, Read};
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Output, Stdio};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

use crate::traits::{
    BackendCapabilities, BackendDescriptor, BackendError, BackendId, CaptureSource,
    CaptureSourceKind, ClipArtifact, ClipKind, EncoderCapability, EncoderPreference, ReplayBackend,
    ReplayConfig, VideoCodec,
};
use serde::Serialize;

#[derive(Clone, Debug, Eq, PartialEq)]
struct CommandSpec {
    program: PathBuf,
    args: Vec<OsString>,
}

impl VideoCodec {
    fn as_gsr_value(&self) -> &'static str {
        match self {
            Self::H264 => "h264",
            Self::Hevc => "hevc",
        }
    }
}

#[derive(Clone, Debug)]
pub struct GsrBackend {
    executable: Option<PathBuf>,
    origin: Option<String>,
}

impl GsrBackend {
    pub fn discover_with_resource_dir(resource_dir: Option<PathBuf>) -> Self {
        let mut candidates = Vec::new();

        if let Ok(path) = env::var("SOURISTV_GSR_PATH") {
            candidates.push((PathBuf::from(path), "external".to_string()));
        }

        if let Some(resource_dir) = resource_dir {
            candidates.push((
                resource_dir.join("gsr/gpu-screen-recorder"),
                "bundled".to_string(),
            ));
            candidates.push((
                resource_dir.join("gpu-screen-recorder"),
                "bundled".to_string(),
            ));
        }

        if let Ok(executable) = env::current_exe() {
            if let Some(bin_dir) = executable.parent() {
                if let Some(prefix) = bin_dir.parent() {
                    candidates.push((
                        prefix.join("libexec/moonlit/gpu-screen-recorder"),
                        "bundled".to_string(),
                    ));
                }
            }
        }

        candidates.push((
            PathBuf::from("/usr/libexec/moonlit/gpu-screen-recorder"),
            "bundled".to_string(),
        ));
        if let Some(path) = find_in_path("gpu-screen-recorder") {
            candidates.push((path, "system".to_string()));
        }

        candidates
            .into_iter()
            .find_map(|(path, origin)| {
                validate_executable_path(path).ok().map(|path| Self {
                    executable: Some(path),
                    origin: Some(origin),
                })
            })
            .unwrap_or(Self {
                executable: None,
                origin: None,
            })
    }

    fn executable(&self) -> Result<&Path, BackendError> {
        self.executable.as_deref().ok_or_else(|| {
            BackendError::backend_unavailable(
                "gpu-screen-recorder no esta disponible en PATH; usa FakeBackend",
            )
        })
    }

    fn origin(&self) -> &str {
        self.origin.as_deref().unwrap_or("none")
    }

    fn executable_dir(&self) -> Option<&Path> {
        self.executable.as_deref().and_then(Path::parent)
    }

    fn inspect(&self) -> BackendDescriptor {
        let executable = self.executable.as_deref();
        let Some(program) = executable else {
            return Self::unavailable_descriptor(
                "GSR no esta instalado; el backend simulado sigue habilitado",
            );
        };

        let version_probe = run_probe(program, &["--version"]);
        if !version_probe.success {
            return Self::unavailable_descriptor(
                version_probe
                    .detail
                    .as_deref()
                    .unwrap_or("No se pudo ejecutar GSR"),
            );
        }

        let help_probe = run_probe(program, &["--help"]);
        let monitor_probe = run_probe(program, &["--list-monitors"]);
        let note = if monitor_probe.success {
            format!(
                "GSR disponible ({}) desde {}",
                version_probe
                    .output
                    .as_deref()
                    .unwrap_or("version desconocida"),
                self.origin()
            )
        } else {
            format!(
                "GSR no pudo listar monitores: {}",
                monitor_probe
                    .detail
                    .as_deref()
                    .unwrap_or("se requiere validacion real")
            )
        };
        let codecs = detect_codecs(&format!(
            "{}\n{}",
            version_probe.raw_output, help_probe.raw_output
        ));
        BackendDescriptor {
            id: BackendId::LegacyGsr,
            display_name: "GSR legacy".to_string(),
            available: version_probe.success && monitor_probe.success,
            simulated: false,
            capabilities: BackendCapabilities {
                source_kinds: vec![CaptureSourceKind::Monitor],
                max_resolution: None,
                max_fps: None,
                encoders: vec![EncoderCapability {
                    id: EncoderPreference::Auto,
                    available: version_probe.success && monitor_probe.success,
                    reason: if codecs.is_empty() {
                        Some("GSR no reporto codecs".to_string())
                    } else {
                        None
                    },
                }],
            },
            note: Some(note),
        }
    }

    fn unavailable_descriptor(note: &str) -> BackendDescriptor {
        BackendDescriptor {
            id: BackendId::LegacyGsr,
            display_name: "GSR legacy".to_string(),
            available: false,
            simulated: false,
            capabilities: BackendCapabilities {
                source_kinds: vec![CaptureSourceKind::Monitor],
                max_resolution: None,
                max_fps: None,
                encoders: Vec::new(),
            },
            note: Some(note.to_string()),
        }
    }

    fn build_replay_command(
        &self,
        config: &ReplayConfig,
        output_dir: &Path,
    ) -> Result<CommandSpec, BackendError> {
        let resolution = config
            .resolution
            .clone()
            .unwrap_or(crate::traits::VideoResolution {
                width: 1920,
                height: 1080,
            });
        let fps = config.fps.unwrap_or(60);
        let source = if config.source_id == "legacy-monitor" {
            "portal"
        } else {
            config.source_id.as_str()
        };
        if source.trim().is_empty() || fps == 0 || resolution.width == 0 || resolution.height == 0 {
            return Err(BackendError::invalid_config("Perfil GSR invalido"));
        }

        let mut args = vec![
            OsString::from("-w"),
            OsString::from(source),
            OsString::from("-c"),
            OsString::from("mkv"),
            OsString::from("-r"),
            OsString::from(config.buffer_seconds.to_string()),
            OsString::from("-s"),
            OsString::from(format!("{}x{}", resolution.width, resolution.height)),
            OsString::from("-f"),
            OsString::from(fps.to_string()),
            OsString::from("-k"),
            OsString::from(config.codec.as_gsr_value()),
            OsString::from("-encoder"),
            OsString::from("gpu"),
            OsString::from("-fallback-cpu-encoding"),
            OsString::from("no"),
            OsString::from("-bm"),
            OsString::from("cbr"),
            OsString::from("-o"),
            output_dir.as_os_str().to_os_string(),
        ];

        if !matches!(
            config.encoder,
            EncoderPreference::Auto | EncoderPreference::Nvenc
        ) {
            return Err(BackendError::new(
                crate::traits::BackendErrorCode::Unsupported,
                "GSR legacy solo admite encoder Auto o NVENC",
                false,
            ));
        }
        args.shrink_to_fit();
        Ok(CommandSpec {
            program: self.executable()?.to_path_buf(),
            args,
        })
    }
}

pub struct LegacyGsrBackend {
    backend: GsrBackend,
    process: Option<GsrProcess>,
}

struct GsrProcess {
    child: Child,
    config: ReplayConfig,
    output_dir: PathBuf,
    logs: Arc<Mutex<VecDeque<String>>>,
}

impl LegacyGsrBackend {
    pub fn discover_with_resource_dir(resource_dir: Option<PathBuf>) -> Self {
        Self {
            backend: GsrBackend::discover_with_resource_dir(resource_dir),
            process: None,
        }
    }
}

impl ReplayBackend for LegacyGsrBackend {
    fn descriptor(&self) -> BackendDescriptor {
        self.backend.inspect()
    }

    fn list_sources(&self) -> Result<Vec<CaptureSource>, BackendError> {
        if !self.descriptor().available {
            return Err(BackendError::backend_unavailable(
                self.descriptor()
                    .note
                    .unwrap_or_else(|| "GSR no disponible".to_string()),
            ));
        }
        Ok(vec![CaptureSource {
            id: "legacy-monitor".to_string(),
            kind: CaptureSourceKind::Monitor,
            label: "Monitor via portal (GSR)".to_string(),
            is_default: true,
        }])
    }

    fn start(&mut self, config: &ReplayConfig, output_dir: &Path) -> Result<(), BackendError> {
        if self.process.is_some() {
            return Err(BackendError::invalid_state("GSR ya esta activo"));
        }
        let sources = self.list_sources()?;
        config.validate(&sources)?;
        fs::create_dir_all(output_dir)
            .map_err(|error| BackendError::io(format!("No se pudo crear la carpeta: {error}")))?;
        let command = self.backend.build_replay_command(config, output_dir)?;
        let mut process_command = Command::new(command.program);
        process_command
            .args(command.args)
            .stdin(Stdio::null())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());
        if let Some(executable_dir) = self.backend.executable_dir() {
            let mut path_entries = vec![executable_dir.to_path_buf()];
            path_entries.extend(env::split_paths(&env::var_os("PATH").unwrap_or_default()));
            if let Ok(path) = env::join_paths(path_entries) {
                process_command.env("PATH", path);
            }
        }
        let mut child = process_command
            .spawn()
            .map_err(|error| BackendError::io(format!("No se pudo iniciar GSR: {error}")))?;
        let logs = Arc::new(Mutex::new(VecDeque::with_capacity(32)));
        if let Some(stdout) = child.stdout.take() {
            spawn_log_reader(stdout, Arc::clone(&logs));
        }
        if let Some(stderr) = child.stderr.take() {
            spawn_log_reader(stderr, Arc::clone(&logs));
        }
        if let Some(status) = child
            .try_wait()
            .map_err(|error| BackendError::io(format!("No se pudo comprobar GSR: {error}")))?
        {
            return Err(BackendError::new(
                crate::traits::BackendErrorCode::BackendExited,
                format!("GSR termino inmediatamente ({status})"),
                true,
            ));
        }
        self.process = Some(GsrProcess {
            child,
            config: config.clone(),
            output_dir: output_dir.to_path_buf(),
            logs,
        });
        Ok(())
    }

    fn save_replay(&mut self) -> Result<ClipArtifact, BackendError> {
        let process = self
            .process
            .as_mut()
            .ok_or_else(|| BackendError::invalid_state("GSR no esta activo"))?;
        let existing = media_files(&process.output_dir)?;
        send_process_signal(process.child.id(), ProcessSignal::SaveReplay)?;

        let deadline = Instant::now() + Duration::from_secs(12);
        let mut stable_file: Option<(PathBuf, u64, u8)> = None;
        loop {
            if let Some(path) = media_files(&process.output_dir)?
                .into_iter()
                .find(|path| !existing.contains(path))
            {
                let size = fs::metadata(&path)
                    .map(|metadata| metadata.len())
                    .unwrap_or(0);
                if size > 0 {
                    let stable_samples = match stable_file.take() {
                        Some((previous_path, previous_size, samples))
                            if previous_path == path && previous_size == size =>
                        {
                            samples.saturating_add(1)
                        }
                        _ => 0,
                    };
                    stable_file = Some((path.clone(), size, stable_samples));
                    if stable_samples >= 2 {
                        return Ok(ClipArtifact {
                            path,
                            duration_seconds: process.config.buffer_seconds,
                            kind: ClipKind::Media,
                        });
                    }
                }
            }
            if let Some(status) = process
                .child
                .try_wait()
                .map_err(|error| BackendError::io(format!("No se pudo comprobar GSR: {error}")))?
            {
                return Err(BackendError::new(
                    crate::traits::BackendErrorCode::BackendExited,
                    format!("GSR termino antes de guardar ({status})"),
                    true,
                ));
            }
            if Instant::now() >= deadline {
                return Err(BackendError::new(
                    crate::traits::BackendErrorCode::Timeout,
                    format!(
                        "GSR no produjo un replay a tiempo: {}",
                        recent_logs(&process.logs)
                    ),
                    true,
                ));
            }
            thread::sleep(Duration::from_millis(100));
        }
    }

    fn stop(&mut self) -> Result<(), BackendError> {
        let Some(mut process) = self.process.take() else {
            return Ok(());
        };
        if process
            .child
            .try_wait()
            .map_err(|error| BackendError::io(error.to_string()))?
            .is_none()
        {
            send_process_signal(process.child.id(), ProcessSignal::Interrupt)?;
            let deadline = Instant::now() + Duration::from_secs(3);
            loop {
                if process
                    .child
                    .try_wait()
                    .map_err(|error| BackendError::io(error.to_string()))?
                    .is_some()
                {
                    break;
                }
                if Instant::now() >= deadline {
                    process
                        .child
                        .kill()
                        .map_err(|error| BackendError::io(error.to_string()))?;
                    process
                        .child
                        .wait()
                        .map_err(|error| BackendError::io(error.to_string()))?;
                    break;
                }
                thread::sleep(Duration::from_millis(50));
            }
        }
        Ok(())
    }
}

impl Drop for LegacyGsrBackend {
    fn drop(&mut self) {
        let _ = self.stop();
    }
}

fn spawn_log_reader<R>(reader: R, logs: Arc<Mutex<VecDeque<String>>>)
where
    R: Read + Send + 'static,
{
    thread::spawn(move || {
        let mut reader = BufReader::new(reader);
        let mut line = String::new();
        while reader.read_line(&mut line).unwrap_or_default() > 0 {
            if let Ok(mut entries) = logs.lock() {
                let cleaned = line.trim().chars().take(4096).collect::<String>();
                if !cleaned.is_empty() {
                    if entries.len() == 32 {
                        entries.pop_front();
                    }
                    entries.push_back(cleaned);
                }
            }
            line.clear();
        }
    });
}

fn recent_logs(logs: &Arc<Mutex<VecDeque<String>>>) -> String {
    logs.lock()
        .map(|entries| entries.iter().cloned().collect::<Vec<_>>().join(" | "))
        .unwrap_or_default()
}

fn media_files(directory: &Path) -> Result<std::collections::HashSet<PathBuf>, BackendError> {
    Ok(fs::read_dir(directory)
        .map_err(|error| BackendError::io(format!("No se pudo leer la carpeta: {error}")))?
        .filter_map(Result::ok)
        .map(|entry| entry.path())
        .filter(|path| {
            matches!(
                path.extension().and_then(|extension| extension.to_str()),
                Some("mkv" | "mp4" | "webm")
            )
        })
        .collect())
}

#[derive(Clone, Copy)]
enum ProcessSignal {
    SaveReplay,
    Interrupt,
}

fn send_process_signal(pid: u32, signal: ProcessSignal) -> Result<(), BackendError> {
    use nix::sys::signal::{kill, Signal};
    use nix::unistd::Pid;

    let signal = match signal {
        ProcessSignal::SaveReplay => Signal::SIGUSR1,
        ProcessSignal::Interrupt => Signal::SIGINT,
    };
    kill(Pid::from_raw(pid as i32), signal)
        .map_err(|error| BackendError::io(format!("No se pudo enviar la senal a GSR: {error}")))
}

fn run_probe(program: &Path, args: &[&str]) -> ProbeResult {
    match Command::new(program).args(args).output() {
        Ok(output) => probe_output(output),
        Err(error) => ProbeResult {
            success: false,
            output: None,
            detail: Some(error.to_string()),
            raw_output: String::new(),
        },
    }
}

struct ProbeResult {
    success: bool,
    output: Option<String>,
    detail: Option<String>,
    raw_output: String,
}

fn probe_output(output: Output) -> ProbeResult {
    let raw_output = format!(
        "{}\n{}",
        bounded_output(&output.stdout),
        bounded_output(&output.stderr)
    );
    ProbeResult {
        success: output.status.success(),
        output: first_line(&output.stdout).or_else(|| first_line(&output.stderr)),
        detail: first_line(&output.stderr).or_else(|| first_line(&output.stdout)),
        raw_output,
    }
}

fn bounded_output(bytes: &[u8]) -> String {
    String::from_utf8_lossy(bytes).chars().take(4096).collect()
}

fn first_line(bytes: &[u8]) -> Option<String> {
    String::from_utf8_lossy(bytes)
        .lines()
        .map(str::trim)
        .find(|line| !line.is_empty())
        .map(|line| line.chars().take(180).collect())
}

fn detect_codecs(output: &str) -> Vec<String> {
    let lower = output.to_ascii_lowercase();
    [
        ("h264", "h264"),
        ("h.264", "h264"),
        ("hevc", "hevc"),
        ("h265", "hevc"),
        ("h.265", "hevc"),
        ("av1", "av1"),
    ]
    .into_iter()
    .filter_map(|(needle, codec)| lower.contains(needle).then_some(codec.to_string()))
    .fold(Vec::new(), |mut codecs, codec| {
        if !codecs.contains(&codec) {
            codecs.push(codec);
        }
        codecs
    })
}

fn find_in_path(program: &str) -> Option<PathBuf> {
    env::split_paths(&env::var_os("PATH")?)
        .map(|directory| directory.join(program))
        .find(|candidate| is_executable_file(candidate))
}

fn validate_executable_path(path: PathBuf) -> Result<PathBuf, BackendError> {
    if !path.is_absolute() {
        return Err(BackendError::invalid_config(
            "La ruta de GSR debe ser absoluta",
        ));
    }
    let path = path.canonicalize().map_err(|error| {
        BackendError::io(format!("No se pudo resolver la ruta de GSR: {error}"))
    })?;
    if !is_executable_file(&path) {
        return Err(BackendError::invalid_config(
            "La ruta de GSR no es un ejecutable regular",
        ));
    }
    let permissions = fs::metadata(&path)
        .map_err(|error| BackendError::io(format!("No se pudieron leer permisos: {error}")))?
        .permissions()
        .mode();
    if permissions & 0o022 != 0 {
        return Err(BackendError::invalid_config(
            "La ruta de GSR es escribible por otro usuario",
        ));
    }
    Ok(path)
}

fn is_executable_file(path: &Path) -> bool {
    fs::metadata(path)
        .map(|metadata| metadata.is_file() && metadata.permissions().mode() & 0o111 != 0)
        .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use std::path::PathBuf;

    use super::{validate_executable_path, GsrBackend, ReplayBackend};
    use crate::traits::{BackendErrorCode, BackendId, ReplayConfig, VideoCodec};

    #[test]
    fn gsr_descriptor_is_linux_specific_and_stable() {
        let backend = GsrBackend {
            executable: None,
            origin: None,
        };
        assert_eq!(backend.inspect().id, BackendId::LegacyGsr);
        assert!(!backend.descriptor().available);
    }

    #[test]
    fn gsr_command_uses_safe_argument_boundaries() {
        let backend = GsrBackend {
            executable: Some(PathBuf::from("/tmp/gpu-screen-recorder")),
            origin: Some("external".to_string()),
        };
        let config = ReplayConfig {
            source_id: "legacy-monitor".to_string(),
            codec: VideoCodec::Hevc,
            buffer_seconds: 60,
            resolution: Some(crate::traits::VideoResolution {
                width: 1920,
                height: 1080,
            }),
            fps: Some(60),
            ..ReplayConfig::default()
        };
        let command = backend
            .build_replay_command(&config, Path::new("/tmp/MoonLit clips"))
            .expect("valid config");
        assert_eq!(command.program, PathBuf::from("/tmp/gpu-screen-recorder"));
        assert!(command
            .args
            .windows(2)
            .any(|pair| pair[0] == "-k" && pair[1] == "hevc"));
        assert!(command.args.iter().any(|arg| arg == "/tmp/MoonLit clips"));
    }

    #[test]
    fn relative_external_path_is_rejected() {
        let error = validate_executable_path(PathBuf::from("gpu-screen-recorder"))
            .expect_err("relative path");
        assert_eq!(error.code, BackendErrorCode::InvalidConfig);
    }
}
