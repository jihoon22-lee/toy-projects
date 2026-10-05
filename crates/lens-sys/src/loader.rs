//! Unit loading with `<unit>.d/*.conf` drop-in scanning.
//!
//! Mirrors systemd's on-disk lookup for the common local case: a unit file
//! `foo.service` is merged with every `foo.service.d/*.conf` sibling in
//! lexicographic filename order (later files override earlier ones).

use crate::model::{Diagnostic, SystemdUnit};
use crate::parser::{apply_drop_in, parse_unit_content};
use lens_core::{LensError, Result};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

const UNIT_SUFFIXES: [&str; 10] = [
    ".service",
    ".socket",
    ".target",
    ".timer",
    ".mount",
    ".automount",
    ".swap",
    ".path",
    ".slice",
    ".scope",
];

/// Load one unit file, then merge every `*.conf` in its `<name>.d/` directory.
pub fn load_unit(path: &Path) -> Result<SystemdUnit> {
    let name = path
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("unit")
        .to_string();
    let content = std::fs::read_to_string(path).map_err(|e| LensError::Io {
        path: path.to_path_buf(),
        source: e,
    })?;
    let mut unit = parse_unit_content(&content, &name, Some(&path.to_string_lossy()));

    // systemd precedence: for an instance `foo@bar.service`, template-level
    // `foo@.service.d/*.conf` merges first, then `foo@bar.service.d/*.conf`.
    for drop_dir in drop_in_dirs(path, &name) {
        if !drop_dir.is_dir() {
            continue;
        }
        let mut confs: Vec<PathBuf> = match std::fs::read_dir(&drop_dir) {
            Ok(rd) => rd
                .flatten()
                .map(|e| e.path())
                .filter(|p| p.extension().map(|x| x == "conf").unwrap_or(false))
                .collect(),
            Err(e) => {
                unit.diagnostics.push(Diagnostic {
                    code: "DROPIN_UNREADABLE".to_string(),
                    severity: "warning".to_string(),
                    message: format!("cannot list {}: {}", drop_dir.display(), e),
                    path: Some(drop_dir.to_string_lossy().into_owned()),
                    line: None,
                });
                Vec::new()
            }
        };
        confs.sort();
        for conf in confs {
            match std::fs::read_to_string(&conf) {
                Ok(c) => apply_drop_in(&mut unit, &c, &conf.to_string_lossy()),
                Err(e) => unit.diagnostics.push(Diagnostic {
                    code: "DROPIN_UNREADABLE".to_string(),
                    severity: "warning".to_string(),
                    message: format!("cannot read {}: {}", conf.display(), e),
                    path: Some(conf.to_string_lossy().into_owned()),
                    line: None,
                }),
            }
        }
    }
    Ok(unit)
}

/// Load every unit under `path` — either a single file or a directory of
/// unit files — each with its drop-ins applied.
pub fn load_units(path: &Path) -> Result<BTreeMap<String, SystemdUnit>> {
    let mut units = BTreeMap::new();
    if !path.exists() {
        // Fail closed: a misspelled --systemd-dir must not yield a vacuous
        // "0 units, no cycles" report.
        return Err(LensError::Io {
            path: path.to_path_buf(),
            source: std::io::Error::new(std::io::ErrorKind::NotFound, "unit path does not exist"),
        });
    }
    if path.is_file() {
        let unit = load_unit(path)?;
        units.insert(unit.name.clone(), unit);
    } else if path.is_dir() {
        let entries = std::fs::read_dir(path).map_err(|e| LensError::Io {
            path: path.to_path_buf(),
            source: e,
        })?;
        let mut paths: Vec<PathBuf> = entries
            .flatten()
            .map(|e| e.path())
            .filter(|p| {
                p.is_file()
                    && p.file_name()
                        .and_then(|n| n.to_str())
                        .map(|n| UNIT_SUFFIXES.iter().any(|s| n.ends_with(s)))
                        .unwrap_or(false)
            })
            .collect();
        paths.sort();
        for p in paths {
            match load_unit(&p) {
                Ok(unit) => {
                    units.insert(unit.name.clone(), unit);
                }
                Err(e) => {
                    // Fail-closed: an unreadable unit is recorded rather than
                    // silently skipped, so diagnostics surface the gap.
                    let name = p
                        .file_name()
                        .and_then(|n| n.to_str())
                        .unwrap_or("unknown")
                        .to_string();
                    let mut unit = SystemdUnit {
                        name: name.clone(),
                        path: Some(p.to_string_lossy().into_owned()),
                        ..Default::default()
                    };
                    unit.diagnostics.push(Diagnostic {
                        code: "UNIT_UNREADABLE".to_string(),
                        severity: "error".to_string(),
                        message: format!("{}", e),
                        path: Some(p.to_string_lossy().into_owned()),
                        line: None,
                    });
                    units.insert(name, unit);
                }
            }
        }
    }
    Ok(units)
}

