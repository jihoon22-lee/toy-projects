use crate::metadata::{normalize_package_name, parse_metadata};
use crate::model::{PyPackage, PyVenv};
use std::collections::BTreeMap;
use std::fs;
use std::path::Path;

pub fn inspect_venv(venv_path: &Path) -> std::io::Result<PyVenv> {
    validate_venv(venv_path)?;

    let mut home = String::new();
    let mut python_version = String::new();

    let cfg_path = venv_path.join("pyvenv.cfg");
    if cfg_path.exists() {
        if let Ok(content) = fs::read_to_string(&cfg_path) {
            for line in content.lines() {
                if let Some(rest) = line.strip_prefix("home =") {
                    home = rest.trim().to_string();
                } else if let Some(rest) = line.strip_prefix("version =") {
                    python_version = rest.trim().to_string();
                }
            }
        }
    }

    let mut packages: BTreeMap<String, PyPackage> = BTreeMap::new();

    // Look in lib/python*/site-packages
    let lib_dir = venv_path.join("lib");
    if lib_dir.exists() {
        if let Ok(entries) = fs::read_dir(&lib_dir) {
            for entry in entries.flatten() {
                let p = entry.path();
                if p.is_dir() {
                    let site_packages = p.join("site-packages");
                    if site_packages.exists() {
                        scan_site_packages(&site_packages, &mut packages);
                    }
                }
            }
        }
    }

    // Also look directly in lib/site-packages (e.g. Windows or flat venvs)
    let direct_site_packages = lib_dir.join("site-packages");
    if direct_site_packages.exists() {
        scan_site_packages(&direct_site_packages, &mut packages);
    }

    // Check missing dependencies
    let mut missing_dependencies = Vec::new();
    for pkg in packages.values() {
        for req in &pkg.requires_dist {
            // Strip environment markers (after ';')
            let req_base = if let Some(semi) = req.find(';') {
                &req[..semi]
            } else {
                req.as_str()
            };

            let req_name: String = req_base
                .chars()
                .take_while(|c| c.is_alphanumeric() || *c == '-' || *c == '_' || *c == '.')
                .collect();

            if !req_name.is_empty() {
                let norm = normalize_package_name(&req_name);
                if !packages.contains_key(&norm) {
                    missing_dependencies.push(format!(
                        "Package '{}' requires '{}' (not installed)",
                        pkg.name,
                        req.trim()
                    ));
                }
            }
        }
    }
    missing_dependencies.sort();
    missing_dependencies.dedup();

    Ok(PyVenv {
        path: venv_path.to_string_lossy().to_string(),
        python_version,
        home,
        packages,
        missing_dependencies,
        shadowing_issues: Vec::new(),
    })
}

/// A path that is not a virtualenv must not yield an empty "all satisfied"
/// report — fail closed on missing `pyvenv.cfg` and `site-packages`.
fn validate_venv(venv_path: &Path) -> std::io::Result<()> {
    if venv_path.join("pyvenv.cfg").is_file() {
        return Ok(());
    }
    let lib = venv_path.join("lib");
    let has_site_packages = lib.join("site-packages").is_dir()
        || fs::read_dir(&lib)
            .map(|entries| {
                entries
                    .flatten()
                    .any(|e| e.path().join("site-packages").is_dir())
            })
            .unwrap_or(false);
    if has_site_packages {
        return Ok(());
    }
    Err(std::io::Error::new(
        std::io::ErrorKind::InvalidInput,
        format!(
            "not a Python virtualenv (no pyvenv.cfg or site-packages): {}",
            venv_path.display()
        ),
    ))
}

fn scan_site_packages(site_packages: &Path, packages: &mut BTreeMap<String, PyPackage>) {
    if let Ok(entries) = fs::read_dir(site_packages) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                if let Some(dir_name) = path.file_name().and_then(|n| n.to_str()) {
                    if dir_name.ends_with(".dist-info") {
                        let meta_path = path.join("METADATA");
                        if meta_path.exists() {
                            if let Ok(content) = fs::read_to_string(&meta_path) {
                                if let Some(pkg) = parse_metadata(&content, Some(dir_name)) {
                                    let norm = normalize_package_name(&pkg.name);
                                    packages.insert(norm, pkg);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
