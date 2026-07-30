//! Small, bounded control protocol shared by MoonLit and its recorder process.
//!
//! Media frames never use this protocol. Only control messages and completed
//! clip metadata cross the process boundary.

use std::fmt;
use std::io::{self, Read, Write};

use serde::{Deserialize, Serialize};

pub const PROTOCOL_VERSION: u16 = 1;
pub const MAX_FRAME_BYTES: usize = 4 * 1024 * 1024;
pub const MAX_STRING_BYTES: usize = 16 * 1024;

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct Frame {
    pub protocol_version: u16,
    pub request_id: u64,
    pub payload: Payload,
}

impl Frame {
    pub fn request(request_id: u64, request: Request) -> Self {
        Self {
            protocol_version: PROTOCOL_VERSION,
            request_id,
            payload: Payload::Request(request),
        }
    }

    pub fn response(request_id: u64, response: Response) -> Self {
        Self {
            protocol_version: PROTOCOL_VERSION,
            request_id,
            payload: Payload::Response(response),
        }
    }
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(tag = "kind", content = "data", rename_all = "camelCase")]
pub enum Payload {
    Request(Request),
    Response(Response),
    Event(Event),
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(tag = "type", content = "data", rename_all = "camelCase")]
pub enum Request {
    Hello { parent_pid: Option<u32> },
    Probe,
    Start(StartRequest),
    SaveReplay,
    Stop,
    Ping,
    Shutdown,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct StartRequest {
    pub source_id: String,
    pub buffer_seconds: u32,
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub fps: Option<u32>,
    pub encoder: String,
    pub codec: String,
    pub format: String,
    pub output_dir: String,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(tag = "type", content = "data", rename_all = "camelCase")]
pub enum Response {
    Hello {
        sidecar_version: String,
        protocol_version: u16,
    },
    Probe(ProbeResult),
    Started {
        encoder: String,
        format: String,
    },
    ClipSaved {
        relative_path: String,
        duration_seconds: u32,
    },
    Stopped,
    Pong,
    Error(SidecarError),
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ProbeResult {
    pub available: bool,
    pub sources: Vec<SourceInfo>,
    pub encoders: Vec<EncoderInfo>,
    pub max_width: Option<u32>,
    pub max_height: Option<u32>,
    pub max_fps: Option<u32>,
    pub note: Option<String>,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SourceInfo {
    pub id: String,
    pub kind: String,
    pub label: String,
    pub is_default: bool,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct EncoderInfo {
    pub id: String,
    pub available: bool,
    pub reason: Option<String>,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SidecarError {
    pub code: String,
    pub message: String,
    pub retryable: bool,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(tag = "type", content = "data", rename_all = "camelCase")]
pub enum Event {
    Heartbeat,
    SourceEnded { source_id: String },
    Fatal(SidecarError),
}

#[derive(Debug)]
pub enum ProtocolError {
    Io(io::Error),
    InvalidLength(usize),
    InvalidJson(serde_json::Error),
    VersionMismatch(u16),
}

impl fmt::Display for ProtocolError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Io(error) => write!(formatter, "protocol I/O failed: {error}"),
            Self::InvalidLength(length) => {
                write!(
                    formatter,
                    "protocol frame length is invalid: {length} bytes"
                )
            }
            Self::InvalidJson(error) => write!(formatter, "protocol JSON is invalid: {error}"),
            Self::VersionMismatch(version) => {
                write!(formatter, "unsupported protocol version: {version}")
            }
        }
    }
}

impl std::error::Error for ProtocolError {}

impl From<io::Error> for ProtocolError {
    fn from(error: io::Error) -> Self {
        Self::Io(error)
    }
}

pub fn write_frame<W: Write>(writer: &mut W, frame: &Frame) -> Result<(), ProtocolError> {
    if frame.protocol_version != PROTOCOL_VERSION {
        return Err(ProtocolError::VersionMismatch(frame.protocol_version));
    }
    let bytes = serde_json::to_vec(frame).map_err(ProtocolError::InvalidJson)?;
    if bytes.len() > MAX_FRAME_BYTES {
        return Err(ProtocolError::InvalidLength(bytes.len()));
    }
    let length =
        u32::try_from(bytes.len()).map_err(|_| ProtocolError::InvalidLength(bytes.len()))?;
    writer.write_all(&length.to_le_bytes())?;
    writer.write_all(&bytes)?;
    writer.flush()?;
    Ok(())
}

pub fn read_frame<R: Read>(reader: &mut R) -> Result<Option<Frame>, ProtocolError> {
    let mut length_bytes = [0_u8; 4];
    let mut first_byte = [0_u8; 1];
    match reader.read(&mut first_byte) {
        Ok(0) => return Ok(None),
        Ok(1) => length_bytes[0] = first_byte[0],
        Ok(_) => unreachable!("a one-byte buffer cannot return more than one byte"),
        Err(error) => return Err(ProtocolError::Io(error)),
    }
    reader.read_exact(&mut length_bytes[1..])?;
    let length = u32::from_le_bytes(length_bytes) as usize;
    if length == 0 || length > MAX_FRAME_BYTES {
        return Err(ProtocolError::InvalidLength(length));
    }
    let mut bytes = vec![0_u8; length];
    reader.read_exact(&mut bytes)?;
    let frame = serde_json::from_slice::<Frame>(&bytes).map_err(ProtocolError::InvalidJson)?;
    if frame.protocol_version != PROTOCOL_VERSION {
        return Err(ProtocolError::VersionMismatch(frame.protocol_version));
    }
    Ok(Some(frame))
}

#[cfg(test)]
mod tests {
    use std::io::Cursor;

