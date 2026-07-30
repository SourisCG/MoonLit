use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::Mutex;

use tauri::State;

use crate::library::{ClipMetadata, LibraryState};

pub struct MediaJobService {
    ffmpeg: Option<PathBuf>,
    proxy_root: PathBuf,
}

impl MediaJobService {
    pub fn new(resource_dir: Option<PathBuf>, proxy_root: PathBuf) -> Result<Self, String> {
        std::fs::create_dir_all(&proxy_root).map_err(|error| error.to_string())?;
        let ffmpeg = resource_dir.map(|root| root.join("runtime/obs/bin/64bit/ffmpeg.exe"));
        Ok(Self {
            ffmpeg: ffmpeg.filter(|path| path.is_file()),
            proxy_root,
        })
    }

    pub fn create_proxy(&self, clip: &ClipMetadata) -> Result<ClipMetadata, String> {
        if clip.codec != "hevc" {
            return Ok(clip.clone());
        }
        let input = Path::new(&clip.path);
        if !input.is_absolute() || !input.is_file() {
            return Err("El archivo H.265 no existe o no es una ruta absoluta".to_string());
        }
        let ffmpeg = self
            .ffmpeg
            .as_ref()
            .ok_or_else(|| "El runtime de proxy H.264 no esta instalado".to_string())?;
        let output = self.proxy_root.join(format!("{}.mp4", clip.id));
        let temporary = output.with_extension("mp4.partial");
        let status = Command::new(ffmpeg)
            .args(["-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-i"])
            .arg(input)
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
            .status()
            .map_err(|error| format!("No se pudo iniciar el worker multimedia: {error}"))?;
        if !status.success() {
            let _ = std::fs::remove_file(&temporary);
            return Err(format!("FFmpeg termino con estado {status}"));
        }
        std::fs::rename(&temporary, &output).map_err(|error| error.to_string())?;
        let mut result = clip.clone();
        result.proxy_path = Some(output.to_string_lossy().into_owned());
        result.proxy_status = "ready".to_string();
        Ok(result)
    }
}

pub struct MediaJobState(pub Mutex<MediaJobService>);

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
            library
                .0
                .lock()
                .map_err(|_| "La biblioteca esta bloqueada".to_string())?
                .set_proxy(&id, updated.proxy_path.as_deref().map(Path::new), "ready")?;
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
