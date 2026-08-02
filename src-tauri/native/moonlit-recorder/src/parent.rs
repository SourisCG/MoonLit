#![allow(unsafe_code)]

//! Parent-process supervision for the recorder.
//!
//! The host passes its PID out of band on the command line and repeats it in
//! the Hello message. On Windows, waiting on the opened process handle is
//! stronger than polling a PID: PID reuse cannot make a dead supervisor look
//! alive. The recorder remains a child-owned process and exits when that
//! handle is signalled.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread::{self, JoinHandle};
use std::time::Duration;

pub const POLL_INTERVAL: Duration = Duration::from_millis(100);

pub struct ParentDeathMonitor {
    dead: Arc<AtomicBool>,
    stop: Arc<AtomicBool>,
    join: Option<JoinHandle<()>>,
}

impl ParentDeathMonitor {
    pub fn new(parent_pid: u32) -> Result<Self, String> {
        if parent_pid == 0 {
            return Err("el parent PID debe ser mayor que cero".to_string());
        }

        let dead = Arc::new(AtomicBool::new(false));
        let stop = Arc::new(AtomicBool::new(false));
        let dead_signal = Arc::clone(&dead);
        let stop_signal = Arc::clone(&stop);

        #[cfg(windows)]
        let parent_handle = open_parent(parent_pid)?;

        let join = thread::Builder::new()
            .name("moonlit-recorder-parent-watch".to_string())
            .spawn(move || {
                #[cfg(windows)]
                watch_windows(parent_handle, dead_signal, stop_signal);

                #[cfg(not(windows))]
                watch_portable(parent_pid, dead_signal, stop_signal);
            })
            .map_err(|error| error.to_string())?;

        Ok(Self {
            dead,
            stop,
            join: Some(join),
        })
    }

    pub fn is_dead(&self) -> bool {
        self.dead.load(Ordering::Acquire)
    }
}

impl Drop for ParentDeathMonitor {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Release);
        if let Some(join) = self.join.take() {
            let _ = join.join();
        }
    }
}

#[cfg(windows)]
fn open_parent(parent_pid: u32) -> Result<usize, String> {
    const SYNCHRONIZE: u32 = 0x0010_0000;
    let handle = unsafe { OpenProcess(SYNCHRONIZE, 0, parent_pid) };
    if handle.is_null() {
        return Err(format!(
            "no se pudo abrir el parent PID {parent_pid}: Win32 {}",
            unsafe { GetLastError() }
        ));
    }
    Ok(handle as usize)
}

#[cfg(windows)]
fn watch_windows(
    parent_handle: usize,
    dead: Arc<AtomicBool>,
    stop: Arc<AtomicBool>,
) {
    const WAIT_OBJECT_0: u32 = 0;
    const WAIT_FAILED: u32 = 0xffff_ffff;
    const WAIT_TIMEOUT: u32 = 258;
    loop {
        if stop.load(Ordering::Acquire) {
            break;
        }
        let result = unsafe { WaitForSingleObject(parent_handle as *mut core::ffi::c_void, 100) };
        match result {
            WAIT_OBJECT_0 | WAIT_FAILED => {
                dead.store(true, Ordering::Release);
                break;
            }
            WAIT_TIMEOUT => {}
            _ => {
                dead.store(true, Ordering::Release);
                break;
            }
        }
    }
    unsafe {
        let _ = CloseHandle(parent_handle as *mut core::ffi::c_void);
    }
}

#[cfg(not(windows))]
fn watch_portable(
    parent_pid: u32,
    dead: Arc<AtomicBool>,
    stop: Arc<AtomicBool>,
) {
    while !stop.load(Ordering::Acquire) {
        if !portable_parent_exists(parent_pid) {
            dead.store(true, Ordering::Release);
            break;
        }
        thread::sleep(POLL_INTERVAL);
    }
}

#[cfg(target_os = "linux")]
fn portable_parent_exists(parent_pid: u32) -> bool {
    std::path::Path::new("/proc")
        .join(parent_pid.to_string())
        .is_dir()
}

#[cfg(all(not(windows), not(target_os = "linux")))]
fn portable_parent_exists(_parent_pid: u32) -> bool {
    // The production target is Windows, where the handle wait above is used.
    // Keep non-Linux builds conservative without introducing a libc contract.
    true
}

#[cfg(windows)]
#[allow(non_snake_case)]
extern "system" {
    fn OpenProcess(
        desired_access: u32,
        inherit_handle: i32,
        process_id: u32,
    ) -> *mut core::ffi::c_void;
    fn WaitForSingleObject(handle: *mut core::ffi::c_void, milliseconds: u32) -> u32;
    fn CloseHandle(handle: *mut core::ffi::c_void) -> i32;
    fn GetLastError() -> u32;
}

#[cfg(test)]
mod tests {
    use std::process::Command;
    use std::thread;
    use std::time::{Duration, Instant};

    use super::ParentDeathMonitor;

    #[test]
    fn rejects_a_zero_parent_pid() {
        assert!(ParentDeathMonitor::new(0).is_err());
    }

    #[test]
    fn current_process_is_a_live_parent_handle() {
        let monitor = ParentDeathMonitor::new(std::process::id()).expect("current process handle");
        assert!(!monitor.is_dead());
    }

    #[test]
    fn monitor_observes_a_parent_process_exit() {
        let mut child = Command::new(std::env::current_exe().expect("test executable"))
            .arg("--help")
            .spawn()
            .expect("helper process");
        let monitor = ParentDeathMonitor::new(child.id()).expect("child process handle");
        child.wait().expect("helper exit");
        let deadline = Instant::now() + Duration::from_secs(2);
        while !monitor.is_dead() && Instant::now() < deadline {
            thread::sleep(Duration::from_millis(10));
        }
        assert!(monitor.is_dead());
    }
}