    use super::{read_frame, write_frame, Frame, Payload, ProbeResult, Request, Response};

    #[test]
    fn round_trips_a_probe_request() {
        let frame = Frame::request(7, Request::Probe);
        let mut bytes = Vec::new();
        write_frame(&mut bytes, &frame).expect("write frame");
        let decoded = read_frame(&mut Cursor::new(bytes))
            .expect("read frame")
            .expect("frame");
        assert_eq!(decoded, frame);
    }

    #[test]
    fn round_trips_nested_probe_data() {
        let frame = Frame::response(
            9,
            Response::Probe(ProbeResult {
                available: true,
                sources: Vec::new(),
                encoders: Vec::new(),
                max_width: Some(1920),
                max_height: Some(1080),
                max_fps: Some(60),
                note: None,
            }),
        );
        let mut bytes = Vec::new();
        write_frame(&mut bytes, &frame).expect("write frame");
        assert_eq!(
            read_frame(&mut Cursor::new(bytes))
                .expect("read frame")
                .expect("frame")
                .payload,
            Payload::Response(Response::Probe(ProbeResult {
                available: true,
                sources: Vec::new(),
                encoders: Vec::new(),
                max_width: Some(1920),
                max_height: Some(1080),
                max_fps: Some(60),
                note: None,
            }))
        );
    }

    #[test]
    fn rejects_an_oversized_frame_before_allocating_payload() {
        let length = (super::MAX_FRAME_BYTES as u32 + 1).to_le_bytes();
        let error = read_frame(&mut Cursor::new(length.to_vec())).expect_err("oversized frame");
        assert!(matches!(error, super::ProtocolError::InvalidLength(_)));
    }

    #[test]
    fn treats_clean_eof_as_no_frame() {
        assert_eq!(read_frame(&mut Cursor::new(Vec::new())).expect("eof"), None);
    }

    #[test]
    fn rejects_a_truncated_length_prefix() {
        let error = read_frame(&mut Cursor::new(vec![1, 2])).expect_err("truncated prefix");
        assert!(matches!(error, super::ProtocolError::Io(_)));
    }
}
