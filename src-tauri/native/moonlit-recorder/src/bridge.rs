#![allow(unsafe_code)]

use std::ffi::{CStr, CString, NulError};
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

pub struct BridgeEngine {
    runtime_root: PathBuf,
    _library: Library,
    last_error: LastError,
    probe: Probe,
    start: Start,
    save: Save,
    stop: Stop,
    shutdown: Shutdown,
}

impl BridgeEngine {
    pub fn new(runtime_root: &Path) -> Result<Self, String> {
        let bridge_path = runtime_root.join("bin/64bit/moonlit-obs-bridge.dll");
        if !bridge_path.is_file() {
            return Err(format!("falta el bridge: {}", bridge_path.display()));
        }
        let library = unsafe { Library::new(&bridge_path) }.map_err(|error| error.to_string())?;
        let initialize = load::<Initialize>(&library, b"moonlit_obs_bridge_initialize\0")?;
        let last_error = load::<LastError>(&library, b"moonlit_obs_bridge_last_error\0")?;
        let probe = load::<Probe>(&library, b"moonlit_obs_bridge_probe_json\0")?;
        let start = load::<Start>(&library, b"moonlit_obs_bridge_start_json\0")?;
        let save = load::<Save>(&library, b"moonlit_obs_bridge_save_json\0")?;
        let stop = load::<Stop>(&library, b"moonlit_obs_bridge_stop_json\0")?;
        let shutdown = load::<Shutdown>(&library, b"moonlit_obs_bridge_shutdown\0")?;
        let root =
            CString::new(runtime_root.to_string_lossy().as_bytes()).map_err(cstring_error)?;
        let result = unsafe { initialize(root.as_ptr()) };
        if result != 0 {
            return Err(last_error_message(last_error));
        }
        Ok(Self {
            runtime_root: runtime_root.to_path_buf(),
            _library: library,
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
    if length < 0 || length as usize >= buffer.len() {
        return Err(SidecarError {
            code: "backendUnavailable".to_string(),
            message: last_error_message(last_error),
            retryable: true,
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
    unsafe { CStr::from_ptr(pointer) }
        .to_string_lossy()
        .into_owned()
}

fn cstring_error(error: NulError) -> String {
    format!("la ruta del runtime contiene NUL: {error}")
}
