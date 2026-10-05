pub mod compiler;
pub mod diff;
pub mod impact;
pub mod model;

pub use compiler::{parse_command_entry, split_command_line};
pub use diff::diff_compilations;
pub use impact::{extract_includes, ImpactGraph};
pub use model::*;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_compile_command() {
        let entry = CompileCommandEntry {
            directory: "/project/build".to_string(),
            file: "/project/src/main.cpp".to_string(),
            command: None,
            arguments: Some(vec![
                "/usr/bin/c++".to_string(),
                "-DBUILDSCOPE_APP=1".to_string(),
                "-std=c++20".to_string(),
                "-I/project/include".to_string(),
                "-Wall".to_string(),
                "-c".to_string(),
                "/project/src/main.cpp".to_string(),
                "-o".to_string(),
                "main.o".to_string(),
            ]),
            output: None,
        };

        let unit = parse_command_entry(&entry);
        assert_eq!(unit.compiler, "/usr/bin/c++");
        assert_eq!(unit.defines, vec!["BUILDSCOPE_APP=1"]);
        assert_eq!(unit.standard, Some("c++20".to_string()));
        assert_eq!(unit.includes, vec!["/project/include"]);
        assert_eq!(unit.output, Some("main.o".to_string()));
        assert_eq!(unit.flags, vec!["-Wall"]);
    }

    #[test]
    fn test_impact_analysis() {
        let mut graph = ImpactGraph::new();

        // main.cpp -> app.h -> config.h
        // core.cpp -> config.h
        graph.add_unit_include("/project/src/main.cpp", "/project/include/app.h");
        graph.add_header_include("/project/include/app.h", "/project/include/config.h");
        graph.add_unit_include("/project/src/core.cpp", "/project/include/config.h");

        // If config.h changes, both main.cpp and core.cpp must be rebuilt!
        let report = graph.compute_impact("/project/include/config.h");
        assert_eq!(report.total_impacted, 2);
        assert!(report
            .impacted_units
            .contains(&"/project/src/main.cpp".to_string()));
        assert!(report
            .impacted_units
            .contains(&"/project/src/core.cpp".to_string()));

        // If app.h changes, only main.cpp is impacted
        let report_app = graph.compute_impact("/project/include/app.h");
        assert_eq!(report_app.total_impacted, 1);
        assert_eq!(
            report_app.impacted_units,
            vec!["/project/src/main.cpp".to_string()]
        );
    }

    #[test]
    fn test_diff_compilations() {
        let u1 = ParsedUnit {
            file: "/project/src/main.cpp".to_string(),
            directory: "/project/build".to_string(),
            compiler: "clang++".to_string(),
            includes: vec!["/usr/include".to_string()],
            defines: vec!["DEBUG=1".to_string()],
            output: Some("main.o".to_string()),
            standard: Some("c++20".to_string()),
            flags: vec!["-O0".to_string()],
        };

        let u2 = ParsedUnit {
            file: "/project/src/main.cpp".to_string(),
            directory: "/project/build".to_string(),
            compiler: "clang++".to_string(),
            includes: vec!["/usr/include".to_string()],
            defines: vec!["NDEBUG=1".to_string()],
            output: Some("main.o".to_string()),
            standard: Some("c++20".to_string()),
            flags: vec!["-O3".to_string()],
        };

        let diff = diff_compilations(&[u1], &[u2]);
        assert_eq!(diff.modified_units.len(), 1);
        let mod_unit = &diff.modified_units[0];
        assert_eq!(mod_unit.added_flags, vec!["-O3"]);
        assert_eq!(mod_unit.removed_flags, vec!["-O0"]);
        assert_eq!(mod_unit.added_defines, vec!["NDEBUG=1"]);
        assert_eq!(mod_unit.removed_defines, vec!["DEBUG=1"]);
    }
}
