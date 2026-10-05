use crate::model::{PyPackage, ShadowingIssue};
use std::collections::{BTreeMap, HashSet};
use std::path::Path;
use walkdir::WalkDir;

const STDLIB_MODULES: &[&str] = &[
    "abc",
    "argparse",
    "ast",
    "asyncio",
    "base64",
    "builtins",
    "collections",
    "contextlib",
    "copy",
    "csv",
    "datetime",
    "decimal",
    "email",
    "enum",
    "functools",
    "glob",
    "hashlib",
    "http",
    "importlib",
    "inspect",
    "io",
    "itertools",
    "json",
    "logging",
    "math",
    "multiprocessing",
    "operator",
    "os",
    "pathlib",
    "pickle",
    "platform",
    "queue",
    "random",
    "re",
    "shutil",
    "signal",
    "socket",
    "sqlite3",
    "ssl",
    "stat",
    "string",
    "subprocess",
    "sys",
    "tempfile",
    "threading",
    "time",
    "token",
    "tokenize",
    "traceback",
    "types",
    "typing",
    "unittest",
    "urllib",
    "uuid",
    "warnings",
    "weakref",
    "zipfile",
];

pub fn detect_shadowing(
    project_root: &Path,
    installed_packages: &BTreeMap<String, PyPackage>,
) -> Vec<ShadowingIssue> {
    let mut issues = Vec::new();
    let stdlib_set: HashSet<&str> = STDLIB_MODULES.iter().copied().collect();
    let installed_names: HashSet<String> = installed_packages.keys().cloned().collect();

    let scan_dirs = [project_root.to_path_buf(), project_root.join("src")];

    for dir in &scan_dirs {
        if !dir.exists() {
            continue;
        }

        for entry in WalkDir::new(dir).max_depth(2).into_iter().flatten() {
            let path = entry.path();
            if path.is_file() {
                if let Some(file_name) = path.file_name().and_then(|n| n.to_str()) {
                    if file_name.ends_with(".py") && file_name != "__init__.py" {
                        let mod_name = &file_name[..file_name.len() - 3];

                        if stdlib_set.contains(mod_name) {
                            issues.push(ShadowingIssue {
                                module_name: mod_name.to_string(),
                                local_path: path.to_string_lossy().to_string(),
                                shadows: format!("Python standard library module '{}'", mod_name),
                            });
                        } else if installed_names.contains(mod_name) {
                            issues.push(ShadowingIssue {
                                module_name: mod_name.to_string(),
                                local_path: path.to_string_lossy().to_string(),
                                shadows: format!("Installed third-party package '{}'", mod_name),
                            });
                        }
                    }
                }
            }
        }
    }

    issues.sort_by(|a, b| a.module_name.cmp(&b.module_name));
    issues.dedup();
    issues
}
