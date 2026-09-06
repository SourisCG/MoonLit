//! Confirmation cue: synthesized two-tone ding (no binary assets, no licensing issues).

/// 880 Hz -> 1320 Hz, 0.22 s, exponential decay. 44.1 kHz mono f32.
pub fn ding_samples() -> Vec<f32> {
    const RATE: f32 = 44100.0;
    const LEN: usize = (44100.0 * 0.22) as usize;
    (0..LEN)
        .map(|i| {
            let t = i as f32 / RATE;
            let freq = if t < 0.11 { 880.0 } else { 1320.0 };
            let env = (-4.0 * t / 0.22).exp();
            (2.0 * std::f32::consts::PI * freq * t).sin() * env * 0.5
        })
        .collect()
}

pub fn play_ding() {
    let samples = ding_samples();
    // Plain thread: callable from any context (commands, hotkey handler).
    std::thread::spawn(move || {
        let Ok(stream) = rodio::OutputStreamBuilder::open_default_stream() else {
            return;
        };
        let sink = rodio::Sink::connect_new(stream.mixer());
        sink.append(rodio::buffer::SamplesBuffer::new(1, 44100, samples));
        sink.sleep_until_end();
    });
}
