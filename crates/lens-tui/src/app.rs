use lens_disk::{DiskScanner, ScanOptions, ScanResult};
use lens_log::LogIndexer;
use lens_net::{inspect_network, NetReport};
use lens_sys::SystemdUnit;
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TuiTab {
    Storage,
    Services,
    Logs,
    Network,
}

impl TuiTab {
    pub fn all() -> &'static [TuiTab] {
        &[
            TuiTab::Storage,
            TuiTab::Services,
            TuiTab::Logs,
            TuiTab::Network,
        ]
    }

    pub fn title(&self) -> &'static str {
        match self {
            TuiTab::Storage => "1: Storage",
            TuiTab::Services => "2: Services",
            TuiTab::Logs => "3: Logs",
            TuiTab::Network => "4: Network",
        }
    }

    pub fn next(&self) -> Self {
        match self {
            TuiTab::Storage => TuiTab::Services,
            TuiTab::Services => TuiTab::Logs,
            TuiTab::Logs => TuiTab::Network,
            TuiTab::Network => TuiTab::Storage,
        }
    }
}

#[derive(Debug)]
pub struct TuiItem {
    pub name: String,
    pub path: PathBuf,
    pub is_dir: bool,
    pub size: u64,
    pub allocated_size: u64,
    pub children_count: usize,
}

pub struct TuiApp {
    pub current_path: PathBuf,
    pub active_tab: TuiTab,
    pub items: Vec<TuiItem>,
    pub selected_index: usize,
    pub total_size: u64,
    pub net_report: Option<NetReport>,
    pub net_selected: usize,
    pub sys_units: Vec<SystemdUnit>,
    pub sys_selected: usize,
    pub log_path: Option<PathBuf>,
    pub log_lines: Vec<String>,
    pub log_scroll: usize,
    pub status_message: String,
    pub should_quit: bool,
}

impl TuiApp {
    pub fn new(initial_path: &Path) -> Self {
        let mut app = Self {
            current_path: initial_path.to_path_buf(),
            active_tab: TuiTab::Storage,
            items: Vec::new(),
            selected_index: 0,
            total_size: 0,
            net_report: None,
            net_selected: 0,
            sys_units: Vec::new(),
            sys_selected: 0,
            log_path: None,
            log_lines: Vec::new(),
            log_scroll: 0,
            status_message: "Ready. [Tab/1-4] Switch tabs  [j/k] Navigate  [Enter] Open  [q] Quit"
                .to_string(),
            should_quit: false,
        };
        app.reload();
        app.reload_network();
        app.reload_services();
        app.reload_logs();
        app
    }

    pub fn next_tab(&mut self) {
        self.active_tab = self.active_tab.next();
    }

    pub fn set_tab(&mut self, tab: TuiTab) {
        self.active_tab = tab;
    }

    pub fn reload_network(&mut self) {
        match inspect_network(None) {
            Ok(rep) => {
                if self.net_selected >= rep.listening.len() {
                    self.net_selected = 0;
                }
                self.net_report = Some(rep);
            }
            Err(_) => {
                // Clear stale data so the UI shows "unavailable", not
                // a snapshot from a previous run.
                self.net_report = None;
                self.net_selected = 0;
            }
        }
    }

    /// Load systemd units (with drop-ins) for the Services tab.
    pub fn reload_services(&mut self) {
        let base = Path::new("/etc/systemd/system");
        match lens_sys::load_units(base) {
            Ok(units) => {
                self.sys_units = units.into_values().collect();
            }
            Err(_) => {
                self.sys_units = Vec::new();
            }
        }
        if self.sys_selected >= self.sys_units.len() {
            self.sys_selected = 0;
        }
    }

    /// Tail the first readable system log for the Logs tab.
    pub fn reload_logs(&mut self) {
        let candidates = [
            "/var/log/syslog",
            "/var/log/messages",
            "/var/log/kern.log",
            "/var/log/daemon.log",
        ];
        let path = candidates
            .iter()
            .map(Path::new)
            .find(|p| p.is_file())
            .map(|p| p.to_path_buf())
            .or_else(|| {
                // Fall back to the first *.log file in /var/log.
                std::fs::read_dir("/var/log").ok().and_then(|rd| {
                    rd.flatten()
                        .map(|e| e.path())
                        .find(|p| p.extension().map(|e| e == "log").unwrap_or(false) && p.is_file())
                })
            });

        let loaded = path.and_then(|p| {
            LogIndexer::open(&p).ok().map(|indexer| {
                let start = indexer.len().saturating_sub(2000);
                let lines: Vec<String> = (start..indexer.len())
                    .filter_map(|i| indexer.get_line(i).map(|s| s.to_string()))
                    .collect();
                (p, lines)
            })
        });
        match loaded {
            Some((p, lines)) => {
                self.log_lines = lines;
                self.log_path = Some(p);
                self.log_scroll = self.log_lines.len();
            }
            None => {
                // No readable log — drop previously shown lines so the tab
                // doesn't display stale output under a new directory.
                self.log_lines.clear();
                self.log_path = None;
                self.log_scroll = 0;
            }
        }
    }

