//! OS-level "open this path" (file manager select / default app).
//! Replaces the `#[cfg]` pair that used to live in commands.rs — shared code
//! calls `crate::os::open_external` and never branches on OS.

use std::path::Path;

/// Open `path` with the OS default handler (selects nothing, just opens).
/// Used as the last-resort fallback when the opener plugin cannot reveal.
pub fn open_external(path: &Path) -> Result<(), String> {
    std::process::Command::new("xdg-open")
        .arg(path)
        .spawn()
        .map(|_| ())
        .map_err(|e| format!("OS launcher failed: {e}"))
}
