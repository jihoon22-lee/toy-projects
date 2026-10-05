pub mod dag;
pub mod diff;
pub mod loader;
pub mod model;
pub mod parser;

pub use dag::OrderingGraph;
pub use diff::diff_systemd;
pub use loader::{load_unit, load_units};
pub use model::*;
pub use parser::{apply_drop_in, expand_specifiers, parse_unit_content};

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeMap;

    #[test]
    fn test_parse_unit_and_specifiers() {
        let content = r#"
[Unit]
Description=My Service %i on %n
Wants=network.target network-online.target
After=network.target

[Service]
Type=simple
ExecStart=/usr/bin/my-daemon --instance=%i --user=%u
"#;
        let unit = parse_unit_content(
            content,
            "my-daemon@test.service",
            Some("/etc/systemd/system/my-daemon@test.service"),
        );
        assert_eq!(unit.name, "my-daemon@test.service");
        assert_eq!(unit.wants, vec!["network-online.target", "network.target"]);
        assert_eq!(unit.after, vec!["network.target"]);
        assert_eq!(
            unit.exec_start,
            Some("/usr/bin/my-daemon --instance=test --user=root".to_string())
        );
    }

    #[test]
    fn test_drop_in_override() {
        let base_content = r#"
[Unit]
Description=Base Service
After=syslog.target

[Service]
ExecStart=/usr/bin/old-daemon
"#;
        let mut unit = parse_unit_content(base_content, "base.service", None);

        let drop_in = r#"
[Unit]
Wants=redis.service

[Service]
ExecStart=
ExecStart=/usr/bin/new-daemon --fast
"#;
        apply_drop_in(
            &mut unit,
            drop_in,
            "/etc/systemd/system/base.service.d/override.conf",
        );

        assert_eq!(unit.wants, vec!["redis.service"]);
        assert_eq!(
            unit.exec_start,
            Some("/usr/bin/new-daemon --fast".to_string())
        );
        assert_eq!(unit.drop_ins.len(), 1);
    }

    #[test]
    fn test_cycle_detection() {
        let mut units = BTreeMap::new();

        let u1 = parse_unit_content("[Unit]\nBefore=b.service\n", "a.service", None);
        let u2 = parse_unit_content("[Unit]\nBefore=c.service\n", "b.service", None);
        let u3 = parse_unit_content("[Unit]\nBefore=a.service\n", "c.service", None);

        units.insert("a.service".to_string(), u1);
        units.insert("b.service".to_string(), u2);
        units.insert("c.service".to_string(), u3);

        let graph = OrderingGraph::build(&units);
        let cycles = graph.find_cycles();

        assert_eq!(cycles.len(), 1);
        assert_eq!(cycles[0], vec!["a.service", "b.service", "c.service"]);
        assert!(graph.topological_sort().is_none());
    }

    #[test]
    fn test_diff_systemd() {
        let snap1 = SystemdSnapshot {
            schema: SNAPSHOT_SCHEMA_V1.to_string(),
            version: env!("CARGO_PKG_VERSION").to_string(),
            semantics: "systemd-255-subset-v1".to_string(),
            units: {
                let mut map = BTreeMap::new();
                map.insert(
                    "a.service".to_string(),
                    parse_unit_content("[Service]\nExecStart=/bin/a", "a.service", None),
                );
                map
            },
            cycles: vec![],
            diagnostics: vec![],
        };

        let snap2 = SystemdSnapshot {
            schema: SNAPSHOT_SCHEMA_V1.to_string(),
            version: env!("CARGO_PKG_VERSION").to_string(),
            semantics: "systemd-255-subset-v1".to_string(),
            units: {
                let mut map = BTreeMap::new();
                map.insert(
                    "a.service".to_string(),
                    parse_unit_content("[Service]\nExecStart=/bin/a_new", "a.service", None),
                );
                map.insert(
                    "b.service".to_string(),
                    parse_unit_content("[Service]\nExecStart=/bin/b", "b.service", None),
                );
                map
            },
            cycles: vec![],
            diagnostics: vec![],
        };

        let diff = diff_systemd(&snap1, &snap2);
        assert_eq!(diff.added_units, vec!["b.service"]);
        assert!(diff.removed_units.is_empty());
        assert_eq!(diff.modified_units.len(), 1);
        assert_eq!(diff.modified_units[0].unit, "a.service");
    }
}
