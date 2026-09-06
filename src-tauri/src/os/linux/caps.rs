//! KMS capability check + one-click fix (Linux only).
//! The bundled GSR needs cap_sys_admin for direct KMS capture without portals.

/// Does the bundled GSR binary carry cap_sys_admin (KMS without portal)?
pub fn caps_ok(path: &std::path::Path) -> bool {
    let Ok(out) = std::process::Command::new("getcap").arg(path).output() else {
        return false;
    };
    String::from_utf8_lossy(&out.stdout).contains("cap_sys_admin")
}

/// One-click fix: pkexec setcap on OUR bundled binary (polkit dialog, once).
pub async fn fix_caps(path: &std::path::Path) -> Result<(), String> {
    let status = tokio::process::Command::new("pkexec")
        .args(["setcap", "cap_sys_admin+ep", &path.to_string_lossy()])
        .status()
        .await
        .map_err(|e| format!("pkexec failed: {e}"))?;
    if !status.success() {
        return Err("setcap rejected or failed".into());
    }
    if !caps_ok(path) {
        return Err("capability still missing after setcap".into());
    }
    Ok(())
}
