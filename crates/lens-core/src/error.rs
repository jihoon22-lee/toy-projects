use std::path::PathBuf;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum LensError {
    #[error("I/O error at {path:?}: {source}")]
    Io {
        path: PathBuf,
        #[source]
        source: std::io::Error,
    },

    #[error("Resource limit exceeded: {message}")]
    LimitExceeded { message: String },

    #[error("Corrupt or invalid input format: {message}")]
    InvalidInput { message: String },

    #[error("File modified during inspection (TOCTOU violation): {path:?}")]
    InputChanged { path: PathBuf },

    #[error("JSON serialization or deserialization failed: {0}")]
    Json(#[from] serde_json::Error),

    #[error("Unsupported platform or format feature: {0}")]
    Unsupported(String),
}

pub type Result<T> = std::result::Result<T, LensError>;
