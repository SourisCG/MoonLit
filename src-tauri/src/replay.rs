//! GOP-aware buffer for encoded packets.
//!
//! The buffer owns encoded media data inside the backend. It never crosses
//! the Tauri command or event boundary.

#![allow(dead_code)]

use std::collections::VecDeque;
use std::fmt;
use std::sync::Arc;
use std::time::Duration;

const DEFAULT_MAX_BYTES: usize = 512 * 1024 * 1024;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct EncodedPacket {
    pub pts_100ns: u64,
    pub duration_100ns: u64,
    pub is_keyframe: bool,
    pub data: Arc<[u8]>,
}

impl EncodedPacket {
    pub fn new(pts_100ns: u64, duration_100ns: u64, is_keyframe: bool, data: Vec<u8>) -> Self {
        Self {
            pts_100ns,
            duration_100ns,
            is_keyframe,
            data: Arc::from(data),
        }
    }

    fn end_100ns(&self) -> u64 {
        self.pts_100ns.saturating_add(self.duration_100ns)
    }

    fn byte_len(&self) -> usize {
        self.data.len()
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ReplayClip {
    pub packets: Vec<EncodedPacket>,
    pub start_pts_100ns: u64,
    pub end_pts_100ns: u64,
}

impl ReplayClip {
    pub fn duration_100ns(&self) -> u64 {
        self.end_pts_100ns.saturating_sub(self.start_pts_100ns)
    }

    pub fn duration_ms(&self) -> u64 {
        self.duration_100ns() / 10_000
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ReplayError {
    InvalidWindow,
    InvalidBufferLimit,
    InvalidPacket(&'static str),
    OutOfOrderPacket,
    NoDecodableKeyframe,
}

impl fmt::Display for ReplayError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidWindow => formatter.write_str("replay window must be greater than zero"),
            Self::InvalidBufferLimit => {
                formatter.write_str("replay byte limit must be greater than zero")
            }
            Self::InvalidPacket(reason) => write!(formatter, "invalid encoded packet: {reason}"),
            Self::OutOfOrderPacket => {
                formatter.write_str("encoded packet timestamps are out of order")
            }
            Self::NoDecodableKeyframe => {
                formatter.write_str("replay buffer has no decodable keyframe")
            }
        }
    }
}

impl std::error::Error for ReplayError {}

pub struct ReplayBuffer {
    window_100ns: u64,
    max_bytes: usize,
    bytes: usize,
    packets: VecDeque<EncodedPacket>,
}

impl ReplayBuffer {
    pub fn new(window: Duration) -> Result<Self, ReplayError> {
        Self::with_max_bytes(window, DEFAULT_MAX_BYTES)
    }

    pub fn with_max_bytes(window: Duration, max_bytes: usize) -> Result<Self, ReplayError> {
        let window_100ns =
            u64::try_from(window.as_nanos() / 100).map_err(|_| ReplayError::InvalidWindow)?;
        if window_100ns == 0 {
            return Err(ReplayError::InvalidWindow);
        }
        if max_bytes == 0 {
            return Err(ReplayError::InvalidBufferLimit);
        }

        Ok(Self {
            window_100ns,
            max_bytes,
            bytes: 0,
            packets: VecDeque::new(),
        })
    }

    pub fn push(&mut self, packet: EncodedPacket) -> Result<(), ReplayError> {
        if packet.duration_100ns == 0 {
            return Err(ReplayError::InvalidPacket(
                "duration must be greater than zero",
            ));
        }
        if packet.data.is_empty() {
            return Err(ReplayError::InvalidPacket("data must not be empty"));
        }
        if self
            .packets
            .back()
            .is_some_and(|previous| packet.pts_100ns < previous.pts_100ns)
        {
            return Err(ReplayError::OutOfOrderPacket);
        }

        self.bytes = self.bytes.saturating_add(packet.byte_len());
        self.packets.push_back(packet);
        self.prune();
        Ok(())
    }

    pub fn len(&self) -> usize {
        self.packets.len()
    }

    pub fn is_empty(&self) -> bool {
        self.packets.is_empty()
    }

    pub fn bytes(&self) -> usize {
        self.bytes
    }

    pub fn snapshot(&self) -> Vec<EncodedPacket> {
        self.packets.iter().cloned().collect()
    }

    pub fn save_last(&self, duration: Duration) -> Result<ReplayClip, ReplayError> {
        let requested_100ns =
            u64::try_from(duration.as_nanos() / 100).map_err(|_| ReplayError::InvalidWindow)?;
        if requested_100ns == 0 {
            return Err(ReplayError::InvalidWindow);
        }

        let end_pts_100ns = self
            .packets
            .back()
            .map(EncodedPacket::end_100ns)
            .ok_or(ReplayError::NoDecodableKeyframe)?;
        let cutoff_100ns = end_pts_100ns.saturating_sub(requested_100ns);
        let start_index = self
            .decodable_start_index(cutoff_100ns)
            .ok_or(ReplayError::NoDecodableKeyframe)?;
        let packets: Vec<_> = self.packets.iter().skip(start_index).cloned().collect();
        let start_pts_100ns = packets
            .first()
            .map(|packet| packet.pts_100ns)
            .ok_or(ReplayError::NoDecodableKeyframe)?;

        Ok(ReplayClip {
            packets,
            start_pts_100ns,
            end_pts_100ns,
        })
    }

    fn prune(&mut self) {
        let Some(end_pts_100ns) = self.packets.back().map(EncodedPacket::end_100ns) else {
            return;
        };
        let cutoff_100ns = end_pts_100ns.saturating_sub(self.window_100ns);

        let keep_index = self
            .packets
            .iter()
            .enumerate()
            .filter(|(_, packet)| packet.is_keyframe && packet.pts_100ns <= cutoff_100ns)
            .map(|(index, _)| index)
            .next_back()
            .or_else(|| self.packets.iter().position(|packet| packet.is_keyframe));

        let remove_count = keep_index.unwrap_or_else(|| {
            self.packets
                .iter()
                .position(|packet| packet.end_100ns() > cutoff_100ns)
                .unwrap_or(self.packets.len())
        });
        self.remove_front(remove_count);

        while self.bytes > self.max_bytes {
            let next_keyframe = self
                .packets
                .iter()
                .enumerate()
                .skip(1)
                .find(|(_, packet)| packet.is_keyframe)
                .map(|(index, _)| index);
            match next_keyframe {
                Some(index) => self.remove_front(index),
                None => {
                    self.packets.clear();
                    self.bytes = 0;
                    break;
                }
            }
        }
    }

    fn remove_front(&mut self, count: usize) {
        for _ in 0..count {
            if let Some(packet) = self.packets.pop_front() {
                self.bytes = self.bytes.saturating_sub(packet.byte_len());
            }
        }
    }

    fn decodable_start_index(&self, cutoff_100ns: u64) -> Option<usize> {
        self.packets
            .iter()
            .enumerate()
            .filter(|(_, packet)| packet.is_keyframe && packet.pts_100ns <= cutoff_100ns)
            .map(|(index, _)| index)
            .next_back()
            .or_else(|| self.packets.iter().position(|packet| packet.is_keyframe))
    }
}

#[cfg(test)]
mod tests {
    use std::time::Duration;

    use super::{EncodedPacket, ReplayBuffer, ReplayError};

    fn h264_packet(pts_ms: u64, is_keyframe: bool) -> EncodedPacket {
        EncodedPacket::new(
            pts_ms * 10_000,
            40 * 10_000,
            is_keyframe,
            vec![0, 0, 0, 1, if is_keyframe { 0x65 } else { 0x41 }],
        )
    }

    #[test]
    fn keeps_the_keyframe_before_the_requested_window() {
        let mut buffer = ReplayBuffer::new(Duration::from_millis(120)).expect("buffer");
        for (pts_ms, is_keyframe) in [
            (0, true),
            (40, false),
            (80, true),
            (120, false),
            (160, false),
            (200, false),
        ] {
            buffer
                .push(h264_packet(pts_ms, is_keyframe))
                .expect("packet");
        }

        let clip = buffer
            .save_last(Duration::from_millis(120))
            .expect("decodable clip");
        assert_eq!(clip.start_pts_100ns, 80 * 10_000);
        assert_eq!(clip.end_pts_100ns, 240 * 10_000);
        assert_eq!(clip.packets.len(), 4);
        assert!(clip.packets[0].is_keyframe);
    }

    #[test]
    fn drops_packets_before_the_retained_gop() {
        let mut buffer = ReplayBuffer::new(Duration::from_millis(100)).expect("buffer");
        for (pts_ms, is_keyframe) in [
            (0, true),
            (40, false),
            (80, true),
            (120, false),
            (160, false),
            (200, true),
        ] {
            buffer
                .push(h264_packet(pts_ms, is_keyframe))
                .expect("packet");
        }

        let packets = buffer.snapshot();
        assert_eq!(
            packets.first().map(|packet| packet.pts_100ns),
            Some(80 * 10_000)
        );
        assert!(packets.first().is_some_and(|packet| packet.is_keyframe));
    }

    #[test]
    fn refuses_to_save_without_a_keyframe() {
        let mut buffer = ReplayBuffer::new(Duration::from_secs(1)).expect("buffer");
        buffer.push(h264_packet(0, false)).expect("packet");
        assert_eq!(
            buffer.save_last(Duration::from_secs(1)),
            Err(ReplayError::NoDecodableKeyframe)
        );
    }

    #[test]
    fn rejects_invalid_packets_and_timestamps() {
        let mut buffer = ReplayBuffer::new(Duration::from_secs(1)).expect("buffer");
        assert_eq!(
            buffer.push(EncodedPacket::new(0, 0, true, vec![1])),
            Err(ReplayError::InvalidPacket(
                "duration must be greater than zero"
            ))
        );
        assert_eq!(
            buffer.push(EncodedPacket::new(0, 40, true, Vec::new())),
            Err(ReplayError::InvalidPacket("data must not be empty"))
        );
        buffer.push(h264_packet(40, true)).expect("packet");
        assert_eq!(
            buffer.push(h264_packet(0, false)),
            Err(ReplayError::OutOfOrderPacket)
        );
    }

    #[test]
    fn enforces_a_byte_limit_at_a_gop_boundary() {
        let mut buffer = ReplayBuffer::with_max_bytes(Duration::from_secs(30), 10).expect("buffer");
        buffer.push(h264_packet(0, true)).expect("packet");
        buffer.push(h264_packet(40, false)).expect("packet");
        buffer.push(h264_packet(80, true)).expect("packet");
        buffer.push(h264_packet(120, false)).expect("packet");
        assert_eq!(buffer.bytes(), 10);
        assert_eq!(
            buffer.snapshot().first().map(|packet| packet.pts_100ns),
            Some(80 * 10_000)
        );
    }

    #[test]
    fn rejects_zero_duration_windows_and_limits() {
        assert!(matches!(
            ReplayBuffer::new(Duration::ZERO),
            Err(ReplayError::InvalidWindow)
        ));
        assert!(matches!(
            ReplayBuffer::with_max_bytes(Duration::from_secs(1), 0),
            Err(ReplayError::InvalidBufferLimit)
        ));
    }
}
