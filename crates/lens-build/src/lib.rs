pub mod compiler;
pub mod diff;
pub mod impact;
pub mod model;

pub use compiler::{normalize_path, parse_command_entry, split_command_line};
pub use diff::diff_compilations;
pub use impact::{extract_includes, ImpactGraph, IncludeDirective};
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
    fn test_direct_vs_transitive_includers() {
        let mut graph = ImpactGraph::new();
        // a.c includes mid.h; mid.h includes leaf.h — a.c is only a
        // transitive includer of leaf.h.
        graph.add_unit_include("/p/src/a.c", "/p/inc/mid.h");
        graph.add_header_include("/p/inc/mid.h", "/p/inc/leaf.h");

        // The snapshot's reverse_impact (direct includers) must NOT list
        // a.c under leaf.h...
        let direct: Vec<&String> = graph
            .header_to_units
            .get("/p/inc/leaf.h")
            .map(|s| s.iter().collect())
            .unwrap_or_default();
        assert!(direct.is_empty());

        // ...while transitive_impact does.
        let report = graph.compute_impact("/p/inc/leaf.h");
        assert_eq!(report.impacted_units, vec!["/p/src/a.c".to_string()]);
    }

    #[test]
    fn test_add_translation_unit_resolves_on_disk_includes() {
        let root = std::env::temp_dir().join(format!("lensbuild-{}", std::process::id()));
        let inc_dir = root.join("include");
        let src_dir = root.join("src");
        std::fs::create_dir_all(&inc_dir).unwrap();
        std::fs::create_dir_all(&src_dir).unwrap();
        std::fs::write(inc_dir.join("config.h"), "#define X 1\n").unwrap();
        std::fs::write(src_dir.join("app.h"), "#include \"config.h\"\n").unwrap();
        std::fs::write(src_dir.join("main.cpp"), "#include \"app.h\"\n").unwrap();
        std::fs::write(src_dir.join("core.cpp"), "#include <config.h>\n").unwrap();

        let mk = |file: &str, args: Vec<&str>| {
            let entry = CompileCommandEntry {
                directory: root.to_string_lossy().into_owned(),
                file: src_dir.join(file).to_string_lossy().into_owned(),
                command: None,
                arguments: Some(
                    std::iter::once("c++".to_string())
                        .chain(args.into_iter().map(String::from))
                        .collect(),
                ),
                output: None,
            };
            parse_command_entry(&entry)
        };
        let inc_arg = format!("-I{}", inc_dir.display());
        let u_main = mk("main.cpp", vec![inc_arg.as_str()]);
        let u_core = mk("core.cpp", vec![inc_arg.as_str()]);

        let mut graph = ImpactGraph::new();
        graph.add_translation_unit(&u_main);
        graph.add_translation_unit(&u_core);

        // Transitive: config.h -> app.h -> main.cpp; direct: config.h -> core.cpp
        let config_key = inc_dir.join("config.h").to_string_lossy().into_owned();
        let report = graph.compute_impact(&config_key);
        assert_eq!(report.total_impacted, 2);
        let main_key = src_dir.join("main.cpp").to_string_lossy().into_owned();
        let core_key = src_dir.join("core.cpp").to_string_lossy().into_owned();
        assert!(report.impacted_units.contains(&main_key));
        assert!(report.impacted_units.contains(&core_key));

        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn test_include_resolution_normalizes_dotdot_and_preserves_order() {
        let root = std::env::temp_dir().join(format!("lensbuild-dd-{}", std::process::id()));
        // Layout: root/proj/inc/a_shadow.h? no — want ../ include from src/.
        let src = root.join("proj/src");
        let hdr = root.join("proj");
        std::fs::create_dir_all(&src).unwrap();
        // common.h lives next to src/, included as "../common.h"
        std::fs::write(hdr.join("common.h"), "#define C 1\n").unwrap();
        std::fs::write(
            src.join("m.c"),
            "#include \"../common.h\" // trailing comment\n/*\n#include \"dead.h\"\n*/\n",
        )
        .unwrap();

        let entry = CompileCommandEntry {
            directory: root.to_string_lossy().into_owned(),
            file: src.join("m.c").to_string_lossy().into_owned(),
            command: None,
            arguments: Some(vec!["cc".to_string()]),
            output: None,
        };
        let unit = parse_command_entry(&entry);
        let mut graph = ImpactGraph::new();
        graph.add_translation_unit(&unit);

        // The canonical path must be the graph key — a `..`-spelled include
        // resolves to the same key the CLI's normalized --header produces.
        let canonical = hdr.join("common.h").to_string_lossy().into_owned();
        let report = graph.compute_impact(&canonical);
        assert_eq!(report.total_impacted, 1);
        // The commented-out include must NOT have produced an edge.
        assert!(graph.header_to_units.keys().all(|k| !k.contains("dead.h")));
        // Trailing `//` comment didn't hide the include.
        assert!(graph.header_to_units.contains_key(&canonical));

        // -I order is preserved (first match wins).
        let a = root.join("order/a");
        let b = root.join("order/b");
        std::fs::create_dir_all(&a).unwrap();
        std::fs::create_dir_all(&b).unwrap();
        let entry = CompileCommandEntry {
            directory: root.to_string_lossy().into_owned(),
            file: src.join("m.c").to_string_lossy().into_owned(),
            command: None,
            arguments: Some(vec![
                "cc".to_string(),
                format!("-I{}", b.display()),
                format!("-I{}", a.display()),
            ]),
            output: None,
        };
        let unit = parse_command_entry(&entry);
        assert_eq!(
            unit.includes,
            vec![
                b.to_string_lossy().into_owned(),
                a.to_string_lossy().into_owned()
            ]
        );

        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn test_extended_include_flags() {
        // -iquote/-idirafter contribute include dirs (both separate-arg and
        // joined forms), -imacros contributes a forced include, and
        // -isysroot consumes its value without leaking into `flags`.
        let entry = CompileCommandEntry {
            directory: "/p".to_string(),
            file: "m.c".to_string(),
            command: None,
            arguments: Some(vec![
                "cc".to_string(),
                "-iquote".to_string(),
                "q".to_string(),
                "-idirafterD".to_string(),
                "-imacros".to_string(),
                "pre.h".to_string(),
                "-isysroot".to_string(),
                "/sdk".to_string(),
                "-Wall".to_string(),
            ]),
            output: None,
        };
        let unit = parse_command_entry(&entry);
        assert_eq!(unit.includes, vec!["/p/q".to_string(), "/p/D".to_string()]);
        assert_eq!(unit.forced_includes, vec!["pre.h".to_string()]);
        assert_eq!(unit.flags, vec!["-Wall".to_string()]);
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
            forced_includes: Vec::new(),
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
            forced_includes: Vec::new(),
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
