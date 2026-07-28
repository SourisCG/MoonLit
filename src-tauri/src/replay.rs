//! GOP-aware buffer for encoded packets.
//!
//! The buffer owns encoded media data inside the backend. It never crosses
//! the Tauri command or event boundary.

#![allow(dead_code)]

use std::collections::VecDeque;
use std::fmt;
use std::time::Duration;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct EncodedPacket {
    pub pts_ms: u64,
    pub duration_ms: u64,
    pub is_keyframe: bool,
    pub data: Vec<u8>,
}

impl EncodedPacket {
    pub fn new(pts_ms: u64, duration_ms: u64, is_keyframe: bool, data: Vec<u8>) -> Self {
        Self {
            pts_ms,
            duration_ms,
            is_keyframe,
            data,
        }
    }

    fn end_ms(&self) -> u64 {
        self.pts_ms.saturating_add(self.duration_ms)
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ReplayClip {
    pub packets: Vec<EncodedPacket>,
    pub start_pts_ms: u64,
    pub end_pts_ms: u64,
}

impl ReplayClip {
    pub fn duration_ms(&self) -> u64 {
        self.end_pts_ms.saturating_sub(self.start_pts_ms)
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ReplayError {
    InvalidWindow,
    InvalidPacket(&'static str),
    OutOfOrderPacket,
    NoDecodableKeyframe,
}

impl fmt::Display for ReplayError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidWindow => formatter.write_str("replay window must be greater than zero"),
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
    window_ms: u64,
    packets: VecDeque<EncodedPacket>,
}

impl ReplayBuffer {
    pub fn new(window: Duration) -> Result<Self, ReplayError> {
        let window_ms =
            u64::try_from(window.as_millis()).map_err(|_| ReplayError::InvalidWindow)?;
        if window_ms == 0 {
            return Err(ReplayError::InvalidWindow);
        }

        Ok(Self {
            window_ms,
            packets: VecDeque::new(),
        })
    }

    pub fn push(&mut self, packet: EncodedPacket) -> Result<(), ReplayError> {
        if packet.duration_ms == 0 {
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
            .is_some_and(|previous| packet.pts_ms < previous.pts_ms)
        {
            return Err(ReplayError::OutOfOrderPacket);
        }

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

    pub fn snapshot(&self) -> Vec<EncodedPacket> {
        self.packets.iter().cloned().collect()
    }

    pub fn save_last(&self, duration: Duration) -> Result<ReplayClip, ReplayError> {
        let requested_ms =
            u64::try_from(duration.as_millis()).map_err(|_| ReplayError::InvalidWindow)?;
        if requested_ms == 0 {
            return Err(ReplayError::InvalidWindow);
        }

        let end_pts_ms = self
            .packets
            .back()
            .map(EncodedPacket::end_ms)
            .ok_or(ReplayError::NoDecodableKeyframe)?;
        let cutoff_ms = end_pts_ms.saturating_sub(requested_ms);
        let start_index = self
            .decodable_start_index(cutoff_ms)
            .ok_or(ReplayError::NoDecodableKeyframe)?;
        let packets: Vec<_> = self.packets.iter().skip(start_index).cloned().collect();
        let start_pts_ms = packets
            .first()
            .map(|packet| packet.pts_ms)
            .ok_or(ReplayError::NoDecodableKeyframe)?;

        Ok(ReplayClip {
            packets,
            start_pts_ms,
            end_pts_ms,
        })
    }

    fn prune(&mut self) {
        let Some(end_pts_ms) = self.packets.back().map(EncodedPacket::end_ms) else {
            return;
        };
        let cutoff_ms = end_pts_ms.saturating_sub(self.window_ms);

        let keep_index = self
            .packets
            .iter()
            .enumerate()
            .filter(|(_, packet)| packet.is_keyframe && packet.pts_ms <= cutoff_ms)
            .map(|(index, _)| index)
            .next_back()
            .or_else(|| self.packets.iter().position(|packet| packet.is_keyframe));

        let remove_count = keep_index.unwrap_or_else(|| {
            self.packets
                .iter()
                .position(|packet| packet.end_ms() > cutoff_ms)
                .unwrap_or(self.packets.len())
        });

        for _ in 0..remove_count {
            self.packets.pop_front();
        }
    }

    fn decodable_start_index(&self, cutoff_ms: u64) -> Option<usize> {
        self.packets
            .iter()
            .enumerate()
            .filter(|(_, packet)| packet.is_keyframe && packet.pts_ms <= cutoff_ms)
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
            pts_ms,
            40,
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
        assert_eq!(clip.start_pts_ms, 80);
        assert_eq!(clip.end_pts_ms, 240);
        assert_eq!(clip.packets.len(), 4);
        assert!(clip.packets[0].is_keyframe);
        assert_eq!(clip.packets[0].data, vec![0, 0, 0, 1, 0x65]);
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
        assert_eq!(packets.first().map(|packet| packet.pts_ms), Some(80));
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
    fn rejects_zero_duration_windows() {
        assert!(matches!(
            ReplayBuffer::new(Duration::ZERO),
            Err(ReplayError::InvalidWindow)
        ));
    }
}
