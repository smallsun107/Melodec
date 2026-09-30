use std::fmt;

/// Errors returned by the decoder.
#[derive(Debug)]
pub enum Error {
    /// Underlying I/O failure.
    Io(std::io::Error),
    /// The file does not start with the `CTENFDAM` magic.
    BadMagic,
    /// The key section could not be decrypted or lacks the expected prefix.
    BadKey,
    /// The comment section could not be decrypted or lacks the expected marker.
    BadComment,
    /// The metadata could not be decrypted or lacks the `music:` prefix.
    BadMeta,
    /// A container section declared a length beyond the supported maximum.
    SectionTooLarge,
    /// The two cover-size fields disagree in a way that would misalign the audio.
    BadCover,
    /// The comment payload was not valid base64.
    Base64(base64::DecodeError),
    /// The metadata payload was not valid JSON.
    Json(serde_json::Error),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Io(e) => write!(f, "io error: {e}"),
            Error::BadMagic => f.write_str("not an NCM file: bad CTENFDAM magic"),
            Error::BadKey => f.write_str("invalid ncm key section"),
            Error::BadComment => f.write_str("invalid ncm comment section"),
            Error::BadMeta => f.write_str("invalid ncm metadata section"),
            Error::SectionTooLarge => f.write_str("ncm section length exceeds limit"),
            Error::BadCover => f.write_str("inconsistent ncm cover size"),
            Error::Base64(e) => write!(f, "base64 error: {e}"),
            Error::Json(e) => write!(f, "json error: {e}"),
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Error::Io(e) => Some(e),
            Error::Base64(e) => Some(e),
            Error::Json(e) => Some(e),
            _ => None,
        }
    }
}

impl From<std::io::Error> for Error {
    fn from(e: std::io::Error) -> Self {
        Error::Io(e)
    }
}

impl From<base64::DecodeError> for Error {
    fn from(e: base64::DecodeError) -> Self {
        Error::Base64(e)
    }
}

impl From<serde_json::Error> for Error {
    fn from(e: serde_json::Error) -> Self {
        Error::Json(e)
    }
}

/// Convenience alias.
pub type Result<T> = std::result::Result<T, Error>;
