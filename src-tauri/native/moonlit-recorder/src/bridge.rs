#![allow(unsafe_code)]

use std::ffi::{CString, NulError};
use std::os::raw::{c_char, c_int};
use std::path::{Path, PathBuf};

use libloading::{Library, Symbol};
use moonlit_libobs_protocol::{ProbeResult, Response, SidecarError, StartRequest};

type Initialize = unsafe extern "C" fn(*const c_char) -> c_int;
type LastError = unsafe extern "C" fn() -> *const c_char;
type Probe = unsafe extern "C" fn(*mut c_char, usize) -> c_int;
type Start = unsafe extern "C" fn(*const c_char, *mut c_char, usize) -> c_int;
type Save = unsafe extern "C" fn(*mut c_char, usize) -> c_int;
type Stop = unsafe extern "C" fn() -> c_int;
type Shutdown = unsafe extern "C" fn();

fn validated_bridge_path(runtime_root: &Path) -> Result<PathBuf, String> {
    if !runtime_root.is_absolute() || !runtime_root.is_dir() {
        return Err(format!(
            "el runtime root debe ser un directorio absoluto: {}",
            runtime_root.display()
        ));
    }
    let root = runtime_root
        .canonicalize()
        .map_err(|error| format!("no se pudo resolver el runtime root: {error}"))?;
    let bin = root.join("bin/64bit");
    let requested = bin.join("moonlit-obs-bridge.dll");
    if !requested.is_file() {
        return Err(format!("falta el bridge: {}", requested.display()));
    }
    let bridge = requested
        .canonicalize()
        .map_err(|error| format!("no se pudo resolver el bridge: {error}"))?;
    if bridge.file_name().and_then(|name| name.to_str()) != Some("moonlit-obs-bridge.dll")
        || bridge.parent() != Some(bin.as_path())
    {
        return Err("el bridge debe estar dentro de runtime/bin/64bit".to_string());
    }
    Ok(bridge)
}

#[cfg(windows)]
struct DllSearchGuard {
    cookie: *mut core::ffi::c_void,
}

#[cfg(windows)]
impl DllSearchGuard {
    fn new(directory: &Path) -> Result<Self, String> {
        let directory = directory
            .canonicalize()
            .map_err(|error| format!("no se pudo resolver el directorio DLL: {error}"))?;
        let wide = wide_path(&directory)?;
        const LOAD_LIBRARY_SEARCH_SYSTEM32: u32 = 0x0000_0800;
        const LOAD_LIBRARY_SEARCH_USER_DIRS: u32 = 0x0000_0400;
        let default_dirs = LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS;
        if unsafe { SetDefaultDllDirectories(default_dirs) } == 0 {
            return Err(format!(
                "SetDefaultDllDirectories fallo: Win32 {}",
                unsafe { GetLastError() }
            ));
        }
        let cookie = unsafe { AddDllDirectory(wide.as_ptr()) };
        if cookie.is_null() {
            return Err(format!("AddDllDirectory fallo: Win32 {}", unsafe {
                GetLastError()
            }));
        }
        Ok(Self { cookie })
    }
}

#[cfg(windows)]
impl Drop for DllSearchGuard {
    fn drop(&mut self) {
        unsafe {
            let _ = RemoveDllDirectory(self.cookie);
        }
    }
}

#[cfg(not(windows))]
struct DllSearchGuard;

#[cfg(not(windows))]
impl DllSearchGuard {
    fn new(_directory: &Path) -> Result<Self, String> {
        Ok(Self)
    }
}

fn load_library(path: &Path) -> Result<Library, String> {
    #[cfg(windows)]
    {
        use libloading::os::windows::Library as WindowsLibrary;
        const LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR: u32 = 0x0000_0100;
        const LOAD_LIBRARY_SEARCH_SYSTEM32: u32 = 0x0000_0800;
        const LOAD_LIBRARY_SEARCH_USER_DIRS: u32 = 0x0000_0400;
        let flags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
            | LOAD_LIBRARY_SEARCH_SYSTEM32
            | LOAD_LIBRARY_SEARCH_USER_DIRS;
        let library = unsafe { WindowsLibrary::load_with_flags(path, flags) }
            .map_err(|error| format!("no se pudo cargar el bridge con busqueda segura: {error}"))?;
        Ok(library.into())
    }

    #[cfg(not(windows))]
    {
        unsafe { Library::new(path) }.map_err(|error| error.to_string())
    }
}

