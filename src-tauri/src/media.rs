use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

#[cfg(not(test))]
use tauri::State;

#[cfg(test)]
use crate::backends::host_library as library_module;
#[cfg(test)]
use crate::backends::host_library::ClipMetadata;
#[cfg(not(test))]
use crate::library as library_module;
#[cfg(not(test))]
use crate::library::{ClipMetadata, LibraryState};

const DEFAULT_PROXY_TIMEOUT: Duration = Duration::from_secs(300);
const STDERR_CAPTURE_LIMIT: usize = 64 * 1024;
const POLL_INTERVAL: Duration = Duration::from_millis(20);
const CHILD_CLEANUP_TIMEOUT: Duration = Duration::from_millis(500);
const STDERR_JOIN_TIMEOUT: Duration = Duration::from_millis(500);

#[derive(Clone)]
pub struct MediaJobService {
    ffmpeg: Option<PathBuf>,
    proxy_root: PathBuf,
    timeout: Duration,
    cancellation: Arc<AtomicBool>,
}

impl MediaJobService {
    pub fn new(resource_dir: Option<PathBuf>, proxy_root: PathBuf) -> Result<Self, String> {
        Self::new_with_timeout(resource_dir, proxy_root, DEFAULT_PROXY_TIMEOUT)
    }

    pub fn new_with_timeout(
        resource_dir: Option<PathBuf>,
        proxy_root: PathBuf,
        timeout: Duration,
    ) -> Result<Self, String> {
        if !proxy_root.is_absolute() {
            return Err("La carpeta de proxies debe ser una ruta absoluta".to_string());
        }
        if timeout.is_zero() {
            return Err("El tiempo limite del proxy debe ser mayor que cero".to_string());
        }
        let proxy_root = library_module::prepare_registered_root(&proxy_root)?;

        let ffmpeg = match resource_dir {
            Some(resource_dir) => {
                if !resource_dir.is_absolute() {
                    return Err("El directorio de recursos debe ser absoluto".to_string());
                }
                let executable = resource_dir.join("runtime/obs/bin/64bit/ffmpeg.exe");
                match std::fs::symlink_metadata(&executable) {
                    Ok(metadata)
                        if !unsafe_metadata(&metadata) && metadata.file_type().is_file() =>
                    {
                        Some(validate_worker_path(&executable)?)
                    }
                    Ok(_) => {
                        return Err("El worker multimedia no es un archivo regular".to_string())
                    }
                    Err(error) if error.kind() == std::io::ErrorKind::NotFound => None,
                    Err(error) => return Err(error.to_string()),
                }
            }
            None => None,
        };

        Ok(Self {
            ffmpeg,
            proxy_root,
            timeout,
            cancellation: Arc::new(AtomicBool::new(false)),
        })
    }

    #[allow(dead_code)]
    pub fn cancel(&self) {
        self.cancellation.store(true, Ordering::Release);
    }

    pub fn create_proxy(&self, clip: &ClipMetadata) -> Result<ClipMetadata, String> {
        self.cancellation.store(false, Ordering::Release);
        if clip.codec != "hevc" {
            return Ok(clip.clone());
        }
        let input = library_module::registered_file(Path::new(&clip.path))?;
        // Keep every parent directory stable while the worker is using the
        // path. The final input is revalidated after the guards are acquired;
        // this is the strongest no-reparse guarantee available without
        // transporting an open Windows file handle to FFmpeg.
        let _input_guard = library_module::hold_registered_path(&input)?;
        let input_after_guard = library_module::registered_file(Path::new(&clip.path))?;
        if input_after_guard != input {
            return Err("La identidad del clip de entrada cambio".to_string());
        }
        let ffmpeg = self
            .ffmpeg
            .as_ref()
            .ok_or_else(|| "El runtime de proxy H.264 no esta instalado".to_string())?;
        let _proxy_root_guard = library_module::hold_registered_directory(&self.proxy_root)?;
        validate_clip_id(&clip.id)?;
        let output = self.proxy_root.join(format!("{}.mp4", clip.id));
        let temporary = self.proxy_root.join(format!("{}.mp4.partial", clip.id));
        validate_proxy_path(&output)?;
        validate_proxy_path(&temporary)?;

        if let Some(metadata) = symlink_metadata_if_present(&output)? {
            if unsafe_metadata(&metadata) || !metadata.file_type().is_file() {
                return Err("El destino del proxy no es un archivo regular".to_string());
            }
            return Err("El proxy ya existe; no se sobrescribira".to_string());
        }
        if symlink_metadata_if_present(&temporary)?.is_some() {
            remove_proxy_file(&temporary)?;
        }

        let mut child = Command::new(ffmpeg)
            .args(["-hide_banner", "-loglevel", "error", "-nostdin", "-n", "-i"])
            .arg(&input)
            .args([
                "-map",
                "0:v:0",
                "-map",
                "0:a?",
                "-vf",
                "scale=min(1280\\,iw):-2",
                "-c:v",
                "libx264",
                "-preset",
                "veryfast",
                "-c:a",
                "aac",
                "-b:a",
                "128k",
            ])
            .arg(&temporary)
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::piped())
            .spawn()
            .map_err(|error| format!("No se pudo iniciar el worker multimedia: {error}"))?;
        let stderr = match child.stderr.take() {
            Some(stderr) => stderr,
            None => {
                terminate_child(&mut child);
                return Err("El worker multimedia no proporciono stderr".to_string());
            }
        };
        let (stderr_join, stderr_done) = spawn_stderr_reader(stderr).map_err(|error| {
            terminate_child(&mut child);
            error
        })?;