/// Drop-in directories for a unit, in merge order. `foo@bar.service` gets
/// `foo@.service.d/` (template level) then `foo@bar.service.d/`.
fn drop_in_dirs(unit_path: &Path, unit_name: &str) -> Vec<PathBuf> {
    let parent = unit_path.parent().unwrap_or_else(|| Path::new("."));
    let mut dirs = Vec::new();
    if let Some(at) = unit_name.find('@') {
        if let Some(dot) = unit_name.rfind('.') {
            if at < dot {
                let template = format!("{}@{}.d", &unit_name[..at], &unit_name[dot..]);
                dirs.push(parent.join(template));
            }
        }
    }
    dirs.push(parent.join(format!("{}.d", unit_name)));
    dirs
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_drop_in_reset_and_specifiers() {
        let dir = std::env::temp_dir().join(format!("lenssys-{}", std::process::id()));
        let drop = dir.join("aa.service.d");
        std::fs::create_dir_all(&drop).unwrap();
        std::fs::write(
            dir.join("aa.service"),
            "[Unit]\nWants=b.service\n[Service]\nUser=nobody\nExecStart=/bin/old\nExecStartPre=/bin/pre %u %h\n",
        )
        .unwrap();
        std::fs::write(
            drop.join("10-override.conf"),
            "[Service]\nExecStart=\nExecStart=/bin/new\n",
        )
        .unwrap();

        let units = load_units(&dir).unwrap();
        let unit = &units["aa.service"];
        // ExecStart= reset clears /bin/old before /bin/new is appended
        assert_eq!(unit.exec_start.as_deref(), Some("/bin/new"));
        assert_eq!(
            unit.sections["Service"]["ExecStart"],
            vec!["/bin/new".to_string()]
        );
        // %u/%h resolve to the unit's User=, not hardcoded root
        assert_eq!(
            unit.sections["Service"]["ExecStartPre"],
            vec!["/bin/pre nobody /home/nobody".to_string()]
        );
        assert_eq!(unit.drop_ins.len(), 1);
        assert_eq!(unit.wants, vec!["b.service".to_string()]);
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_template_drop_in_merges_before_instance() {
        let dir = std::env::temp_dir().join(format!("lenssys-tpl-{}", std::process::id()));
        let tpl = dir.join("foo@.service.d");
        let inst = dir.join("foo@bar.service.d");
        std::fs::create_dir_all(&tpl).unwrap();
        std::fs::create_dir_all(&inst).unwrap();
        std::fs::write(
            dir.join("foo@bar.service"),
            "[Service]\nExecStart=/run/%i\n",
        )
        .unwrap();
        std::fs::write(tpl.join("10-tpl.conf"), "[Unit]\nDescription=tpl\n").unwrap();
        std::fs::write(inst.join("20-inst.conf"), "[Unit]\nDescription=inst\n").unwrap();

        let unit = load_unit(&dir.join("foo@bar.service")).unwrap();
        assert_eq!(unit.drop_ins.len(), 2);
        // instance drop-in wins on Description (merged after template —
        // last value wins for single-valued keys)
        assert_eq!(unit.sections["Unit"]["Description"].last().unwrap(), "inst");
        // %i expands to the instance name
        assert_eq!(unit.exec_start.as_deref(), Some("/run/bar"));
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_load_units_rejects_missing_path() {
        let missing = std::path::Path::new("/nonexistent-lens-sys-dir-xyz");
        assert!(load_units(missing).is_err());
    }
}
