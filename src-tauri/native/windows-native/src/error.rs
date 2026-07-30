use std::fmt;

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum NativeError {
    Unsupported(&'static str),
    Windows { operation: &'static str, code: i32 },
    DriverUnavailable(&'static str),
    PermissionDenied,
    SourceNotFound(String),
    SourceEnded,
    EncoderUnavailable(String),
    InvalidConfig(&'static str),
    ChannelClosed,
    WorkerPanicked,
    Io(String),
}

impl NativeError {
    pub(crate) fn windows(operation: &'static str, error: windows::core::Error) -> Self {
        Self::Windows {
            operation,
            code: error.code().0,
        }
    }
}

impl fmt::Display for NativeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Unsupported(message) => formatter.write_str(message),
            Self::Windows { operation, code } => {
                write!(formatter, "{operation} failed with HRESULT 0x{code:08x}")
            }
            Self::DriverUnavailable(message) => formatter.write_str(message),
            Self::PermissionDenied => formatter.write_str("Windows capture permission was denied"),
            Self::SourceNotFound(source) => write!(formatter, "capture source not found: {source}"),
            Self::SourceEnded => formatter.write_str("capture source ended"),
            Self::EncoderUnavailable(message) => formatter.write_str(message),
            Self::InvalidConfig(message) => formatter.write_str(message),
            Self::ChannelClosed => formatter.write_str("native capture channel closed"),
            Self::WorkerPanicked => formatter.write_str("native capture worker panicked"),
            Self::Io(message) => formatter.write_str(message),
        }
    }
}

impl std::error::Error for NativeError {}

impl From<std::io::Error> for NativeError {
    fn from(error: std::io::Error) -> Self {
        Self::Io(error.to_string())
    }
}
