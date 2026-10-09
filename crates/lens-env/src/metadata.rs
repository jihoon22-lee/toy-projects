use crate::model::PyPackage;

pub fn normalize_package_name(name: &str) -> String {
    let mut normalized = String::with_capacity(name.len());
    let mut last_was_sep = false;

    for ch in name.chars() {
        if ch == '-' || ch == '_' || ch == '.' {
            if !last_was_sep {
                normalized.push('-');
                last_was_sep = true;
            }
        } else {
            normalized.push(ch.to_ascii_lowercase());
            last_was_sep = false;
        }
    }

    normalized
}

pub fn parse_metadata(content: &str, dist_info_dir: Option<&str>) -> Option<PyPackage> {
    let mut name = String::new();
    let mut version = String::new();
    let mut summary = None;
    let mut requires_dist = Vec::new();

    for line in content.lines() {
        if line.is_empty() {
            // End of headers in RFC 822
            break;
        }

        if let Some(rest) = line.strip_prefix("Name:") {
            name = rest.trim().to_string();
        } else if let Some(rest) = line.strip_prefix("Version:") {
            version = rest.trim().to_string();
        } else if let Some(rest) = line.strip_prefix("Summary:") {
            summary = Some(rest.trim().to_string());
        } else if let Some(rest) = line.strip_prefix("Requires-Dist:") {
            let req = rest.trim().to_string();
            if !req.is_empty() {
                requires_dist.push(req);
            }
        }
    }

    if name.is_empty() || version.is_empty() {
        return None;
    }

    Some(PyPackage {
        name,
        version,
        summary,
        requires_dist,
        dist_info: dist_info_dir.map(String::from),
        top_level_modules: Vec::new(),
    })
}
