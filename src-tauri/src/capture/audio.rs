//! Live per-track capture gain (Linux/PipeWire).
//! Mechanism: per-stream (source-output) volume via pactl. This changes ONLY
//! what GSR records — never what the user hears (device volumes untouched).
//! See docs/02_CAPTURE_ENGINE.md § volumes.

use std::collections::HashMap;
use std::time::Duration;
use tokio::process::Command;
use tokio::time::sleep;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Track {
    Game,
    Mic,
}

#[derive(Debug, serde::Deserialize)]
struct Output {
    index: u32,
    properties: HashMap<String, String>,
}

fn prop(o: &Output, key: &str) -> String {
    o.properties.get(key).cloned().unwrap_or_default().to_lowercase()
}

/// GSR recording streams, each tagged with its track.
pub async fn gsr_streams() -> Result<Vec<(u32, Track)>, String> {
    let out = Command::new("pactl")
        .args(["-f", "json", "list", "source-outputs"])
        .output()
        .await
        .map_err(|e| format!("pactl failed: {e}"))?;
    if !out.status.success() {
        return Err("pactl list failed".into());
    }
    let outputs: Vec<Output> =
        serde_json::from_slice(&out.stdout).map_err(|e| format!("pactl parse: {e}"))?;
    let gsr: Vec<&Output> = outputs
        .iter()
        .filter(|o| prop(o, "application.name").contains("gpu-screen-recorder"))
        .collect();
    if gsr.is_empty() {
        return Ok(vec![]);
    }
    let mut tagged: Vec<(u32, Track)> = Vec::new();
    let mut untagged: Vec<u32> = Vec::new();
    for o in &gsr {
        let media = prop(o, "media.name");
        let node = prop(o, "node.name");
        if media.contains("monitor") || node.contains("monitor") {
            tagged.push((o.index, Track::Game));
        } else {
            untagged.push(o.index);
        }
    }
    // Fallback: GSR spawns -a in order (game first), source-outputs follow
    // creation order, so lowest index = game.
    untagged.sort_unstable();
    for (i, idx) in untagged.into_iter().enumerate() {
        let has_game = tagged.iter().any(|(_, t)| *t == Track::Game);
        tagged.push((idx, if i == 0 && !has_game { Track::Game } else { Track::Mic }));
    }
    Ok(tagged)
}

async fn set_volume(index: u32, percent: u32) -> Result<(), String> {
    let pct = percent.clamp(0, 150);
    let status = Command::new("pactl")
        .args(["set-source-output-volume", &index.to_string(), &format!("{pct}%")])
        .status()
        .await
        .map_err(|e| format!("pactl volume failed: {e}"))?;
    if !status.success() {
        return Err(format!("pactl volume failed for stream {index}"));
    }
    Ok(())
}

async fn set_mute(index: u32, muted: bool) -> Result<(), String> {
    let status = Command::new("pactl")
        .args([
            "set-source-output-mute",
            &index.to_string(),
            if muted { "1" } else { "0" },
        ])
        .status()
        .await
        .map_err(|e| format!("pactl mute failed: {e}"))?;
    if !status.success() {
        return Err(format!("pactl mute failed for stream {index}"));
    }
    Ok(())
}

/// Wait (streams appear async after spawn) then apply gains to each track.
pub async fn apply_gains(
    game_pct: u32,
    mic_pct: u32,
    mute_game: bool,
    mute_mic: bool,
) -> Result<(), String> {
    let deadline = std::time::Instant::now() + Duration::from_secs(4);
    loop {
        let streams = gsr_streams().await?;
        if !streams.is_empty() {
            for (idx, track) in streams {
                match track {
                    Track::Game => {
                        set_volume(idx, game_pct).await?;
                        set_mute(idx, mute_game).await?;
                    }
                    Track::Mic => {
                        set_volume(idx, mic_pct).await?;
                        set_mute(idx, mute_mic).await?;
                    }
                }
            }
            return Ok(());
        }
        if std::time::Instant::now() >= deadline {
            return Err("no GSR audio streams appeared".into());
        }
        sleep(Duration::from_millis(250)).await;
    }
}
