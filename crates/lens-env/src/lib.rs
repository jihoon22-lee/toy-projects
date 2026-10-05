pub mod diff;
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
    fn test_diff_environments() {
        let mut v1 = PyVenv {
            path: "/venv1".to_string(),
            python_version: "3.11.0".to_string(),
            home: "/usr/bin".to_string(),
            packages: std::collections::BTreeMap::new(),
            missing_dependencies: vec![],
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
            },
        );

        let mut v2 = PyVenv {
            path: "/venv2".to_string(),
            python_version: "3.11.0".to_string(),
            home: "/usr/bin".to_string(),
            packages: std::collections::BTreeMap::new(),
            missing_dependencies: vec![],
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
