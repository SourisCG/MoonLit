//! Windows capabilities stub. No file capabilities exist on Windows;
//! KMS-style permission prompts do not apply.

use std::path::Path;

/// Always false on Windows (nothing to check).
pub fn caps_ok(_path: &Path) -> bool {
    false
}

/// Always an error on Windows (nothing to fix).
pub async fn fix_caps(_path: &Path) -> Result<(), String> {
    Err("no capability fix needed on Windows".into())
}
