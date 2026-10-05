use crate::model::{Diagnostic, SystemdUnit};
use std::collections::BTreeMap;

pub fn expand_specifiers(value: &str, unit_name: &str) -> String {
    let (prefix, instance) = if let Some(at_pos) = unit_name.find('@') {
        let p = &unit_name[..at_pos];
        let rest = &unit_name[at_pos + 1..];
        let inst = rest.rfind('.').map(|dot| &rest[..dot]).unwrap_or(rest);
        (p, inst)
    } else {
        let p = unit_name
            .rfind('.')
            .map(|dot| &unit_name[..dot])
            .unwrap_or(unit_name);
        (p, "")
    };

    let mut out = String::with_capacity(value.len());
    let mut chars = value.chars().peekable();

    while let Some(ch) = chars.next() {
        if ch == '%' {
            if let Some(&next) = chars.peek() {
                chars.next();
                match next {
                    '%' => out.push('%'),
                    'n' | 'N' => out.push_str(unit_name),
                    'p' => out.push_str(prefix),
                    'i' | 'I' => out.push_str(instance),
                    'u' => out.push_str("root"),
                    'h' => out.push_str("/root"),
                    other => {
                        out.push('%');
                        out.push(other);
                    }
                }
            } else {
                out.push('%');
            }
        } else {
            out.push(ch);
        }
    }

    out
}

pub fn parse_unit_content(content: &str, unit_name: &str, path: Option<&str>) -> SystemdUnit {
    let mut sections: BTreeMap<String, BTreeMap<String, Vec<String>>> = BTreeMap::new();
    let mut current_section = String::new();
    let mut diagnostics = Vec::new();

    let mut logical_lines = Vec::new();
    let mut buffer = String::new();
    let mut start_line = 1;

    for (idx, physical) in content.lines().enumerate() {
        let line_num = idx + 1;
        let trimmed = physical.trim();
        if trimmed.starts_with('#') || trimmed.starts_with(';') {
            continue;
        }

        if let Some(without_slash) = trimmed.strip_suffix('\\') {
            let without_slash = without_slash.trim_end();
            if buffer.is_empty() {
                start_line = line_num;
                buffer.push_str(without_slash);
            } else {
                buffer.push(' ');
                buffer.push_str(without_slash);
            }
        } else {
            if !buffer.is_empty() {
                buffer.push(' ');
                buffer.push_str(trimmed);
                logical_lines.push((start_line, std::mem::take(&mut buffer)));
            } else if !trimmed.is_empty() {
                logical_lines.push((line_num, trimmed.to_string()));
            }
        }
    }

    if !buffer.is_empty() {
        logical_lines.push((start_line, buffer));
    }

    for (line_num, line) in logical_lines {
        let trimmed = line.trim();
        if trimmed.starts_with('[') && trimmed.ends_with(']') {
            current_section = trimmed[1..trimmed.len() - 1].trim().to_string();
            sections.entry(current_section.clone()).or_default();
            continue;
        }

        if let Some(eq_idx) = trimmed.find('=') {
            let key = trimmed[..eq_idx].trim().to_string();
            let raw_val = trimmed[eq_idx + 1..].trim();
            let val = expand_specifiers(raw_val, unit_name);

            if current_section.is_empty() {
                diagnostics.push(Diagnostic {
                    code: "SYNTAX_KEY_BEFORE_SECTION".to_string(),
                    severity: "warning".to_string(),
                    message: format!("Key '{}' specified before any section header", key),
                    path: path.map(String::from),
                    line: Some(line_num),
                });
                continue;
            }

            let sec_map = sections.entry(current_section.clone()).or_default();
            let entries = sec_map.entry(key.clone()).or_default();

            // In systemd, an empty assignment like `ExecStart=` clears previous values
            if val.is_empty() {
                entries.clear();
            } else {
                entries.push(val);
            }
        }
    }

    // Extract dependencies & ExecStart
    let mut wants = Vec::new();
    let mut requires = Vec::new();
    let mut before = Vec::new();
    let mut after = Vec::new();
    let mut exec_start = None;

    if let Some(sec) = sections.get("Unit") {
        if let Some(w) = sec.get("Wants") {
            for line in w {
                for item in line.split_whitespace() {
                    wants.push(item.to_string());
                }
            }
        }
        if let Some(r) = sec.get("Requires") {
            for line in r {
                for item in line.split_whitespace() {
                    requires.push(item.to_string());
                }
            }
        }
        if let Some(b) = sec.get("Before") {
            for line in b {
                for item in line.split_whitespace() {
                    before.push(item.to_string());
                }
            }
        }
        if let Some(a) = sec.get("After") {
            for line in a {
                for item in line.split_whitespace() {
                    after.push(item.to_string());
                }
            }
        }
    }

    if let Some(sec) = sections.get("Service") {
        if let Some(es) = sec.get("ExecStart") {
            if let Some(last) = es.last() {
                exec_start = Some(last.clone());
            }
        }
    }

    wants.sort();
    wants.dedup();
    requires.sort();
    requires.dedup();
    before.sort();
    before.dedup();
    after.sort();
    after.dedup();

    SystemdUnit {
        name: unit_name.to_string(),
        path: path.map(String::from),
        sections,
        wants,
        requires,
        before,
        after,
        exec_start,
        drop_ins: Vec::new(),
        diagnostics,
    }
}

pub fn apply_drop_in(base: &mut SystemdUnit, drop_in_content: &str, drop_in_path: &str) {
    let drop_in_unit = parse_unit_content(drop_in_content, &base.name, Some(drop_in_path));

    base.drop_ins.push(drop_in_path.to_string());
    base.diagnostics.extend(drop_in_unit.diagnostics);

    for (sec_name, sec_keys) in drop_in_unit.sections {
        let target_sec = base.sections.entry(sec_name.clone()).or_default();
        for (key, values) in sec_keys {
            if values.is_empty() {
                target_sec.remove(&key);
            } else {
                let target_values = target_sec.entry(key).or_default();
                for val in values {
                    target_values.push(val);
                }
            }
        }
    }

    // Refresh dependencies & exec_start
    let mut wants = Vec::new();
    let mut requires = Vec::new();
    let mut before = Vec::new();
    let mut after = Vec::new();

    if let Some(sec) = base.sections.get("Unit") {
        if let Some(w) = sec.get("Wants") {
            for line in w {
                for item in line.split_whitespace() {
                    wants.push(item.to_string());
                }
            }
        }
        if let Some(r) = sec.get("Requires") {
            for line in r {
                for item in line.split_whitespace() {
                    requires.push(item.to_string());
                }
            }
        }
        if let Some(b) = sec.get("Before") {
            for line in b {
                for item in line.split_whitespace() {
                    before.push(item.to_string());
                }
            }
        }
        if let Some(a) = sec.get("After") {
            for line in a {
                for item in line.split_whitespace() {
                    after.push(item.to_string());
                }
            }
        }
    }

    if let Some(sec) = base.sections.get("Service") {
        if let Some(es) = sec.get("ExecStart") {
            base.exec_start = es.last().cloned();
        }
    }

    wants.sort();
    wants.dedup();
    requires.sort();
    requires.dedup();
    before.sort();
    before.dedup();
    after.sort();
    after.dedup();

    base.wants = wants;
    base.requires = requires;
    base.before = before;
    base.after = after;
}