#[cfg(windows)]
fn wide_path(path: &Path) -> Result<Vec<u16>, String> {
    use std::os::windows::ffi::OsStrExt;
    if path.as_os_str().to_string_lossy().contains('\0') {
        return Err("la ruta DLL contiene NUL".to_string());
    }
    Ok(path
        .as_os_str()
        .encode_wide()
        .chain(std::iter::once(0))
        .collect())
}

pub struct BridgeEngine {
    runtime_root: PathBuf,
    _library: Library,
    _dll_search: DllSearchGuard,
    last_error: LastError,
    probe: Probe,
    start: Start,
    save: Save,
    stop: Stop,
    shutdown: Shutdown,
}

impl BridgeEngine {
    pub fn new(runtime_root: &Path) -> Result<Self, String> {
        let canonical_runtime_root = runtime_root
            .canonicalize()
            .map_err(|error| format!("no se pudo resolver el runtime root: {error}"))?;
        let bridge_path = validated_bridge_path(&canonical_runtime_root)?;
        let dll_search = DllSearchGuard::new(
            bridge_path
                .parent()
                .ok_or_else(|| "el bridge no tiene directorio padre".to_string())?,
        )?;
        let library = load_library(&bridge_path)?;
        let initialize = load::<Initialize>(&library, b"moonlit_obs_bridge_initialize\0")?;
        let last_error = load::<LastError>(&library, b"moonlit_obs_bridge_last_error\0")?;
        let probe = load::<Probe>(&library, b"moonlit_obs_bridge_probe_json\0")?;
        let start = load::<Start>(&library, b"moonlit_obs_bridge_start_json\0")?;
        let save = load::<Save>(&library, b"moonlit_obs_bridge_save_json\0")?;
        let stop = load::<Stop>(&library, b"moonlit_obs_bridge_stop_json\0")?;
        let shutdown = load::<Shutdown>(&library, b"moonlit_obs_bridge_shutdown\0")?;
        let root = CString::new(canonical_runtime_root.to_string_lossy().as_bytes())
            .map_err(cstring_error)?;
        let result = unsafe { initialize(root.as_ptr()) };
        if result != 0 {
            return Err(last_error_message(last_error));
        }
        Ok(Self {
            runtime_root: canonical_runtime_root,
            _library: library,
            _dll_search: dll_search,
            last_error,
            probe,
            start,
            save,
            stop,
            shutdown,
        })
    }

    pub fn runtime_root(&self) -> &Path {
        &self.runtime_root
    }

    pub fn probe(&self) -> Result<ProbeResult, SidecarError> {
        let bytes = self.call_buffer(self.probe)?;
        serde_json::from_slice(&bytes).map_err(|error| SidecarError {
            code: "invalidResponse".to_string(),
            message: error.to_string(),
            retryable: true,
        })
    }

    pub fn start(&self, request: &StartRequest) -> Result<Response, SidecarError> {
        let request = serde_json::to_vec(request).map_err(|error| SidecarError {
            code: "invalidRequest".to_string(),
            message: error.to_string(),
            retryable: false,
        })?;
        let request = CString::new(request).map_err(|error| SidecarError {
            code: "invalidRequest".to_string(),
            message: error.to_string(),
            retryable: false,
        })?;
        let bytes = self.call_buffer_with_request(self.start, request.as_ptr())?;
        serde_json::from_slice(&bytes).map_err(|error| SidecarError {
            code: "invalidResponse".to_string(),
            message: error.to_string(),
            retryable: true,
        })
    }

    pub fn save(&self) -> Result<Response, SidecarError> {
        let bytes = self.call_buffer(self.save)?;
        serde_json::from_slice(&bytes).map_err(|error| SidecarError {
            code: "invalidResponse".to_string(),
            message: error.to_string(),
            retryable: true,
        })
    }

    pub fn stop(&self) -> Result<(), SidecarError> {
        let result = unsafe { (self.stop)() };
        if result == 0 {
            Ok(())
        } else {
            Err(SidecarError {
                code: "backendExited".to_string(),
                message: last_error_message(self.last_error),
                retryable: true,
            })
        }
    }

    fn call_buffer(&self, function: Probe) -> Result<Vec<u8>, SidecarError> {
        let mut buffer = vec![0_u8; 256 * 1024];
        let length = unsafe { function(buffer.as_mut_ptr().cast(), buffer.len()) };
        finish_buffer(length, buffer, self.last_error)
    }

