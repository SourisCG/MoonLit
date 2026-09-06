//! OS-level "open this path" (Windows stub side: `cmd /C start`).
//! Shared code calls `crate::os::open_external` and never branches on OS.

use std::path::Path;

/// Open `path` with the OS default handler.
pub fn open_external(path: &Path) -> Result<(), String> {
    std::process::Command::new("cmd")
        .args(["/C", "start", "", &path.to_string_lossy()])
        .spawn()
        .map(|_| ())
        .map_err(|e| format!("OS launcher failed: {e}"))
}