    pub fn reload(&mut self) {
        let scanner = DiskScanner::new(ScanOptions::default());
        if let Ok(res) = scanner.scan(&self.current_path) {
            self.total_size = if res.tree.nodes.is_empty() {
                0
            } else {
                res.tree.nodes[res.root_id as usize].size
            };
            self.populate_items(&res);
        } else {
            // Do not leave the previous directory's entries visible.
            self.items.clear();
            self.total_size = 0;
            self.selected_index = 0;
            self.status_message = format!("Failed to scan {:?}", self.current_path);
        }
    }

    fn populate_items(&mut self, res: &ScanResult) {
        self.items.clear();
        if res.tree.nodes.is_empty() {
            return;
        }

        let children = res.tree.children_ids(res.root_id);
        for child_id in children {
            let node = &res.tree.nodes[child_id as usize];
            let sub_children_count = res.tree.children_ids(child_id).len();
            self.items.push(TuiItem {
                name: node.name.clone(),
                path: self.current_path.join(&node.name),
                is_dir: node.is_dir,
                size: node.size,
                allocated_size: node.allocated_size,
                children_count: sub_children_count,
            });
        }

        self.items.sort_by_key(|b| std::cmp::Reverse(b.size));
        if self.selected_index >= self.items.len() {
            self.selected_index = 0;
        }
    }

    pub fn next(&mut self) {
        match self.active_tab {
            TuiTab::Storage => {
                if !self.items.is_empty() {
                    self.selected_index = (self.selected_index + 1) % self.items.len();
                }
            }
            TuiTab::Services => {
                if !self.sys_units.is_empty() {
                    self.sys_selected = (self.sys_selected + 1) % self.sys_units.len();
                }
            }
            TuiTab::Logs => {
                if self.log_scroll < self.log_lines.len() {
                    self.log_scroll += 1;
                }
            }
            TuiTab::Network => {
                if let Some(rep) = &self.net_report {
                    if !rep.listening.is_empty() {
                        self.net_selected = (self.net_selected + 1) % rep.listening.len();
                    }
                }
            }
        }
    }

    pub fn previous(&mut self) {
        match self.active_tab {
            TuiTab::Storage => {
                if !self.items.is_empty() {
                    if self.selected_index == 0 {
                        self.selected_index = self.items.len() - 1;
                    } else {
                        self.selected_index -= 1;
                    }
                }
            }
            TuiTab::Services => {
                if !self.sys_units.is_empty() {
                    if self.sys_selected == 0 {
                        self.sys_selected = self.sys_units.len() - 1;
                    } else {
                        self.sys_selected -= 1;
                    }
                }
            }
            TuiTab::Logs => {
                self.log_scroll = self.log_scroll.saturating_sub(1);
            }
            TuiTab::Network => {
                if let Some(rep) = &self.net_report {
                    if !rep.listening.is_empty() {
                        if self.net_selected == 0 {
                            self.net_selected = rep.listening.len() - 1;
                        } else {
                            self.net_selected -= 1;
                        }
                    }
                }
            }
        }
    }

    pub fn enter(&mut self) {
        if let Some(item) = self.items.get(self.selected_index) {
            if item.is_dir {
                self.current_path = item.path.clone();
                self.selected_index = 0;
                self.reload();
            }
        }
    }

    pub fn parent(&mut self) {
        if let Some(p) = self.current_path.parent() {
            self.current_path = p.to_path_buf();
            self.selected_index = 0;
            self.reload();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::tempdir;

    #[test]
    fn test_tui_app_navigation() {
        let dir = tempdir().expect("tempdir");
        std::fs::create_dir(dir.path().join("sub")).expect("mkdir");
        std::fs::write(dir.path().join("file.txt"), "hello").expect("write");

        let mut app = TuiApp::new(dir.path());
        assert_eq!(app.items.len(), 2);

        app.next();
        assert_eq!(app.selected_index, 1);
        app.previous();
        assert_eq!(app.selected_index, 0);
    }
}
