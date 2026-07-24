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

use serde::Serialize;
use sha2::{Digest, Sha256};
use tauri::{AppHandle, Manager};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum VideoCodec {
    H264,
    #[allow(dead_code)]
    Hevc,
}

impl VideoCodec {
    fn as_gsr_value(self) -> &'static str {
        match self {
            Self::H264 => "h264",
            Self::Hevc => "hevc",
        }
    }
}

#[derive(Clone, Debug)]
pub struct CaptureProfile {
    pub source: String,
    pub output_dir: PathBuf,
    pub codec: VideoCodec,
    pub width: u32,
    pub height: u32,
    pub fps: u32,
    pub buffer_seconds: u32,
    pub audio_sources: Vec<String>,
}

impl Default for CaptureProfile {
    fn default() -> Self {
        Self {
            source: "portal".to_string(),
            output_dir: PathBuf::from("."),
            codec: VideoCodec::H264,
            width: 1920,
            height: 1080,
            fps: 60,
            buffer_seconds: 60,
            audio_sources: Vec::new(),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CommandSpec {
    pub program: PathBuf,
    pub args: Vec<OsString>,
}

pub trait RecorderBackend {
    fn name(&self) -> &'static str;
    fn build_replay_command(&self, profile: &CaptureProfile) -> Result<CommandSpec, String>;
}

#[derive(Clone, Debug)]
pub struct CapturedClip {
    pub path: PathBuf,
    pub duration_seconds: u32,
    pub kind: String,
}

pub trait CaptureBackend: Send {
    fn name(&self) -> &'static str;
    fn start(&mut self, profile: &CaptureProfile) -> Result<(), String>;
    fn save_clip(&mut self) -> Result<CapturedClip, String>;
    fn stop(&mut self) -> Result<(), String>;
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

        Self::from_candidates(candidates)
    }

    pub fn from_external_path(path: PathBuf) -> Result<Self, String> {
        let path = validate_executable_path(path)?;
        Ok(Self {
            executable: Some(path),
            origin: Some("external".to_string()),
        })
    }

    fn from_candidates(candidates: Vec<(PathBuf, String)>) -> Self {
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

    fn executable(&self) -> Result<&Path, String> {
        self.executable
            .as_deref()
            .ok_or_else(|| "gpu-screen-recorder no está disponible en PATH".to_string())
    }

    fn origin(&self) -> &str {
        self.origin.as_deref().unwrap_or("none")
    }

    fn executable_dir(&self) -> Option<&Path> {
        self.executable.as_deref().and_then(Path::parent)
    }
}

pub struct GsrRecorder {
    backend: GsrBackend,
    process: Option<GsrProcess>,
}

struct GsrProcess {
    child: Child,
    profile: CaptureProfile,
    logs: Arc<Mutex<VecDeque<String>>>,
}

impl GsrRecorder {
    pub fn discover_with_resource_dir(resource_dir: Option<PathBuf>) -> Self {
        Self {
            backend: GsrBackend::discover_with_resource_dir(resource_dir),
            process: None,
        }
    }

    pub fn from_external_path(path: PathBuf) -> Result<Self, String> {
        Ok(Self {
            backend: GsrBackend::from_external_path(path)?,
            process: None,
        })
    }

    pub fn status(&self) -> NativeBackendStatus {
        self.backend.inspect()
    }

    fn recent_logs(&self) -> String {
        self.process
            .as_ref()
            .and_then(|process| process.logs.lock().ok())
            .map(|logs| logs.iter().cloned().collect::<Vec<_>>().join(" | "))
            .unwrap_or_default()
    }

    fn stop_process(&mut self) -> Result<(), String> {
        let Some(mut process) = self.process.take() else {
            return Ok(());
        };

        if process
            .child
            .try_wait()
            .map_err(|error| error.to_string())?
            .is_none()
        {
            if let Err(signal_error) =
                send_process_signal(process.child.id(), ProcessSignal::Interrupt)
            {
                let kill_result = process.child.kill();
                let wait_result = process.child.wait();
                return Err(format!(
                    "No se pudo solicitar el cierre limpio de GSR ({signal_error}); terminación forzada: {}{}",
                    kill_result
                        .err()
                        .map(|error| error.to_string())
                        .unwrap_or_else(|| "correcta".to_string()),
                    wait_result
                        .err()
                        .map(|error| format!("; no se pudo recoger el proceso: {error}"))
                        .unwrap_or_default()
                ));
            }
            let deadline = Instant::now() + Duration::from_secs(3);
            loop {
                if process
                    .child
                    .try_wait()
                    .map_err(|error| error.to_string())?
                    .is_some()
                {
                    break;
                }
                if Instant::now() >= deadline {
                    process
                        .child
                        .kill()
                        .map_err(|error| format!("No se pudo finalizar GSR: {error}"))?;
                    process
                        .child
                        .wait()
                        .map_err(|error| format!("No se pudo recoger GSR: {error}"))?;
                    break;
                }
                thread::sleep(Duration::from_millis(50));
            }
        }
        Ok(())
    }
}

impl CaptureBackend for GsrRecorder {
    fn name(&self) -> &'static str {
        "gpu-screen-recorder"
    }

    fn start(&mut self, profile: &CaptureProfile) -> Result<(), String> {
        if self.process.is_some() {
            return Err("El grabador GSR ya está activo".to_string());
        }

        let status = self.backend.inspect();
        if !status.available {
            return Err(status.note);
        }
        fs::create_dir_all(&profile.output_dir)
            .map_err(|error| format!("No se pudo crear la carpeta de captura: {error}"))?;
        let command = self.backend.build_replay_command(profile)?;
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
            .map_err(|error| format!("No se pudo iniciar gpu-screen-recorder: {error}"))?;

        let logs = Arc::new(Mutex::new(VecDeque::with_capacity(32)));
        if let Some(stdout) = child.stdout.take() {
            spawn_log_reader(stdout, Arc::clone(&logs));
        }
        if let Some(stderr) = child.stderr.take() {
            spawn_log_reader(stderr, Arc::clone(&logs));
        }

        if let Some(status) = child
            .try_wait()
            .map_err(|error| format!("No se pudo comprobar GSR: {error}"))?
        {
            return Err(format!(
                "GSR terminó inmediatamente ({status}); {}",
                logs.lock()
                    .map(|items| items.iter().cloned().collect::<Vec<_>>().join(" | "))
                    .unwrap_or_default()
            ));
        }

        self.process = Some(GsrProcess {
            child,
            profile: profile.clone(),
            logs,
        });
        Ok(())
    }

    fn save_clip(&mut self) -> Result<CapturedClip, String> {
        let process = self
            .process
            .as_mut()
            .ok_or_else(|| "El grabador GSR no está activo".to_string())?;
        let existing = media_files(&process.profile.output_dir)?;
        send_process_signal(process.child.id(), ProcessSignal::SaveReplay)?;

        let deadline = Instant::now() + Duration::from_secs(12);
        let mut stable_file: Option<(PathBuf, u64, u8)> = None;
        loop {
            if let Some(path) = media_files(&process.profile.output_dir)?
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
                        return Ok(CapturedClip {
                            path,
                            duration_seconds: process.profile.buffer_seconds,
                            kind: "native".to_string(),
                        });
                    }
                }
            }
            if let Some(status) = process
                .child
                .try_wait()
                .map_err(|error| format!("No se pudo comprobar GSR: {error}"))?
            {
                return Err(format!(
                    "GSR terminó antes de guardar el replay ({status}); {}",
                    self.recent_logs()
                ));
            }
            if Instant::now() >= deadline {
                return Err(format!(
                    "GSR no produjo un archivo de replay a tiempo; {}",
                    self.recent_logs()
                ));
            }
            thread::sleep(Duration::from_millis(100));
        }
    }

    fn stop(&mut self) -> Result<(), String> {
        self.stop_process()
    }
}

