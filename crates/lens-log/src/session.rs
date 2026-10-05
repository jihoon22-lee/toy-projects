use serde::{Deserialize, Serialize};

pub const SESSION_SCHEMA_V2: &str = "loglens.session/v2";

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SessionSource {
    pub path: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub format: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub multiline: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub max_record_bytes: Option<u64>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SessionV2 {
    pub schema: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub filter: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub level: Option<String>,
    pub source: SessionSource,
}

impl SessionV2 {
    pub fn new(path: impl Into<String>) -> Self {
        Self {
            schema: SESSION_SCHEMA_V2.to_string(),
            name: None,
            filter: None,
            level: None,
            source: SessionSource {
                path: path.into(),
                format: Some("auto".to_string()),
                multiline: Some("fold-continuations".to_string()),
                max_record_bytes: Some(65536),
            },
        }
    }
}
