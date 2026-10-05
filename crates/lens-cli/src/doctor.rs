use std::env;
use std::fs;
use std::path::Path;

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum HealthStatus {
    Pass,
    Warn,
    Fail,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DoctorCheck {
    pub category: String,
    pub name: String,
    pub status: HealthStatus,
    pub message: String,
    pub recommendation: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DoctorSummary {
    pub total: usize,
    pub passed: usize,
    pub warnings: usize,
    pub failures: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DoctorReport {
    pub schema_version: String,
    pub overall_status: HealthStatus,
    pub checks: Vec<DoctorCheck>,
    pub summary: DoctorSummary,
}

pub fn check_storage(root_path: &Path) -> DoctorCheck {
    let mut stat: libc::statvfs = unsafe { std::mem::zeroed() };
    let c_path = std::ffi::CString::new(root_path.to_string_lossy().as_bytes()).unwrap_or_default();
    let res = unsafe { libc::statvfs(c_path.as_ptr(), &mut stat) };

    if res == 0 && stat.f_blocks > 0 {
        let total_bytes = stat.f_blocks as u64 * stat.f_frsize as u64;
        let avail_bytes = stat.f_bavail as u64 * stat.f_frsize as u64;
        let free_pct = (avail_bytes as f64 / total_bytes as f64) * 100.0;
        let avail_gb = avail_bytes as f64 / (1024.0 * 1024.0 * 1024.0);

        if free_pct < 5.0 || avail_gb < 1.0 {
            DoctorCheck {
                category: "Storage".to_string(),
                name: "Root Disk Capacity".to_string(),
                status: HealthStatus::Fail,
                message: format!(
                    "Critically low disk space: {:.1}% free ({:.2} GB available)",
                    free_pct, avail_gb
                ),
                recommendation: Some(
                    "Clean up disk immediately using 'lens disk scan' and trash old files."
                        .to_string(),
                ),
            }
        } else if free_pct < 15.0 || avail_gb < 5.0 {
            DoctorCheck {
                category: "Storage".to_string(),
                name: "Root Disk Capacity".to_string(),
                status: HealthStatus::Warn,
                message: format!(
                    "Low disk space warning: {:.1}% free ({:.2} GB available)",
                    free_pct, avail_gb
                ),
                recommendation: Some(
                    "Consider pruning large unused logs or artifacts.".to_string(),
                ),
            }
        } else {
            DoctorCheck {
                category: "Storage".to_string(),
                name: "Root Disk Capacity".to_string(),
                status: HealthStatus::Pass,
                message: format!(
                    "Adequate disk space: {:.1}% free ({:.2} GB available)",
                    free_pct, avail_gb
                ),
                recommendation: None,
            }
        }
    } else {
        DoctorCheck {
            category: "Storage".to_string(),
            name: "Root Disk Capacity".to_string(),
            status: HealthStatus::Warn,
            message: "Unable to query filesystem statistics".to_string(),
            recommendation: None,
        }
    }
}

pub fn check_network(proc_path: Option<&Path>) -> Vec<DoctorCheck> {
    let mut checks = Vec::new();

    let net_report = match lens_net::parser::inspect_network(proc_path) {
        Ok(r) => r,
        Err(e) => {
            checks.push(DoctorCheck {
                category: "Network".to_string(),
                name: "Procfs Network Parsing".to_string(),
                status: HealthStatus::Warn,
                message: format!("Failed to parse network sockets: {}", e),
                recommendation: Some("Ensure /proc/net is mounted and accessible.".to_string()),
            });
            return checks;
        }
    };

    checks.push(DoctorCheck {
        category: "Network".to_string(),
        name: "Network Sockets Overview".to_string(),
        status: HealthStatus::Pass,
        message: format!(
            "Total sockets: {}, Active listeners: {}, Established: {}",
            net_report.summary.total_sockets,
            net_report.summary.listening_ports,
            net_report.summary.established_connections
        ),
        recommendation: None,
    });

    let sensitive_ports = [
        (22, "SSH"),
        (23, "Telnet"),
        (3306, "MySQL"),
        (5432, "PostgreSQL"),
        (6379, "Redis"),
        (27017, "MongoDB"),
    ];

    let mut exposed_sensitive = Vec::new();
    for p in &net_report.listening {
        let is_wildcard =
            p.local_address == "0.0.0.0" || p.local_address == "::" || p.local_address.is_empty();
        if is_wildcard {
            for &(port, name) in &sensitive_ports {
                if p.local_port == port {
                    let item = format!("{}(port {})", name, port);
                    if !exposed_sensitive.contains(&item) {
                        exposed_sensitive.push(item);
                    }
                }
            }
        }
    }

    if !exposed_sensitive.is_empty() {
        checks.push(DoctorCheck {
            category: "Network".to_string(),
            name: "Wildcard Sensitive Listeners".to_string(),
            status: HealthStatus::Warn,
            message: format!(
                "Sensitive services exposed on wildcard 0.0.0.0/:: address: {}",
                exposed_sensitive.join(", ")
            ),
            recommendation: Some(
                "Bind sensitive services to 127.0.0.1 or configure a firewall rule.".to_string(),
            ),
        });
    } else {
        checks.push(DoctorCheck {
            category: "Network".to_string(),
            name: "Wildcard Sensitive Listeners".to_string(),
            status: HealthStatus::Pass,
            message:
                "No exposed sensitive database or remote management ports on wildcard addresses"
                    .to_string(),
            recommendation: None,
        });
    }

    if net_report.summary.orphan_sockets > 0 {
        checks.push(DoctorCheck {
            category: "Network".to_string(),
            name: "Orphan Socket Detection".to_string(),
            status: HealthStatus::Warn,
            message: format!(
                "Detected {} sockets without associated process PID in /proc",
                net_report.summary.orphan_sockets
            ),
            recommendation: Some(
                "Run with elevated permissions or inspect kernel socket tables.".to_string(),
            ),
        });
    } else {
        checks.push(DoctorCheck {
            category: "Network".to_string(),
            name: "Orphan Socket Detection".to_string(),
            status: HealthStatus::Pass,
            message: "All open sockets correlate with active processes".to_string(),
            recommendation: None,
        });
    }

    checks
}

use lens_sys::{parse_unit_content, OrderingGraph, SystemdUnit};
use std::collections::BTreeMap;

pub fn load_systemd_units(path: &Path) -> BTreeMap<String, SystemdUnit> {
    let mut units = BTreeMap::new();
    if path.is_file() {
        let name = path.file_name().and_then(|n| n.to_str()).unwrap_or("unit");
        if let Ok(content) = fs::read_to_string(path) {
            let unit = parse_unit_content(&content, name, Some(&path.to_string_lossy()));
            units.insert(name.to_string(), unit);
        }
    } else if path.is_dir() {
        if let Ok(entries) = fs::read_dir(path) {
            for entry in entries.flatten() {
                let p = entry.path();
                if p.is_file() {
                    if let Some(name) = p.file_name().and_then(|n| n.to_str()) {
                        if name.ends_with(".service")
                            || name.ends_with(".target")
                            || name.ends_with(".socket")
                        {
                            if let Ok(content) = fs::read_to_string(&p) {
                                let unit =
                                    parse_unit_content(&content, name, Some(&p.to_string_lossy()));
                                units.insert(name.to_string(), unit);
                            }
                        }
                    }
                }
            }
        }
    }
    units
}

pub fn check_services(systemd_dir: Option<&Path>) -> DoctorCheck {
    let base = systemd_dir.unwrap_or_else(|| Path::new("/etc/systemd/system"));
    if !base.exists() {
        return DoctorCheck {
            category: "Services".to_string(),
            name: "Systemd Dependency Cycles".to_string(),
            status: HealthStatus::Pass,
            message: "No local systemd unit directory to inspect (non-systemd environment)"
                .to_string(),
            recommendation: None,
        };
    }

    let units = load_systemd_units(base);
    let graph = OrderingGraph::build(&units);
    let cycles = graph.find_cycles();

    if !cycles.is_empty() {
        DoctorCheck {
            category: "Services".to_string(),
            name: "Systemd Dependency Cycles".to_string(),
            status: HealthStatus::Fail,
            message: format!(
                "Detected {} cyclic dependency loop(s) in systemd units",
                cycles.len()
            ),
            recommendation: Some(
                "Resolve circular After=/Before=/Requires= directives to prevent boot deadlocks."
                    .to_string(),
            ),
        }
    } else {
        DoctorCheck {
            category: "Services".to_string(),
            name: "Systemd Dependency Cycles".to_string(),
            status: HealthStatus::Pass,
            message: format!(
                "Analyzed {} systemd units; 0 cyclic dependencies found",
                units.len()
            ),
            recommendation: None,
        }
    }
}

pub fn check_environment() -> Vec<DoctorCheck> {
    let mut checks = Vec::new();

    let preload = Path::new("/etc/ld.so.preload");
    if preload.exists() {
        let content = fs::read_to_string(preload).unwrap_or_default();
        let trimmed = content.trim();
        if !trimmed.is_empty() {
            checks.push(DoctorCheck {
                category: "Security".to_string(),
                name: "ld.so.preload Verification".to_string(),
                status: HealthStatus::Warn,
                message: format!("/etc/ld.so.preload is present with entries: {}", trimmed),
                recommendation: Some(
                    "Verify that preloaded shared libraries are intentional and trusted."
                        .to_string(),
                ),
            });
        } else {
            checks.push(DoctorCheck {
                category: "Security".to_string(),
                name: "ld.so.preload Verification".to_string(),
                status: HealthStatus::Pass,
                message: "/etc/ld.so.preload is empty".to_string(),
                recommendation: None,
            });
        }
    } else {
        checks.push(DoctorCheck {
            category: "Security".to_string(),
            name: "ld.so.preload Verification".to_string(),
            status: HealthStatus::Pass,
            message: "/etc/ld.so.preload not present (clean system default)".to_string(),
            recommendation: None,
        });
    }

    if let Ok(path_var) = env::var("PATH") {
        let parts: Vec<&str> = path_var.split(':').collect();
        let mut suspicious = Vec::new();
        for p in &parts {
            if p.is_empty() || *p == "." {
                suspicious.push("Current directory '.' or empty entry in PATH".to_string());
            }
        }
        if !suspicious.is_empty() {
            checks.push(DoctorCheck {
                category: "Environment".to_string(),
                name: "PATH Sanity".to_string(),
                status: HealthStatus::Warn,
                message: format!(
                    "Suspicious entries found in PATH: {}",
                    suspicious.join("; ")
                ),
                recommendation: Some(
                    "Remove relative or empty entries from PATH to prevent binary spoofing."
                        .to_string(),
                ),
            });
        } else {
            checks.push(DoctorCheck {
                category: "Environment".to_string(),
                name: "PATH Sanity".to_string(),
                status: HealthStatus::Pass,
                message: format!(
                    "PATH contains {} search directories without relative entries",
                    parts.len()
                ),
                recommendation: None,
            });
        }
    }

    checks
}

pub fn run_doctor(
    root: Option<&Path>,
    procfs: Option<&Path>,
    sys_dir: Option<&Path>,
) -> DoctorReport {
    let mut checks = Vec::new();

    checks.push(check_storage(root.unwrap_or_else(|| Path::new("/"))));
    checks.extend(check_network(procfs));
    checks.push(check_services(sys_dir));
    checks.extend(check_environment());

    let mut passed = 0;
    let mut warnings = 0;
    let mut failures = 0;

    for c in &checks {
        match c.status {
            HealthStatus::Pass => passed += 1,
            HealthStatus::Warn => warnings += 1,
            HealthStatus::Fail => failures += 1,
        }
    }

    let overall_status = if failures > 0 {
        HealthStatus::Fail
    } else if warnings > 0 {
        HealthStatus::Warn
    } else {
        HealthStatus::Pass
    };

    let summary = DoctorSummary {
        total: checks.len(),
        passed,
        warnings,
        failures,
    };

    DoctorReport {
        schema_version: "lens.doctor/v1".to_string(),
        overall_status,
        checks,
        summary,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_doctor_storage_check() {
        let check = check_storage(Path::new("/"));
        assert_eq!(check.category, "Storage");
        assert!(check.status == HealthStatus::Pass || check.status == HealthStatus::Warn);
    }

    #[test]
    fn test_doctor_environment_check() {
        let checks = check_environment();
        assert!(!checks.is_empty());
        assert!(checks.iter().any(|c| c.name.contains("PATH")));
    }

    #[test]
    fn test_run_doctor_summary() {
        let report = run_doctor(Some(Path::new("/")), None, None);
        assert_eq!(report.schema_version, "lens.doctor/v1");
        assert!(report.summary.total >= 4);
        assert_eq!(
            report.summary.total,
            report.summary.passed + report.summary.warnings + report.summary.failures
        );
    }
}