impl Drop for GsrRecorder {
    fn drop(&mut self) {
        let _ = self.stop_process();
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
                let cleaned = line.trim().to_string();
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

fn media_files(directory: &Path) -> Result<std::collections::HashSet<PathBuf>, String> {
    let entries = fs::read_dir(directory)
        .map_err(|error| format!("No se pudo leer la carpeta de capturas: {error}"))?;
    Ok(entries
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

#[cfg(unix)]
fn send_process_signal(pid: u32, signal: ProcessSignal) -> Result<(), String> {
    use nix::sys::signal::{kill, Signal};
    use nix::unistd::Pid;

    let signal = match signal {
        ProcessSignal::SaveReplay => Signal::SIGUSR1,
        ProcessSignal::Interrupt => Signal::SIGINT,
    };
    kill(Pid::from_raw(pid as i32), signal)
        .map_err(|error| format!("No se pudo enviar la señal a GSR: {error}"))
}

#[cfg(not(unix))]
fn send_process_signal(_pid: u32, _signal: ProcessSignal) -> Result<(), String> {
    Err("El control por señales de GSR sólo está soportado en Linux".to_string())
}

impl RecorderBackend for GsrBackend {
    fn name(&self) -> &'static str {
        "gpu-screen-recorder"
    }

    fn build_replay_command(&self, profile: &CaptureProfile) -> Result<CommandSpec, String> {
        if profile.source.trim().is_empty() {
            return Err("La fuente de captura no puede estar vacía".to_string());
        }
        if !(2..=86_400).contains(&profile.buffer_seconds) {
            return Err("El buffer debe estar entre 2 y 86400 segundos".to_string());
        }
        if profile.width == 0 || profile.height == 0 || profile.fps == 0 {
            return Err("La resolución y los FPS deben ser mayores que cero".to_string());
        }

        let mut args = vec![
            OsString::from("-w"),
            OsString::from(&profile.source),
            OsString::from("-c"),
            OsString::from("mkv"),
            OsString::from("-r"),
            OsString::from(profile.buffer_seconds.to_string()),
            OsString::from("-s"),
            OsString::from(format!("{}x{}", profile.width, profile.height)),
            OsString::from("-f"),
            OsString::from(profile.fps.to_string()),
            OsString::from("-k"),
            OsString::from(profile.codec.as_gsr_value()),
            OsString::from("-encoder"),
            OsString::from("gpu"),
            OsString::from("-fallback-cpu-encoding"),
            OsString::from("no"),
            OsString::from("-bm"),
            OsString::from("cbr"),
            OsString::from("-o"),
            profile.output_dir.as_os_str().to_os_string(),
        ];

        for source in &profile.audio_sources {
            if source.trim().is_empty() {
                return Err("Una fuente de audio no puede estar vacía".to_string());
            }
            args.push(OsString::from("-a"));
            args.push(OsString::from(source));
        }

        Ok(CommandSpec {
            program: self.executable()?.to_path_buf(),
            args,
        })
    }
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct NativeBackendStatus {
    pub name: String,
    pub available: bool,
    pub executable: Option<String>,
    pub origin: String,
    pub sha256: Option<String>,
    pub status: String,
    pub version: Option<String>,
    pub codecs: Vec<String>,
    pub note: String,
}

impl GsrBackend {
    pub fn inspect(&self) -> NativeBackendStatus {
        let executable = self
            .executable
            .as_ref()
            .map(|path| path.to_string_lossy().into_owned());

        let Some(program) = self.executable.as_deref() else {
            return NativeBackendStatus {
                name: self.name().to_string(),
                available: false,
                executable,
                origin: self.origin().to_string(),
                sha256: None,
                status: "missing".to_string(),
                version: None,
                codecs: Vec::new(),
                note: "No instalado; el FakeBackend sigue habilitado".to_string(),
            };
        };

        let version_probe = run_probe(program, &["--version"]);
        if !version_probe.success {
            return NativeBackendStatus {
                name: self.name().to_string(),
                available: false,
                executable,
                origin: self.origin().to_string(),
                sha256: file_sha256(program),
                status: "failed".to_string(),
                version: version_probe.output,
                codecs: Vec::new(),
                note: version_probe
                    .detail
                    .unwrap_or_else(|| "No se pudo ejecutar --version".to_string()),
            };
        }

        let help_probe = run_probe(program, &["--help"]);
        let capability_probe = run_probe(program, &["--list-monitors"]);
        let combined_output = format!("{}\n{}", version_probe.raw_output, help_probe.raw_output);
        let codecs = if capability_probe.success {
            detect_codecs(&combined_output)
        } else {
            Vec::new()
        };
        let status = if capability_probe.success {
            "ready"
        } else {
            "degraded"
        };
        let note = if capability_probe.success {
            "GSR responde y puede consultar los monitores disponibles".to_string()
        } else {
            format!(
                "GSR está instalado, pero no pudo consultar los monitores: {}",
                capability_probe
                    .detail
                    .unwrap_or_else(|| "se requiere validación real".to_string())
            )
        };

        NativeBackendStatus {
            name: self.name().to_string(),
            available: version_probe.success && capability_probe.success,
            executable,
            origin: self.origin().to_string(),
            sha256: file_sha256(program),
            status: status.to_string(),
            version: version_probe.output,
            codecs,
            note,
        }
    }
}

#[tauri::command]
pub fn get_capture_backend(app: AppHandle) -> NativeBackendStatus {
    let backend = GsrBackend::discover_with_resource_dir(app.path().resource_dir().ok());
    let mut status = backend.inspect();
    if status.available {
        status.note = match backend.build_replay_command(&CaptureProfile::default()) {
            Ok(_) => status.note,
            Err(error) => format!("Perfil predeterminado inválido: {error}"),
        };
    }
    status
}

struct ProbeResult {
    success: bool,
    output: Option<String>,
    detail: Option<String>,
    raw_output: String,
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
    let mut codecs = Vec::new();
    for (needle, codec) in [
        ("h264", "h264"),
        ("h.264", "h264"),
        ("hevc", "hevc"),
        ("h265", "hevc"),
        ("h.265", "hevc"),
        ("av1", "av1"),
    ] {
        if lower.contains(needle) && !codecs.iter().any(|item| item == codec) {
            codecs.push(codec.to_string());
        }
    }
    codecs
}

fn find_in_path(program: &str) -> Option<PathBuf> {
    env::split_paths(&env::var_os("PATH")?)
        .map(|directory| directory.join(program))
        .find(|candidate| is_executable_file(candidate))
}

fn validate_executable_path(path: PathBuf) -> Result<PathBuf, String> {
    if !path.is_absolute() {
        return Err("La ruta de GSR debe ser absoluta".to_string());
    }
    let path = path
        .canonicalize()
        .map_err(|error| format!("No se pudo resolver la ruta de GSR: {error}"))?;
    if !is_executable_file(&path) {
        return Err("La ruta de GSR no es un ejecutable regular".to_string());
    }
    let permissions = fs::metadata(&path)
        .map_err(|error| format!("No se pudieron leer los permisos de GSR: {error}"))?
        .permissions()
        .mode();
    if permissions & 0o022 != 0 {
        return Err("La ruta de GSR es escribible por otro usuario".to_string());
    }
    Ok(path)
}

fn is_executable_file(path: &Path) -> bool {
    fs::metadata(path)
        .map(|metadata| metadata.is_file() && metadata.permissions().mode() & 0o111 != 0)
        .unwrap_or(false)
}

fn file_sha256(path: &Path) -> Option<String> {
    let contents = fs::read(path).ok()?;
    let digest = Sha256::digest(contents);
    Some(digest.iter().map(|byte| format!("{byte:02x}")).collect())
}

#[cfg(test)]
mod tests {
    use std::path::PathBuf;

    use super::{detect_codecs, CaptureProfile, GsrBackend, RecorderBackend, VideoCodec};

    #[test]
    fn gsr_command_uses_safe_argument_boundaries() {
        let backend = GsrBackend {
            executable: Some(PathBuf::from("/tmp/gpu-screen-recorder")),
            origin: Some("external".to_string()),
        };
        let profile = CaptureProfile {
            source: "portal;name=game with spaces".to_string(),
            output_dir: PathBuf::from("/tmp/MoonLit clips"),
            codec: VideoCodec::Hevc,
            width: 1920,
            height: 1080,
            fps: 60,
            buffer_seconds: 60,
            audio_sources: vec!["app:Discord".to_string(), "default_input".to_string()],
        };

        let command = backend
            .build_replay_command(&profile)
            .expect("valid profile");
        assert_eq!(command.program, PathBuf::from("/tmp/gpu-screen-recorder"));
        assert!(command
            .args
            .windows(2)
            .any(|pair| pair[0] == "-k" && pair[1] == "hevc"));
        assert!(command
            .args
            .windows(2)
            .any(|pair| pair[0] == "-a" && pair[1] == "app:Discord"));
        assert!(command
            .args
            .windows(2)
            .any(|pair| pair[0] == "-o" && pair[1] == "/tmp/MoonLit clips"));
        assert!(command.args.iter().any(|arg| arg == "/tmp/MoonLit clips"));
    }

    #[test]
    fn gsr_command_rejects_empty_audio_sources() {
        let backend = GsrBackend {
            executable: Some(PathBuf::from("/tmp/gpu-screen-recorder")),
            origin: Some("external".to_string()),
        };
        let profile = CaptureProfile {
            audio_sources: vec!["  ".to_string()],
            ..CaptureProfile::default()
        };

        assert!(backend.build_replay_command(&profile).is_err());
    }

    #[test]
    fn codec_detection_normalizes_h265_to_hevc() {
        assert_eq!(detect_codecs("H.264 and H265 available"), ["h264", "hevc"]);
    }

    #[test]
    fn missing_gsr_is_reported_without_starting_a_process() {
        let backend = GsrBackend {
            executable: None,
            origin: None,
        };
        let status = backend.inspect();
        assert!(!status.available);
        assert_eq!(status.status, "missing");
    }

    #[test]
    fn external_gsr_requires_an_absolute_executable_path() {
        let error = GsrBackend::from_external_path(PathBuf::from("gpu-screen-recorder"))
            .expect_err("relative path must be rejected");
        assert!(error.contains("absoluta"));
    }
}
