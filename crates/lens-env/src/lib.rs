pub mod diff;
pub mod markers;
pub mod metadata;
pub mod model;
pub mod shadowing;
pub mod venv;

pub use diff::diff_environments;
pub use metadata::{normalize_package_name, parse_metadata};
pub use model::*;
pub use shadowing::detect_shadowing;
pub use venv::inspect_venv;

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::tempdir;

    #[test]
    fn test_parse_metadata() {
        let sample = r#"Metadata-Version: 2.1
Name: requests
Version: 2.31.0
Summary: Python HTTP for Humans.
Requires-Dist: charset_normalizer<4,>=2
Requires-Dist: idna<4,>=2.5
Requires-Dist: urllib3<3,>=1.21.1
Requires-Dist: certifi>=2017.4.17
"#;
        let pkg = parse_metadata(sample, Some("requests-2.31.0.dist-info"))
            .expect("Failed to parse metadata");
        assert_eq!(pkg.name, "requests");
        assert_eq!(pkg.version, "2.31.0");
        assert_eq!(pkg.summary, Some("Python HTTP for Humans.".to_string()));
        assert_eq!(pkg.requires_dist.len(), 4);
    }

    #[test]
    fn test_shadowing_detection() {
        let dir = tempdir().expect("tempdir");
        let root = dir.path();

        // Create a local email.py that shadows stdlib email
        fs::write(root.join("email.py"), "def send(): pass\n").expect("write");

        let mut installed = std::collections::BTreeMap::new();
        installed.insert(
            "requests".to_string(),
            PyPackage {
                name: "requests".to_string(),
                version: "2.31.0".to_string(),
                summary: None,
                requires_dist: vec![],
                dist_info: None,
                top_level_modules: vec![],
            },
        );

        // Also create a local requests.py that shadows installed package
        fs::write(root.join("requests.py"), "def get(): pass\n").expect("write");

        let issues = detect_shadowing(root, &installed);
        assert_eq!(issues.len(), 2);
        assert_eq!(issues[0].module_name, "email");
        assert!(issues[0].shadows.contains("standard library"));
        assert_eq!(issues[1].module_name, "requests");
        assert!(issues[1].shadows.contains("Installed third-party"));
    }

    #[test]
    fn test_shadowing_nested_module_not_flagged() {
        let dir = tempdir().expect("tempdir");
        let root = dir.path();
        // `pkg/json.py` imports as `pkg.json` — it does not shadow stdlib
        // `json`. Only depth-1 names shadow top-level imports.
        fs::create_dir_all(root.join("pkg")).unwrap();
        fs::write(root.join("pkg/__init__.py"), "").unwrap();
        fs::write(root.join("pkg/json.py"), "x = 1\n").unwrap();

        let issues = detect_shadowing(root, &std::collections::BTreeMap::new());
        assert!(issues.is_empty(), "unexpected: {issues:?}");
    }

    #[test]
    fn test_shadowing_package_dir_and_wider_stdlib() {
        let dir = tempdir().expect("tempdir");
        let root = dir.path();
        // A package directory shadows the stdlib module of the same name.
        fs::create_dir_all(root.join("logging")).unwrap();
        fs::write(root.join("logging/__init__.py"), "x = 1\n").unwrap();
        // `secrets` was missing from the old 58-entry list.
        fs::write(root.join("secrets.py"), "x = 1\n").unwrap();
        // src/ layout is scanned too.
        fs::create_dir_all(root.join("src/email")).unwrap();
        fs::write(root.join("src/email/__init__.py"), "x = 1\n").unwrap();

        let issues = detect_shadowing(root, &std::collections::BTreeMap::new());
        let names: Vec<&str> = issues.iter().map(|i| i.module_name.as_str()).collect();
        assert_eq!(names, vec!["email", "logging", "secrets"]);
    }

    #[test]
    fn test_shadowing_uses_top_level_modules() {
        let dir = tempdir().expect("tempdir");
        let root = dir.path();
        // Dist `PyYAML` provides module `yaml` — a local `yaml.py`
        // shadows it even though the names differ.
        fs::write(root.join("yaml.py"), "x = 1\n").unwrap();

        let mut installed = std::collections::BTreeMap::new();
        installed.insert(
            "pyyaml".to_string(),
            PyPackage {
                name: "PyYAML".to_string(),
                version: "6.0".to_string(),
                summary: None,
                requires_dist: vec![],
                dist_info: Some("PyYAML-6.0.dist-info".to_string()),
                top_level_modules: vec!["yaml".to_string()],
            },
        );
        let issues = detect_shadowing(root, &installed);
        assert_eq!(issues.len(), 1);
        assert_eq!(issues[0].module_name, "yaml");
        assert!(issues[0].shadows.contains("PyYAML"));
    }

    #[test]
    fn test_diff_environments() {
        let mut v1 = PyVenv {
            path: "/venv1".to_string(),
            python_version: "3.11.0".to_string(),
            home: "/usr/bin".to_string(),
            packages: std::collections::BTreeMap::new(),
            missing_dependencies: vec![],
            version_conflicts: vec![],
            unevaluated_dependencies: vec![],
            shadowing_issues: vec![],
        };
        v1.packages.insert(
            "requests".to_string(),
            PyPackage {
                name: "requests".to_string(),
                version: "2.28.0".to_string(),
                summary: None,
                requires_dist: vec![],
                dist_info: None,
                top_level_modules: vec![],
            },
        );

        let mut v2 = PyVenv {
            path: "/venv2".to_string(),
            python_version: "3.11.0".to_string(),
            home: "/usr/bin".to_string(),
            packages: std::collections::BTreeMap::new(),
            missing_dependencies: vec![],
            version_conflicts: vec![],
            unevaluated_dependencies: vec![],
            shadowing_issues: vec![],
        };
        v2.packages.insert(
            "requests".to_string(),
            PyPackage {
                name: "requests".to_string(),
                version: "2.31.0".to_string(),
                summary: None,
                requires_dist: vec![],
                dist_info: None,
                top_level_modules: vec![],
            },
        );
        v2.packages.insert(
            "pytest".to_string(),
            PyPackage {
                name: "pytest".to_string(),
                version: "7.4.0".to_string(),
                summary: None,
                requires_dist: vec![],
                dist_info: None,
                top_level_modules: vec![],
            },
        );

        let diff = diff_environments(&v1, &v2);
        assert_eq!(diff.added_packages, vec!["pytest"]);
        assert!(diff.removed_packages.is_empty());
        assert_eq!(diff.version_changes.len(), 1);
        assert_eq!(diff.version_changes[0].package, "requests");
        assert_eq!(diff.version_changes[0].old_version, "2.28.0");
        assert_eq!(diff.version_changes[0].new_version, "2.31.0");
    }
}