        let started = Instant::now();
        let status = loop {
            if self.cancellation.load(Ordering::Acquire) {
                terminate_child(&mut child);
                let stderr = join_stderr(stderr_join, &stderr_done);
                let cleanup = remove_proxy_file(&temporary);
                return Err(format_worker_error(
                    &cleanup_error("El trabajo de proxy fue cancelado", cleanup),
                    &stderr,
                ));
            }
            if started.elapsed() >= self.timeout {
                terminate_child(&mut child);
                let stderr = join_stderr(stderr_join, &stderr_done);
                let cleanup = remove_proxy_file(&temporary);
                return Err(format_worker_error(
                    &cleanup_error("El trabajo de proxy excedio el tiempo limite", cleanup),
                    &stderr,
                ));
            }
            match child.try_wait() {
                Ok(Some(status)) => break status,
                Ok(None) => thread::sleep(POLL_INTERVAL),
                Err(error) => {
                    terminate_child(&mut child);
                    let stderr = join_stderr(stderr_join, &stderr_done);
                    let cleanup = remove_proxy_file(&temporary);
                    return Err(format_worker_error(
                        &cleanup_error(
                            &format!("No se pudo esperar al worker multimedia: {error}"),
                            cleanup,
                        ),
                        &stderr,
                    ));
                }
            }
        };
        let stderr = join_stderr(stderr_join, &stderr_done);
        if !status.success() {
            let cleanup = remove_proxy_file(&temporary);
            return Err(format_worker_error(
                &cleanup_error(&format!("FFmpeg termino con estado {status}"), cleanup),
                &stderr,
            ));
        }
        if self.cancellation.load(Ordering::Acquire) {
            let cleanup = remove_proxy_file(&temporary);
            return Err(format_worker_error(
                &cleanup_error("El trabajo de proxy fue cancelado", cleanup),
                &stderr,
            ));
        }

        let temporary_path = library_module::registered_file(&temporary)?;
        let temporary_metadata = std::fs::metadata(&temporary_path).map_err(|error| {
            let _ = remove_proxy_file(&temporary_path);
            error.to_string()
        })?;
        if temporary_metadata.len() == 0 {
            let cleanup = remove_proxy_file(&temporary_path);
            return Err(cleanup_error(
                "El worker multimedia produjo un proxy vacio",
                cleanup,
            ));
        }
        let final_path = match finalize_proxy(&temporary_path, &output) {
            Ok(path) => path,
            Err(error) => {
                let cleanup = remove_proxy_file(&temporary_path);
                return Err(cleanup_error(&error, cleanup));
            }
        };
        let mut result = clip.clone();
        result.proxy_path = Some(final_path.to_string_lossy().into_owned());
        result.proxy_status = "ready".to_string();
        Ok(result)
    }
}

pub struct MediaJobState(pub Mutex<MediaJobService>);

#[cfg(not(test))]
#[tauri::command]
pub fn create_clip_proxy(
    media: State<'_, MediaJobState>,
    library: State<'_, LibraryState>,
    id: String,
) -> Result<ClipMetadata, String> {
    let clip = library
        .0
        .lock()
        .map_err(|_| "La biblioteca esta bloqueada".to_string())?
        .get(&id)?
        .ok_or_else(|| "Clip no encontrado".to_string())?;
    if clip.codec != "hevc" {
        return Ok(clip);
    }
    if clip.proxy_status == "ready" {
        if let Some(path) = clip.proxy_path.as_deref() {
            if library_module::registered_file(Path::new(path)).is_ok() {
                return Ok(clip);
            }
        }
    }
    library
        .0
        .lock()
        .map_err(|_| "La biblioteca esta bloqueada".to_string())?
        .set_proxy(&id, None, "processing")?;
    let result = media
        .0
        .lock()
        .map_err(|_| "El worker multimedia esta bloqueado".to_string())?
        .create_proxy(&clip);
    match result {
        Ok(updated) => {
            let path = updated.proxy_path.clone();
            let update_result = library
                .0
                .lock()
                .map_err(|_| "La biblioteca esta bloqueada".to_string())?
                .set_proxy(&id, path.as_deref().map(Path::new), "ready");
            if let Err(error) = update_result {
                if let Some(path) = path {
                    let _ = remove_proxy_file(Path::new(&path));
                }
                let _ = library
                    .0
                    .lock()
                    .ok()
                    .and_then(|store| store.set_proxy(&id, None, "failed").ok());
                return Err(error);
            }
            Ok(updated)
        }
        Err(error) => {
            let _ = library
                .0
                .lock()
                .ok()
                .and_then(|store| store.set_proxy(&id, None, "failed").ok());
            Err(error)
        }
    }
}

