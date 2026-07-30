use std::env;
use std::process::Command;
use std::time::{SystemTime, UNIX_EPOCH};

#[cfg(target_os = "linux")]
use std::fs;
#[cfg(windows)]
use std::path::Path;

use serde::Serialize;

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CommandProbe {
    pub name: String,
    pub available: bool,
    pub state: String,
    pub executable: Option<String>,
    pub exit_code: Option<i32>,
    pub version: Option<String>,
    pub detail: Option<String>,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DoctorReport {
    pub generated_at: u64,
    pub architecture: String,
    pub os_name: String,
    pub os_version: Option<String>,
    pub desktop: String,
    pub session: String,
    pub gpu: Option<String>,
    pub wayland_display: bool,
    pub x11_display: bool,
    pub commands: Vec<CommandProbe>,
    pub capabilities: Vec<String>,
    pub notes: Vec<String>,
}

fn now_seconds() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}

#[cfg(any(target_os = "linux", test))]
fn first_env(keys: &[&str]) -> Option<String> {
    keys.iter()
        .find_map(|key| env::var(key).ok().filter(|value| !value.trim().is_empty()))
}

#[cfg(target_os = "linux")]
fn os_release() -> (String, Option<String>) {
    let contents = fs::read_to_string("/etc/os-release").unwrap_or_default();
    let mut name = None;
    let mut version = None;
    for line in contents.lines() {
        let Some((key, value)) = line.split_once('=') else {
            continue;
        };
        let value = value.trim_matches('"').to_string();
        match key {
            "PRETTY_NAME" => name = Some(value),
            "VERSION_ID" => version = Some(value),
            _ => {}
        }
    }
    (name.unwrap_or_else(|| env::consts::OS.to_string()), version)
}

#[cfg(not(target_os = "linux"))]
fn os_release() -> (String, Option<String>) {
    (env::consts::OS.to_string(), None)
}

fn clean_output(bytes: &[u8]) -> Option<String> {
    let output = String::from_utf8_lossy(bytes);
    let first_line = output
        .lines()
        .map(str::trim)
        .find(|line| !line.is_empty())?;
    Some(first_line.chars().take(180).collect())
}

fn probe(name: &str, args: &[&str]) -> CommandProbe {
    probe_named(name, name, args)
}

fn probe_named(label: &str, program: &str, args: &[&str]) -> CommandProbe {
    let executable = find_in_path(program);
    match Command::new(program).args(args).output() {
        Ok(output) => {
            let detail = clean_output(&output.stderr);
            let version = clean_output(&output.stdout).or_else(|| detail.clone());
            let available = output.status.success();
            CommandProbe {
                name: label.to_string(),
                available,
                state: if available { "ready" } else { "failed" }.to_string(),
                executable,
                exit_code: output.status.code(),
                version,
                detail,
            }
        }
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => CommandProbe {
            name: label.to_string(),
            available: false,
            state: "missing".to_string(),
            executable,
            exit_code: None,
            version: None,
            detail: Some("No encontrado en PATH".to_string()),
        },
        Err(error) => CommandProbe {
            name: label.to_string(),
            available: false,
            state: "failed".to_string(),
            executable,
            exit_code: None,
            version: None,
            detail: Some(error.to_string()),
        },
    }
}

fn find_in_path(program: &str) -> Option<String> {
    for directory in env::split_paths(&env::var_os("PATH")?) {
        let candidate = directory.join(program);
        if candidate.is_file() {
            return Some(candidate.to_string_lossy().into_owned());
        }

        #[cfg(windows)]
        if Path::new(program).extension().is_none() {
            for extension in [".com", ".exe", ".bat", ".cmd"] {
                let candidate = directory.join(format!("{program}{extension}"));
                if candidate.is_file() {
                    return Some(candidate.to_string_lossy().into_owned());
                }
            }
        }
    }
    None
}

#[cfg(any(target_os = "linux", test))]
fn display_flags(session: &str, has_wayland: bool, has_x11: bool) -> (bool, bool) {
    (
        session.eq_ignore_ascii_case("wayland") && has_wayland,
        session.eq_ignore_ascii_case("x11") && has_x11,
    )
}

#[cfg(target_os = "linux")]
fn detect_gpu() -> Option<String> {
    if let Ok(output) = Command::new("nvidia-smi")
        .args(["--query-gpu=name", "--format=csv,noheader"])
        .output()
    {
        if let Some(value) = clean_output(&output.stdout) {
            return Some(value);
        }
    }

    let output = Command::new("lspci").arg("-nn").output().ok()?;
    String::from_utf8_lossy(&output.stdout)
        .lines()
        .find(|line| {
            let lower = line.to_ascii_lowercase();
            (lower.contains("vga") || lower.contains("3d controller"))
                && (lower.contains("nvidia")
                    || lower.contains("amd")
                    || lower.contains("advanced micro devices")
                    || lower.contains("intel"))
        })
        .map(|line| line.trim().to_string())
}

#[cfg(target_os = "windows")]
fn detect_gpu() -> Option<String> {
    let output = Command::new("nvidia-smi")
        .args(["--query-gpu=name", "--format=csv,noheader"])
        .output()
        .ok()?;
    clean_output(&output.stdout)
}

#[cfg(not(any(target_os = "linux", target_os = "windows")))]
fn detect_gpu() -> Option<String> {
    None
}