    fn call_buffer_with_request(
        &self,
        function: Start,
        request: *const c_char,
    ) -> Result<Vec<u8>, SidecarError> {
        let mut buffer = vec![0_u8; 256 * 1024];
        let length = unsafe { function(request, buffer.as_mut_ptr().cast(), buffer.len()) };
        finish_buffer(length, buffer, self.last_error)
    }
}

impl Drop for BridgeEngine {
    fn drop(&mut self) {
        unsafe { (self.shutdown)() };
    }
}

fn load<T: Copy>(library: &Library, name: &[u8]) -> Result<T, String> {
    let symbol: Symbol<'_, T> = unsafe { library.get(name) }.map_err(|error| error.to_string())?;
    Ok(*symbol)
}

fn finish_buffer(
    length: c_int,
    mut buffer: Vec<u8>,
    last_error: LastError,
) -> Result<Vec<u8>, SidecarError> {
    if length < 0 {
        return Err(SidecarError {
            code: "backendUnavailable".to_string(),
            message: last_error_message(last_error),
            retryable: true,
        });
    }
    if length as usize >= buffer.len() {
        return Err(SidecarError {
            code: "invalidResponse".to_string(),
            message: "el bridge devolvio una respuesta fuera del buffer".to_string(),
            retryable: false,
        });
    }
    buffer.truncate(length as usize);
    Ok(buffer)
}

fn last_error_message(last_error: LastError) -> String {
    let pointer = unsafe { last_error() };
    if pointer.is_null() {
        return "El bridge libobs no dio detalles".to_string();
    }
    const MAX_ERROR_BYTES: usize = 16 * 1024;
    let bytes = unsafe { std::slice::from_raw_parts(pointer.cast::<u8>(), MAX_ERROR_BYTES) };
    let length = bytes
        .iter()
        .position(|byte| *byte == 0)
        .unwrap_or(MAX_ERROR_BYTES);
    String::from_utf8_lossy(&bytes[..length]).into_owned()
}

fn cstring_error(error: NulError) -> String {
    format!("la ruta del runtime contiene NUL: {error}")
}

#[cfg(windows)]
#[allow(non_snake_case)]
extern "system" {
    fn SetDefaultDllDirectories(flags: u32) -> i32;
    fn AddDllDirectory(new_directory: *const u16) -> *mut core::ffi::c_void;
    fn RemoveDllDirectory(cookie: *mut core::ffi::c_void) -> i32;
    fn GetLastError() -> u32;
}

#[cfg(test)]
mod tests {
    use std::fs;
    use std::os::raw::c_char;

    use super::{finish_buffer, validated_bridge_path, BridgeEngine};

    #[test]
    fn loader_rejects_a_missing_bridge_before_loading_anything() {
        let root =
            std::env::temp_dir().join(format!("moonlit-recorder-loader-{}", std::process::id()));
        fs::create_dir_all(&root).expect("runtime root");
        let error = validated_bridge_path(&root).expect_err("missing bridge");
        assert!(error.contains("falta el bridge"));
        fs::remove_dir_all(root).expect("cleanup");
    }

    #[test]
    fn loader_requires_an_absolute_runtime_root() {
        let error = validated_bridge_path(std::path::Path::new("runtime/obs"))
            .expect_err("relative runtime");
        assert!(error.contains("absoluto"));
    }

    #[test]
    fn loader_rejects_a_present_but_invalid_bridge() {
        let root = std::env::temp_dir().join(format!(
            "moonlit-recorder-invalid-loader-{}",
            std::process::id()
        ));
        let bin = root.join("bin/64bit");
        fs::create_dir_all(&bin).expect("runtime bin");
        fs::write(bin.join("moonlit-obs-bridge.dll"), b"not a DLL").expect("invalid bridge");
        assert!(BridgeEngine::new(&root).is_err());
        fs::remove_dir_all(root).expect("cleanup");
    }

    #[test]
    fn loader_rejects_an_oversized_bridge_response_buffer() {
        let error = finish_buffer(256 * 1024, vec![0_u8; 256 * 1024], test_last_error)
            .expect_err("oversized response");
        assert_eq!(error.code, "invalidResponse");
    }

    unsafe extern "C" fn test_last_error() -> *const c_char {
        c"test error".as_ptr()
    }
}