fn validate_worker_path(path: &Path) -> Result<PathBuf, String> {
    if !path.is_absolute() {
        return Err("El worker multimedia debe usar una ruta absoluta".to_string());
    }
    ensure_no_reparse_components(path)?;
    let metadata = std::fs::symlink_metadata(path).map_err(|error| error.to_string())?;
    if unsafe_metadata(&metadata) || !metadata.file_type().is_file() {
        return Err("El worker multimedia no es un archivo regular".to_string());
    }
    std::fs::canonicalize(path).map_err(|error| error.to_string())
}

fn ensure_no_reparse_components(path: &Path) -> Result<(), String> {
    for ancestor in path.ancestors() {
        match std::fs::symlink_metadata(ancestor) {
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

fn validate_proxy_path(path: &Path) -> Result<(), String> {
    if !path.is_absolute() {
        return Err("La ruta del proxy debe ser absoluta".to_string());
    }
    library_module::validate_registered_path(path, false).map(|_| ())
}

fn validate_clip_id(id: &str) -> Result<(), String> {
    if id.is_empty()
        || !id
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || byte == b'-' || byte == b'_')
    {
        return Err("El identificador del clip no es seguro para un nombre de archivo".to_string());
    }
    Ok(())
}

fn symlink_metadata_if_present(path: &Path) -> Result<Option<std::fs::Metadata>, String> {
    match std::fs::symlink_metadata(path) {
        Ok(metadata) => Ok(Some(metadata)),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(None),
        Err(error) => Err(error.to_string()),
    }
}

fn remove_proxy_file(path: &Path) -> Result<(), String> {
    library_module::remove_registered_file(path)
}

fn terminate_child(child: &mut Child) {
    let _ = child.kill();
    let deadline = Instant::now() + CHILD_CLEANUP_TIMEOUT;
    loop {
        match child.try_wait() {
            Ok(Some(_)) | Err(_) => return,
            Ok(None) if Instant::now() >= deadline => return,
            Ok(None) => thread::sleep(Duration::from_millis(10)),
        }
    }
}

fn drain_stderr(mut stderr: impl Read) -> Vec<u8> {
    let mut captured = Vec::with_capacity(STDERR_CAPTURE_LIMIT.min(4096));
    let mut buffer = [0u8; 4096];
    loop {
        match stderr.read(&mut buffer) {
            Ok(0) => break,
            Ok(read) => {
                if captured.len() < STDERR_CAPTURE_LIMIT {
                    let remaining = STDERR_CAPTURE_LIMIT - captured.len();
                    captured.extend_from_slice(&buffer[..read.min(remaining)]);
                }
            }
            Err(_) => break,
        }
    }
    captured
}

fn spawn_stderr_reader(
    stderr: impl Read + Send + 'static,
) -> Result<(thread::JoinHandle<Vec<u8>>, Arc<AtomicBool>), String> {
    let done = Arc::new(AtomicBool::new(false));
    let done_signal = Arc::clone(&done);
    let join = thread::Builder::new()
        .name("moonlit-proxy-stderr".to_string())
        .spawn(move || {
            let captured = drain_stderr(stderr);
            done_signal.store(true, Ordering::Release);
            captured
        })
        .map_err(|error| format!("No se pudo iniciar el lector stderr: {error}"))?;
    Ok((join, done))
}

fn join_stderr(join: thread::JoinHandle<Vec<u8>>, done: &AtomicBool) -> String {
    let deadline = Instant::now() + STDERR_JOIN_TIMEOUT;
    while !done.load(Ordering::Acquire) && Instant::now() < deadline {
        thread::sleep(Duration::from_millis(10));
    }
    let output = if done.load(Ordering::Acquire) {
        join.join().unwrap_or_default()
    } else {
        // A descendant can inherit stderr and keep the pipe open after the
        // direct child is gone. Dropping the handle detaches that reader
        // instead of allowing proxy cancellation to block forever.
        drop(join);
        Vec::new()
    };
    String::from_utf8_lossy(&output).trim().to_string()
}

fn cleanup_error(message: &str, cleanup: Result<(), String>) -> String {
    match cleanup {
        Ok(()) => message.to_string(),
        Err(error) => format!("{message}; no se pudo limpiar el parcial: {error}"),
    }
}

fn finalize_proxy(temporary: &Path, output: &Path) -> Result<PathBuf, String> {
    validate_proxy_path(output)?;
    if symlink_metadata_if_present(output)?.is_some() {
        return Err("El destino del proxy aparecio durante la finalizacion".to_string());
    }
    let temporary = library_module::registered_file(temporary)?;
    // hard_link is an atomic no-overwrite publication on the same volume.
    // Unlike rename, it cannot replace an output path that appeared after the
    // preflight check.
    std::fs::hard_link(&temporary, output)
        .map_err(|error| format!("No se pudo publicar el proxy de forma atomica: {error}"))?;
    let final_path = match library_module::registered_file(output) {
        Ok(path) => path,
        Err(error) => {
            let _ = remove_proxy_file(output);
            return Err(error);
        }
    };
    if let Err(error) = remove_proxy_file(&temporary) {
        let _ = remove_proxy_file(output);
        return Err(format!("No se pudo retirar el parcial: {error}"));
    }
    Ok(final_path)
}

fn format_worker_error(message: &str, stderr: &str) -> String {
    if stderr.is_empty() {
        message.to_string()
    } else {
        format!("{message}: {stderr}")
    }
}

fn unsafe_metadata(metadata: &std::fs::Metadata) -> bool {
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

#[cfg(test)]
mod tests {
    use std::io::Read;
    use std::process::Command;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;
    use std::time::{Duration, Instant};
    use std::{fs, path::PathBuf};

    use super::{drain_stderr, validate_clip_id};

    struct BlockingReader {
        release: Arc<AtomicBool>,
    }

    impl Read for BlockingReader {
        fn read(&mut self, _buffer: &mut [u8]) -> std::io::Result<usize> {
            while !self.release.load(Ordering::Acquire) {
                std::thread::sleep(Duration::from_millis(10));
            }
            Ok(0)
        }
    }

    #[test]
    fn proxy_ids_cannot_escape_the_root() {
        assert!(validate_clip_id("../sentinel").is_err());
        assert!(validate_clip_id("clip-1_ok").is_ok());
    }

    #[test]
    fn stderr_capture_is_bounded_while_draining() {
        let input = vec![b'x'; super::STDERR_CAPTURE_LIMIT + 1024];
        assert_eq!(
            drain_stderr(input.as_slice()).len(),
            super::STDERR_CAPTURE_LIMIT
        );
    }

    #[test]
    fn hung_proxy_stderr_join_is_bounded_and_cancelable() {
        let release = Arc::new(AtomicBool::new(false));
        let (join, done) = super::spawn_stderr_reader(BlockingReader {
            release: Arc::clone(&release),
        })
        .expect("stderr reader");
        let started = Instant::now();
        let output = super::join_stderr(join, &done);
        assert!(output.is_empty());
        assert!(started.elapsed() < Duration::from_secs(2));
        release.store(true, Ordering::Release);
    }

    #[test]
    fn hung_proxy_child_termination_is_bounded() {
        let mut child = if cfg!(windows) {
            Command::new("cmd.exe")
                .args(["/C", "ping", "127.0.0.1", "-n", "60", ">", "NUL"])
                .spawn()
                .expect("hung proxy child")
        } else {
            Command::new("sleep")
                .arg("60")
                .spawn()
                .expect("hung proxy child")
        };
        let started = Instant::now();
        super::terminate_child(&mut child);
        assert!(started.elapsed() < Duration::from_secs(2));
    }

    #[test]
    fn proxy_publication_never_replaces_an_existing_sentinel() {
        let root = std::env::temp_dir().join(format!(
            "moonlit-proxy-publication-{}",
            super::validate_clip_id as usize
        ));
        let root = unique_test_directory(root);
        fs::create_dir_all(&root).expect("root");
        let root = super::library_module::prepare_registered_root(&root).expect("registered root");
        let temporary = root.join("clip.mp4.partial");
        let output = root.join("clip.mp4");
        fs::write(&temporary, b"proxy").expect("partial");
        fs::write(&output, b"sentinel").expect("sentinel");

        assert!(super::finalize_proxy(&temporary, &output).is_err());
        assert_eq!(fs::read(&output).expect("sentinel remains"), b"sentinel");
        fs::remove_dir_all(root).expect("cleanup");
    }

    fn unique_test_directory(path: PathBuf) -> PathBuf {
        let mut candidate = path;
        candidate.push(format!("{}", std::process::id()));
        candidate.push(format!("{}", Instant::now().elapsed().as_nanos()));
        candidate
    }
}
