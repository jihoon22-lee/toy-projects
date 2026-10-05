//! Real wall-clock timestamps for forensic artifacts.
//!
//! Never fabricate timestamps: every artifact must carry the true observation
//! time or omit the field entirely.

/// Current UTC time as an ISO-8601 / RFC 3339 string, e.g. `2026-10-05T21:50:03Z`.
pub fn utc_now_iso() -> String {
    jiff::Timestamp::now().to_string()
}

/// Current local time as `YYYY-MM-DDTHH:MM:SS` (FreeDesktop `DeletionDate` format).
pub fn local_now_iso() -> String {
    jiff::Zoned::now().strftime("%Y-%m-%dT%H:%M:%S").to_string()
}

/// Seconds since the UNIX epoch.
pub fn unix_secs() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn utc_now_is_rfc3339() {
        let ts = utc_now_iso();
        assert!(ts.ends_with('Z'));
        assert!(ts.contains('T'));
        assert!(ts.starts_with("20"));
    }
}