#[tauri::command]
pub fn run_doctor() -> DoctorReport {
    let (os_name, os_version) = os_release();
    #[cfg(target_os = "linux")]
    let session = first_env(&["XDG_SESSION_TYPE"]).unwrap_or_else(|| {
        if env::var_os("WAYLAND_DISPLAY").is_some() {
            "wayland".to_string()
        } else if env::var_os("DISPLAY").is_some() {
            "x11".to_string()
        } else {
            "desconocida".to_string()
        }
    });
    #[cfg(not(target_os = "linux"))]
    let session = "windows".to_string();

    #[cfg(target_os = "linux")]
    let desktop = first_env(&["XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP"])
        .unwrap_or_else(|| "desconocido".to_string());
    #[cfg(not(target_os = "linux"))]
    let desktop = "Windows Desktop".to_string();

    #[cfg(target_os = "linux")]
    let commands = vec![
        probe("gpu-screen-recorder", &["--version"]),
        probe("ffmpeg", &["-version"]),
        probe("ffprobe", &["-version"]),
        probe("pipewire", &["--version"]),
        probe("pw-cli", &["--version"]),
        probe_named("pipewire-graph", "pw-cli", &["list-objects", "Node"]),
        probe("nvidia-smi", &["--version"]),
        probe("lspci", &["-nn"]),
    ];
    #[cfg(target_os = "windows")]
    let commands = vec![probe("nvidia-smi", &["--version"])];
    #[cfg(not(any(target_os = "linux", target_os = "windows")))]
    let commands = Vec::new();

    #[cfg(target_os = "linux")]
    let (wayland_display, x11_display) = display_flags(
        &session,
        env::var_os("WAYLAND_DISPLAY").is_some(),
        env::var_os("DISPLAY").is_some(),
    );
    #[cfg(not(target_os = "linux"))]
    let (wayland_display, x11_display) = (false, false);

    let mut capabilities = vec!["fake-backend".to_string()];
    let mut notes =
        vec!["Este diagnóstico no inicia una captura ni modifica el sistema.".to_string()];

    #[cfg(target_os = "windows")]
    {
        capabilities.push("windows".to_string());
        notes.push(
            "La captura WGC/NVENC nativa esta validada como benchmark; el runtime libobs sidecar aun requiere bridge y empaquetado."
                .to_string(),
        );
        if commands
            .iter()
            .any(|item| item.name == "nvidia-smi" && item.available)
        {
            capabilities.push("nvidia-gpu".to_string());
        } else {
            notes.push("nvidia-smi no fue encontrado en PATH.".to_string());
        }
    }

    #[cfg(target_os = "linux")]
    {
        if wayland_display {
            capabilities.push("wayland-display".to_string());
        }
        if x11_display {
            capabilities.push("x11-display".to_string());
        }
        if wayland_display {
            notes.push(
            "La captura Wayland requiere consentimiento del portal; todavía no se marca como lista."
                .to_string(),
        );
        }
        if env::var_os("DISPLAY").is_some() && session.eq_ignore_ascii_case("wayland") {
            notes.push(
                "DISPLAY pertenece a XWayland; no se cuenta como una sesión X11 real.".to_string(),
            );
        }
        if commands
            .iter()
            .any(|item| item.name == "pipewire-graph" && item.available)
        {
            capabilities.push("pipewire".to_string());
        } else {
            notes.push("PipeWire no fue encontrado en PATH.".to_string());
        }
        if commands
            .iter()
            .any(|item| item.name == "gpu-screen-recorder" && item.available)
        {
            capabilities.push("gpu-screen-recorder".to_string());
        } else {
            notes.push("gpu-screen-recorder todavía no está instalado.".to_string());
        }
        if commands
            .iter()
            .any(|item| item.name == "ffprobe" && item.available)
        {
            capabilities.push("ffprobe".to_string());
        }
    }

    DoctorReport {
        generated_at: now_seconds(),
        architecture: env::consts::ARCH.to_string(),
        os_name,
        os_version,
        desktop,
        session,
        gpu: detect_gpu(),
        wayland_display,
        x11_display,
        commands,
        capabilities,
        notes,
    }
}

#[cfg(test)]
mod tests {
    use super::{clean_output, display_flags, first_env, probe_named};

    #[test]
    fn output_is_trimmed_to_a_single_line() {
        assert_eq!(
            clean_output(b"  first line  \nsecond line"),
            Some("first line".to_string())
        );
    }

    #[test]
    fn first_env_skips_empty_values() {
        std::env::set_var("SOURISTV_TEST_EMPTY", "");
        std::env::set_var("SOURISTV_TEST_VALUE", "wayland");
        assert_eq!(
            first_env(&["SOURISTV_TEST_EMPTY", "SOURISTV_TEST_VALUE"]),
            Some("wayland".to_string())
        );
        std::env::remove_var("SOURISTV_TEST_EMPTY");
        std::env::remove_var("SOURISTV_TEST_VALUE");
    }

    #[test]
    fn xwayland_display_is_not_reported_as_an_x11_session() {
        assert_eq!(display_flags("wayland", true, true), (true, false));
        assert_eq!(display_flags("x11", false, true), (false, true));
    }

    #[test]
    fn failed_command_is_not_reported_as_available() {
        let probe = probe_named("rustc-invalid-test", "rustc", &["--moonlit-invalid-option"]);
        assert!(!probe.available);
        assert_eq!(probe.state, "failed");
    }
}
