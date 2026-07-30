use std::time::{Duration, Instant};

fn main() {
    let source = moonlit_windows_native::list_sources()
        .expect("list native sources")
        .into_iter()
        .find(|source| source.is_default)
        .expect("default monitor source");
    let (handle, packets) =
        moonlit_windows_native::start_capture(moonlit_windows_native::NativeConfig {
            source_id: source.id,
            width: source.width,
            height: source.height,
            fps: 30,
        })
        .expect("start native capture");
    let deadline = Instant::now() + Duration::from_secs(5);
    let mut packet_count = 0;
    let mut keyframe_count = 0;
    let mut byte_count = 0;
    while Instant::now() < deadline {
        match packets.recv_timeout(Duration::from_millis(250)) {
            Ok(Ok(packet)) => {
                packet_count += 1;
                keyframe_count += u32::from(packet.is_keyframe);
                byte_count += packet.data.len();
            }
            Ok(Err(error)) => panic!("native capture packet error: {error}"),
            Err(std::sync::mpsc::RecvTimeoutError::Timeout) => {}
            Err(std::sync::mpsc::RecvTimeoutError::Disconnected) => break,
        }
    }
    drop(handle);
    println!("packets={packet_count} keyframes={keyframe_count} bytes={byte_count}");
    assert!(packet_count > 0, "capture produced no encoded packets");
    assert!(keyframe_count > 0, "capture produced no keyframe");
    assert!(byte_count > 0, "capture produced no bitstream bytes");
}
