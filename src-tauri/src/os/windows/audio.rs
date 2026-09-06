//! Windows audio gain stub. Real per-session WASAPI gain lands on the
//! Windows test trip (we own the cpal path there, so no OS mixer hacks).

/// Mirrors os/linux/audio::apply_gains signature. Always errors until implemented.
pub async fn apply_gains(
    _known_args: &[String],
    _game_pct: u32,
    _mic_pct: u32,
    _mute_game: bool,
    _mute_mic: bool,
) -> Result<usize, String> {
    Err("per-stream gain lands on the Windows trip".into())
}

/// Mirrors os/linux/audio::linked_count signature. Always zero until implemented.
pub async fn linked_count(_known_args: &[String]) -> usize {
    0
}
